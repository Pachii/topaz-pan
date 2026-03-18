#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr auto uiLanguageStateKey = "uiLanguage";
constexpr float haasCompDeadZoneMs = 0.75f;
constexpr float haasCompMaxEffectiveDelayMs = 20.0f;
constexpr float haasCompTauMs = 5.0f;
constexpr float haasCompMaxDbAt100Percent = 2.0f;
constexpr float haasCompNewToOldPercentScale = 0.3f;
constexpr float adtMaxSegmentSeconds = 1.25f;
constexpr float adtSmoothstepPeakSlope = 1.5f;
}

VocalWidenerProcessor::VocalWidenerProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
              // Keep the default/main VST3 arrangement stereo->stereo so
              // hosts that decide insert eligibility from the initial bus
              // layout will still offer the plugin on stereo channels.
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
      15.0f));
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
      juce::ParameterID("centeredTiming", 1), "equal delay", false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("pitchDiff", 1), "adt drift",
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
      juce::ParameterID("haasCompEn", 1), "haas comp", false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("haasCompAmt", 1), "haas comp amount",
      juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 100.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)); })));

  return {params.begin(), params.end()};
}

bool VocalWidenerProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  const auto output = layouts.getMainOutputChannelSet();

  if (output != juce::AudioChannelSet::stereo())
    return false;

  return input == juce::AudioChannelSet::mono() ||
         input == juce::AudioChannelSet::stereo();
}

void VocalWidenerProcessor::prepareToPlay(double sampleRate,
                                          int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);
  currentSampleRate = sampleRate;

  isStereoLayout = ((getTotalNumInputChannels() == 1 ||
                     getTotalNumInputChannels() == 2) &&
                    getTotalNumOutputChannels() == 2);

  const float maxVoiceDelayMs =
      maxCenteredRightDelayMs + maxAdtSharedLatencyMs + 2.0f;
  voiceLeft.prepare(sampleRate, maxVoiceDelayMs, 0x13579BDF);
  voiceRight.prepare(sampleRate, maxVoiceDelayMs, 0x2468ACE0);

  const int maxLatencySamples =
      juce::roundToInt((maxReportedLatencyMs * static_cast<float>(sampleRate)) /
                       1000.0f);
  dryDelayLeft.prepare(maxLatencySamples);
  dryDelayRight.prepare(maxLatencySamples);

  activeLatencySamples = computeLatencySamples(
      offsetTimeParam->load(std::memory_order_relaxed),
      centeredTimingParam->load(std::memory_order_relaxed) > 0.5f,
      pitchDiffParam->load(std::memory_order_relaxed));
  pendingLatencySamples.store(activeLatencySamples, std::memory_order_relaxed);
  dryDelayLeft.setDelaySamples(activeLatencySamples);
  dryDelayRight.setDelaySamples(activeLatencySamples);
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
      computeLatencySamples(offsetMs, centered, pDiff);
  if (targetLatencySamples != activeLatencySamples) {
    activeLatencySamples = targetLatencySamples;
    dryDelayLeft.setDelaySamples(targetLatencySamples);
    dryDelayRight.setDelaySamples(targetLatencySamples);
    queueLatencyUpdate(targetLatencySamples);
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

  const auto [leftDriftAmountCents, rightDriftAmountCents] =
      computeVoiceDriftAmounts(pDiff);
  const float sharedAdtLatencyMs = computeAdtSharedLatencyMs(pDiff);

  if (pitchShiftActive != wasPitchShiftActive) {
    voiceLeft.reset();
    voiceRight.reset();
    wasPitchShiftActive = pitchShiftActive;
  }

  // Update readouts for UI
  leftDelayReadout.store(leftReadoutMs, std::memory_order_relaxed);
  rightDelayReadout.store(rightReadoutMs, std::memory_order_relaxed);
  leftPitchReadout.store(0.0f, std::memory_order_relaxed);
  rightPitchReadout.store(0.0f, std::memory_order_relaxed);

  voiceLeft.configure(delayTLeftMs, sharedAdtLatencyMs, leftDriftAmountCents);
  voiceRight.configure(delayTRightMs, sharedAdtLatencyMs, rightDriftAmountCents);

  // Precedence Compensation
  const bool linkPanEnabled =
      linkPanParam->load(std::memory_order_relaxed) > 0.5f;
  bool haasEnable = haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f &&
                    linkPanEnabled;
  float haasAmtNorm =
      (haasCompAmtParam->load(std::memory_order_relaxed) *
       haasCompNewToOldPercentScale) /
      100.0f;

  float targetLeftCompDb = 0.0f;
  float targetRightCompDb = 0.0f;
  float earlierPath = -1.0f; // Tie

  if (haasEnable) {
    float deltaMs = std::abs(delayTLeftMs - delayTRightMs);
    if (deltaMs > haasCompDeadZoneMs &&
        hasMeaningfulPanSeparation) // Only correct if noticeably different
    {
      // Psychoacoustically motivated heuristic:
      // - ignore sub-millisecond delays, where precedence is not yet a stable
      //   lead-dominance problem for this use case
      // - saturate over the first few milliseconds
      // - stop growing once the delay is well into echo/splitting territory
      const float effectiveDeltaMs =
          juce::jlimit(0.0f, haasCompMaxEffectiveDelayMs - haasCompDeadZoneMs,
                       deltaMs - haasCompDeadZoneMs);
      const float factor = 1.0f - std::exp(-effectiveDeltaMs / haasCompTauMs);

      // Stretch the control so 100% now lands where the old 30% setting was.
      // The 300% top end therefore reaches roughly the old 90% response.
      const float compDiffDb = factor * haasAmtNorm *
                               haasCompMaxDbAt100Percent *
                               panSeparationWeight;

      // Convert the desired lead-vs-lag differential into constant-power path
      // gains so the compensation acts like image balancing, not an accidental
      // loudness knob.
      const float gainRatio = juce::Decibels::decibelsToGain(compDiffDb);
      const float earlyGain =
          std::sqrt(2.0f / (1.0f + (gainRatio * gainRatio)));
      const float lateGain = gainRatio * earlyGain;
      const float earlyCompDb = juce::Decibels::gainToDecibels(earlyGain);
      const float lateCompDb = juce::Decibels::gainToDecibels(lateGain);

      if (delayTLeftMs < delayTRightMs) {
        // Left structure is earlier
        targetLeftCompDb = earlyCompDb;
        targetRightCompDb = lateCompDb;

        if (std::abs(leftPan - rightPan) < 0.05f)
          earlierPath = -1.0f;
        else
          earlierPath = (leftPan < rightPan) ? 0.0f : 1.0f;
      } else {
        // Right structure is earlier
        targetLeftCompDb = lateCompDb;
        targetRightCompDb = earlyCompDb;

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
  const auto *inputL = buffer.getReadPointer(0);
  const auto *inputR =
      getTotalNumInputChannels() > 1 ? buffer.getReadPointer(1) : nullptr;

  int numSamples = buffer.getNumSamples();

  for (int channel = getTotalNumOutputChannels(); channel < buffer.getNumChannels();
       ++channel)
    buffer.clear(channel, 0, numSamples);

  // Haas comp smoothing coefficient (~5ms time constant, sample-rate independent)
  const float smoothCoeff = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(currentSampleRate)));

  for (int i = 0; i < numSamples; ++i) {
    const float sourceL = inputL[i];
    const float sourceR = inputR != nullptr ? inputR[i] : sourceL;
    const float monoInput =
        inputR != nullptr ? (sourceL + sourceR) * 0.5f : sourceL;
    const float dryL = dryDelayLeft.processSample(sourceL);
    const float dryR = dryDelayRight.processSample(sourceR);

    if (bypassed) {
      channelL[i] = dryL;
      channelR[i] = dryR;
      continue;
    }

    currentLeftCompDb += (targetLeftCompDb - currentLeftCompDb) * smoothCoeff;
    currentRightCompDb += (targetRightCompDb - currentRightCompDb) * smoothCoeff;

    float gPrecLeft = juce::Decibels::decibelsToGain(currentLeftCompDb);
    float gPrecRight = juce::Decibels::decibelsToGain(currentRightCompDb);

    // 1. Mono conversion
    float in = monoInput;

    // Path A (Left)
    float sA = voiceLeft.processSample(in) * gPrecLeft;

    // Path B (Right)
    float sB = voiceRight.processSample(in) * gPrecRight;

    // Pan and Mix
    float outL = (sA * leftPL) + (sB * rightPL);
    float outR = (sA * leftPR) + (sB * rightPR);

    // Output Gain
    channelL[i] = outL * gainLinear;
    channelR[i] = outR * gainLinear;
  }

  leftPitchReadout.store(
      pitchShiftActive ? voiceLeft.getCurrentDriftCents() : 0.0f,
      std::memory_order_relaxed);
  rightPitchReadout.store(
      pitchShiftActive ? voiceRight.getCurrentDriftCents() : 0.0f,
      std::memory_order_relaxed);
}

bool VocalWidenerProcessor::isPitchShiftActive(float pitchDiffCents) const {
  return std::abs(pitchDiffCents) >= 0.01f;
}

int VocalWidenerProcessor::computeLatencySamples(float offsetMs, bool centered,
                                                 float driftAmountCents) const {
  const float totalLatencyMs =
      computeAdtSharedLatencyMs(driftAmountCents) +
      (centered ? (offsetMs * 0.5f) : 0.0f);
  return juce::roundToInt(
      (totalLatencyMs * static_cast<float>(currentSampleRate)) / 1000.0f);
}

float VocalWidenerProcessor::computeAdtSharedLatencyMs(float driftAmountCents) const {
  const auto [leftDriftCents, rightDriftCents] =
      computeVoiceDriftAmounts(driftAmountCents);
  const float maxVoiceDriftCents = juce::jmax(leftDriftCents, rightDriftCents);

  if (maxVoiceDriftCents < 0.01f)
    return 0.0f;

  const double speedDelta =
      std::abs(1.0 - std::pow(2.0, maxVoiceDriftCents / 1200.0));
  const double excursionSeconds =
      (speedDelta * static_cast<double>(adtMaxSegmentSeconds)) /
      adtSmoothstepPeakSlope;
  return static_cast<float>((excursionSeconds * 1000.0) + 0.5);
}

std::pair<float, float> VocalWidenerProcessor::computeVoiceDriftAmounts(
    float driftAmountCents) const {
  const float clampedAmount = juce::jmax(0.0f, driftAmountCents);
  return {clampedAmount, clampedAmount};
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
  const float driftAmountCents =
      pitchDiffParam != nullptr
          ? pitchDiffParam->load(std::memory_order_relaxed)
          : 0.0f;
  const int latencySamples =
      computeLatencySamples(offsetMs, centered, driftAmountCents);

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
