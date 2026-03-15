#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr auto uiLanguageStateKey = "uiLanguage";
}

VocalWidenerProcessor::VocalWidenerProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#else
    :
#endif
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
  offsetTimeParam = apvts.getRawParameterValue("offsetTime");
  leftPanParam = apvts.getRawParameterValue("leftPan");
  rightPanParam = apvts.getRawParameterValue("rightPan");
  centeredTimingParam = apvts.getRawParameterValue("centeredTiming");
  equalPitchShiftParam = apvts.getRawParameterValue("equalPitchShift");
  pitchDiffParam = apvts.getRawParameterValue("pitchDiff");
  outputGainParam = apvts.getRawParameterValue("outGain");
  bypassParam = apvts.getRawParameterValue("bypass");
  linkPanParam = apvts.getRawParameterValue("linkPan");
  flipPanParam = apvts.getRawParameterValue("flipPan");
  haasCompEnableParam = apvts.getRawParameterValue("haasCompEn");
  haasCompAmtParam = apvts.getRawParameterValue("haasCompAmt");

  apvts.addParameterListener("leftPan", this);
  apvts.addParameterListener("rightPan", this);
  apvts.addParameterListener("linkPan", this);

  setLanguageCode(apvts.state.getProperty(uiLanguageStateKey, "en").toString());
}

VocalWidenerProcessor::~VocalWidenerProcessor() {
  apvts.removeParameterListener("leftPan", this);
  apvts.removeParameterListener("rightPan", this);
  apvts.removeParameterListener("linkPan", this);
}

void VocalWidenerProcessor::parameterChanged(const juce::String &parameterID,
                                             float newValue) {
  if (isLinkingPan.exchange(true))
    return;

  bool linked = linkPanParam != nullptr &&
                linkPanParam->load(std::memory_order_relaxed) > 0.5f;

  if (linked) {
    if (parameterID == "leftPan") {
      if (auto *p = apvts.getParameter("rightPan"))
        p->setValueNotifyingHost(p->convertTo0to1(newValue));
    } else if (parameterID == "rightPan") {
      if (auto *p = apvts.getParameter("leftPan"))
        p->setValueNotifyingHost(p->convertTo0to1(newValue));
    } else if (parameterID == "linkPan") {
      if (auto *pLeft = apvts.getParameter("leftPan"))
        if (auto *pRight = apvts.getParameter("rightPan")) {
          float leftVal = pLeft->convertFrom0to1(pLeft->getValue());
          pRight->setValueNotifyingHost(pRight->convertTo0to1(leftVal));
        }
    }
  }

  isLinkingPan = false;
}

juce::AudioProcessorValueTreeState::ParameterLayout
VocalWidenerProcessor::createParameterLayout() {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("offsetTime", 1), "offset time", 0.0f, maxOffsetMs,
      10.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("leftPan", 1), "left pan",
      juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)); })));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("rightPan", 1), "right pan",
      juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)); })));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("centeredTiming", 1), "equal delay", true));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("equalPitchShift", 1), "equal pitch shift", false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("pitchDiff", 1), "pitch shift",
      juce::NormalisableRange<float>(0.0f, 20.0f, 0.01f), 0.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return juce::String(v, 2); })));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("outGain", 1), "output gain",
      juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) {
            return juce::String(std::abs(v) < 0.05f ? 0.0f : v, 1);
          })));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("bypass", 1), "bypass", false));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("linkPan", 1), "link pan", true));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("flipPan", 1), "flip pan", false));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("haasCompEn", 1), "haas comp", true));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("haasCompAmt", 1), "haas comp amount",
      juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)); })));

  return {params.begin(), params.end()};
}

bool VocalWidenerProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  // ONLY support Stereo in / Stereo out
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;
  if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
    return false;
  return true;
}

void VocalWidenerProcessor::prepareToPlay(double sampleRate,
                                          int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);
  currentSampleRate = sampleRate;

  isStereoLayout =
      (getTotalNumInputChannels() == 2 && getTotalNumOutputChannels() == 2);

  delayLeft.prepare(sampleRate, maxCenteredRightDelayMs);
  delayRight.prepare(sampleRate, maxCenteredRightDelayMs);
  pitchLeft.prepare(sampleRate, pitchWindowMs);
  pitchRight.prepare(sampleRate, pitchWindowMs);

  const int maxLatencySamples =
      juce::roundToInt((maxReportedLatencyMs * static_cast<float>(sampleRate)) /
                       1000.0f);
  dryDelayLeft.prepare(maxLatencySamples);
  dryDelayRight.prepare(maxLatencySamples);

  activeLatencySamples = computeLatencySamples(
      offsetTimeParam->load(std::memory_order_relaxed),
      centeredTimingParam->load(std::memory_order_relaxed) > 0.5f,
      isPitchShiftActive(pitchDiffParam->load(std::memory_order_relaxed)));
  pendingLatencySamples.store(activeLatencySamples, std::memory_order_relaxed);
  setLatencySamples(activeLatencySamples);
  wasPitchShiftActive =
      isPitchShiftActive(pitchDiffParam->load(std::memory_order_relaxed));
}

void VocalWidenerProcessor::releaseResources() {}

void VocalWidenerProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                         juce::MidiBuffer &midiMessages) {
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;

  // Fast bypass and Non-Stereo Layout safe exit
  if (!isStereoLayout)
    return;

  // Load parameters (Realtime safe)
  float offsetMs = offsetTimeParam->load(std::memory_order_relaxed);
  bool centered = centeredTimingParam->load(std::memory_order_relaxed) > 0.5f;
  bool equalPitchShift =
      equalPitchShiftParam->load(std::memory_order_relaxed) > 0.5f;
  bool bypassed = bypassParam->load(std::memory_order_relaxed) > 0.5f;
  float pDiff = pitchDiffParam->load(std::memory_order_relaxed);
  const bool pitchShiftActive = isPitchShiftActive(pDiff);
  float gainLinear = juce::Decibels::decibelsToGain(
      outputGainParam->load(std::memory_order_relaxed));

  const float leftPanAmount =
      juce::jlimit(0.0f, 100.0f, leftPanParam->load(std::memory_order_relaxed));
  const float rightPanAmount = juce::jlimit(
      0.0f, 100.0f, rightPanParam->load(std::memory_order_relaxed));
  const bool flipPan = flipPanParam->load(std::memory_order_relaxed) > 0.5f;

  const float panDirection = flipPan ? 1.0f : -1.0f;
  float leftPan = panDirection * (leftPanAmount / 100.0f);
  float rightPan = -panDirection * (rightPanAmount / 100.0f);

  // Equal-power pan coefficients.
  // Math: left = cos((pan+1) * pi/4), right = sin((pan+1) * pi/4)
  const float leftPL =
      std::cos((leftPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
  const float leftPR =
      std::sin((leftPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
  const float rightPL =
      std::cos((rightPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
  const float rightPR =
      std::sin((rightPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

  // Practical Haas heuristic: scale the gain compensation by the actual
  // left/right separation created by the equal-power panners. This makes the
  // compensation collapse toward zero as both voices approach the center.
  const float leftBalance = leftPR - leftPL;
  const float rightBalance = rightPR - rightPL;
  const float panSeparationWeight =
      juce::jlimit(0.0f, 1.0f, std::abs(rightBalance - leftBalance) * 0.5f);
  const bool hasMeaningfulPanSeparation = panSeparationWeight >= 0.03f;

  const int targetLatencySamples =
      computeLatencySamples(offsetMs, centered, pitchShiftActive);
  if (targetLatencySamples != activeLatencySamples) {
    activeLatencySamples = targetLatencySamples;
    dryDelayLeft.setDelaySamples(targetLatencySamples);
    dryDelayRight.setDelaySamples(targetLatencySamples);
    queueLatencyUpdate(targetLatencySamples);
  } else {
    dryDelayLeft.setDelaySamples(activeLatencySamples);
    dryDelayRight.setDelaySamples(activeLatencySamples);
  }

  // Calculate DSP state per path.
  // In equal-delay mode the host latency compensation does the centering for
  // us. Reporting half the offset as plugin latency shifts the rest of the mix
  // later by that amount, so the effective result becomes -offset/2 on the
  // left and +offset/2 on the right without changing the internal L/R delta.
  float delayTLeftMs = 0.0f;
  float delayTRightMs = offsetMs;

  float leftReadoutMs = centered ? -(offsetMs * 0.5f) : 0.0f;
  float rightReadoutMs = centered ? (offsetMs * 0.5f) : offsetMs;

  float detuneLeftCents = equalPitchShift ? -(pDiff * 0.5f) : 0.0f;
  float detuneRightCents = equalPitchShift ? (pDiff * 0.5f) : pDiff;

  if (pitchShiftActive != wasPitchShiftActive) {
    pitchLeft.reset();
    pitchRight.reset();
    wasPitchShiftActive = pitchShiftActive;
  }

  // Update readouts for UI
  leftDelayReadout.store(leftReadoutMs, std::memory_order_relaxed);
  rightDelayReadout.store(rightReadoutMs, std::memory_order_relaxed);
  leftPitchReadout.store(detuneLeftCents, std::memory_order_relaxed);
  rightPitchReadout.store(detuneRightCents, std::memory_order_relaxed);

  delayLeft.setTargetDelayMs(delayTLeftMs);
  delayRight.setTargetDelayMs(delayTRightMs);
  pitchLeft.setPitchOffsetCents(detuneLeftCents);
  pitchRight.setPitchOffsetCents(detuneRightCents);

  // Precedence Compensation
  const bool linkPanEnabled =
      linkPanParam->load(std::memory_order_relaxed) > 0.5f;
  bool haasEnable = haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f &&
                    linkPanEnabled;
  float haasAmtNorm =
      haasCompAmtParam->load(std::memory_order_relaxed) / 100.0f;

  float targetLeftCompDb = 0.0f;
  float targetRightCompDb = 0.0f;
  float earlierPath = -1.0f; // Tie

  if (haasEnable) {
    float deltaMs = std::abs(delayTLeftMs - delayTRightMs);
    if (deltaMs > 0.1f &&
        hasMeaningfulPanSeparation) // Only correct if noticeably different
    {
      // Exponential saturation model (tau = 8.0f)
      float factor = 1.0f - std::exp(-deltaMs / 8.0f);

      // Hardcoded max dB = 2.0f
      float compDb = factor * haasAmtNorm * 2.0f * panSeparationWeight;

      if (delayTLeftMs < delayTRightMs) {
        // Left structure is earlier
        targetLeftCompDb = -0.5f * compDb;
        targetRightCompDb = +0.5f * compDb;

        if (std::abs(leftPan - rightPan) < 0.05f)
          earlierPath = -1.0f;
        else
          earlierPath = (leftPan < rightPan) ? 0.0f : 1.0f;
      } else {
        // Right structure is earlier
        targetLeftCompDb = +0.5f * compDb;
        targetRightCompDb = -0.5f * compDb;

        if (std::abs(leftPan - rightPan) < 0.05f)
          earlierPath = -1.0f;
        else
          earlierPath = (rightPan < leftPan) ? 0.0f : 1.0f;
      }
    }
  }

  earlierPathReadout.store(earlierPath, std::memory_order_relaxed);
  leftCompReadout.store(targetLeftCompDb, std::memory_order_relaxed);
  rightCompReadout.store(targetRightCompDb, std::memory_order_relaxed);

  auto *channelL = buffer.getWritePointer(0);
  auto *channelR = buffer.getWritePointer(1);

  int numSamples = buffer.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {
    const float dryL = dryDelayLeft.processSample(channelL[i]);
    const float dryR = dryDelayRight.processSample(channelR[i]);

    if (bypassed) {
      channelL[i] = dryL;
      channelR[i] = dryR;
      continue;
    }

    // Smooth Prec. Comp values
    currentLeftCompDb += (targetLeftCompDb - currentLeftCompDb) * 0.005f;
    currentRightCompDb += (targetRightCompDb - currentRightCompDb) * 0.005f;

    float gPrecLeft = juce::Decibels::decibelsToGain(currentLeftCompDb);
    float gPrecRight = juce::Decibels::decibelsToGain(currentRightCompDb);

    // 1. Mono conversion
    float in = (channelL[i] + channelR[i]) * 0.5f;

    // Path A (Left)
    float sA = pitchShiftActive ? pitchLeft.processSample(in)
                                : pitchLeft.processBypassedSample(in);
    sA = delayLeft.processSample(sA) * gPrecLeft;

    // Path B (Right)
    float sB = pitchShiftActive ? pitchRight.processSample(in)
                                : pitchRight.processBypassedSample(in);
    sB = delayRight.processSample(sB) * gPrecRight;

    // Pan and Mix
    float outL = (sA * leftPL) + (sB * rightPL);
    float outR = (sA * leftPR) + (sB * rightPR);

    // Output Gain
    channelL[i] = outL * gainLinear;
    channelR[i] = outR * gainLinear;
  }
}

bool VocalWidenerProcessor::isPitchShiftActive(float pitchDiffCents) const {
  return std::abs(pitchDiffCents) >= 0.01f;
}

int VocalWidenerProcessor::computeLatencySamples(float offsetMs, bool centered,
                                                 bool pitchShiftActive) const {
  const float totalLatencyMs =
      (pitchShiftActive ? pitchLatencyMs : 0.0f) +
      (centered ? (offsetMs * 0.5f) : 0.0f);
  return juce::roundToInt(
      (totalLatencyMs * static_cast<float>(currentSampleRate)) / 1000.0f);
}

float VocalWidenerProcessor::getReportedLatencyMs() const {
  if (currentSampleRate <= 0.0)
    return 0.0f;

  const float offsetMs =
      offsetTimeParam != nullptr ? offsetTimeParam->load(std::memory_order_relaxed)
                                 : 0.0f;
  const bool centered =
      centeredTimingParam != nullptr &&
      centeredTimingParam->load(std::memory_order_relaxed) > 0.5f;
  const bool pitchShiftActive =
      pitchDiffParam != nullptr &&
      isPitchShiftActive(pitchDiffParam->load(std::memory_order_relaxed));
  const int latencySamples =
      computeLatencySamples(offsetMs, centered, pitchShiftActive);

  return (static_cast<float>(latencySamples) * 1000.0f) /
         static_cast<float>(currentSampleRate);
}

void VocalWidenerProcessor::queueLatencyUpdate(int latencySamples) {
  pendingLatencySamples.store(latencySamples, std::memory_order_relaxed);
  triggerAsyncUpdate();
}

juce::String VocalWidenerProcessor::normaliseLanguageCode(
    juce::String languageCodeToNormalise) {
  auto code = languageCodeToNormalise.trim().toLowerCase();

  if (code == "ja" || code == "jp" || code == "japanese" ||
      code == juce::String::fromUTF8("日本語"))
    return "ja";

  return "en";
}

juce::String VocalWidenerProcessor::getLanguageCode() const {
  return normaliseLanguageCode(languageCode);
}

void VocalWidenerProcessor::setLanguageCode(
    const juce::String &newLanguageCode) {
  languageCode = normaliseLanguageCode(newLanguageCode);
  apvts.state.setProperty(uiLanguageStateKey, languageCode, nullptr);
}

void VocalWidenerProcessor::handleAsyncUpdate() {
  setLatencySamples(pendingLatencySamples.load(std::memory_order_relaxed));
}

void VocalWidenerProcessor::getStateInformation(juce::MemoryBlock &destData) {
  auto state = apvts.copyState();
  state.setProperty(uiLanguageStateKey, getLanguageCode(), nullptr);
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void VocalWidenerProcessor::setStateInformation(const void *data,
                                                int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));
  if (xmlState.get() != nullptr)
    if (xmlState->hasTagName(apvts.state.getType())) {
      apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
      setLanguageCode(apvts.state.getProperty(uiLanguageStateKey, "en")
                          .toString());
    }
}

juce::AudioProcessorEditor *VocalWidenerProcessor::createEditor() {
  return new VocalWidenerEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new VocalWidenerProcessor();
}
