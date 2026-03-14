#include "PluginProcessor.h"
#include "PluginEditor.h"

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
  pitchDiffParam = apvts.getRawParameterValue("pitchDiff");
  outputGainParam = apvts.getRawParameterValue("outGain");
  bypassParam = apvts.getRawParameterValue("bypass");
  linkPanParam = apvts.getRawParameterValue("linkPan");
  haasCompEnableParam = apvts.getRawParameterValue("haasCompEn");
  haasCompAmtParam = apvts.getRawParameterValue("haasCompAmt");

  apvts.addParameterListener("leftPan", this);
  apvts.addParameterListener("rightPan", this);
  apvts.addParameterListener("linkPan", this);
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
        p->setValueNotifyingHost(p->convertTo0to1(-newValue));
    } else if (parameterID == "rightPan") {
      if (auto *p = apvts.getParameter("leftPan"))
        p->setValueNotifyingHost(p->convertTo0to1(-newValue));
    } else if (parameterID == "linkPan") {
      if (auto *pLeft = apvts.getParameter("leftPan"))
        if (auto *pRight = apvts.getParameter("rightPan")) {
          float leftVal = pLeft->convertFrom0to1(pLeft->getValue());
          pRight->setValueNotifyingHost(pRight->convertTo0to1(-leftVal));
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
      juce::ParameterID("leftPan", 1), "left pan", -1.0f, 1.0f, -1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("rightPan", 1), "right pan", -1.0f, 1.0f, 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID("centeredTiming", 1), "equal delay", false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID("pitchDiff", 1), "pitch difference",
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
      juce::ParameterID("haasCompEn", 1), "haas comp", false));
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

  delayLeft.prepare(sampleRate, maxOffsetMs);
  delayRight.prepare(sampleRate, maxOffsetMs);
  pitchLeft.prepare(sampleRate, pitchWindowMs);
  pitchRight.prepare(sampleRate, pitchWindowMs);

  const int maxLatencySamples =
      juce::roundToInt((maxReportedLatencyMs * static_cast<float>(sampleRate)) /
                       1000.0f);
  dryDelayLeft.prepare(maxLatencySamples);
  dryDelayRight.prepare(maxLatencySamples);

  activeLatencySamples = computeLatencySamples(
      offsetTimeParam->load(std::memory_order_relaxed),
      centeredTimingParam->load(std::memory_order_relaxed) > 0.5f);
  pendingLatencySamples.store(activeLatencySamples, std::memory_order_relaxed);
  setLatencySamples(activeLatencySamples);
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
  float gainLinear = juce::Decibels::decibelsToGain(
      outputGainParam->load(std::memory_order_relaxed));

  float leftPan = leftPanParam->load(std::memory_order_relaxed);
  float rightPan = rightPanParam->load(std::memory_order_relaxed);

  const int targetLatencySamples = computeLatencySamples(offsetMs, centered);
  if (targetLatencySamples != activeLatencySamples) {
    activeLatencySamples = targetLatencySamples;
    dryDelayLeft.setDelaySamples(targetLatencySamples);
    dryDelayRight.setDelaySamples(targetLatencySamples);
    queueLatencyUpdate(targetLatencySamples);
  } else {
    dryDelayLeft.setDelaySamples(activeLatencySamples);
    dryDelayRight.setDelaySamples(activeLatencySamples);
  }

  // Calculate DSP state per path
  float delayTLeftMs = centered ? (offsetMs * 0.5f) : 0.0f;
  float delayTRightMs = offsetMs;

  float leftReadoutMs = centered ? -(offsetMs * 0.5f) : 0.0f;
  float rightReadoutMs = centered ? (offsetMs * 0.5f) : offsetMs;

  float detuneLeftCents = -(pDiff * 0.5f);
  float detuneRightCents = (pDiff * 0.5f);

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
  bool haasEnable = haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f;
  float haasAmtNorm =
      haasCompAmtParam->load(std::memory_order_relaxed) / 100.0f;

  float targetLeftCompDb = 0.0f;
  float targetRightCompDb = 0.0f;
  float earlierPath = -1.0f; // Tie

  if (haasEnable) {
    float deltaMs = std::abs(delayTLeftMs - delayTRightMs);
    if (deltaMs > 0.1f) // Only correct if noticeably different
    {
      // Exponential saturation model (tau = 8.0f)
      float factor = 1.0f - std::exp(-deltaMs / 8.0f);

      // Hardcoded max dB = 2.0f
      float compDb = factor * haasAmtNorm * 2.0f;

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

  // Equal-power Pan coefficients
  // Math: left = cos((pan+1) * pi/4), right = sin((pan+1) * pi/4)
  float leftPL =
      std::cos((leftPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
  float leftPR =
      std::sin((leftPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

  float rightPL =
      std::cos((rightPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
  float rightPR =
      std::sin((rightPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

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
    float sA = pitchLeft.processSample(in);
    sA = delayLeft.processSample(sA) * gPrecLeft;

    // Path B (Right)
    float sB = pitchRight.processSample(in);
    sB = delayRight.processSample(sB) * gPrecRight;

    // Pan and Mix
    float outL = (sA * leftPL) + (sB * rightPL);
    float outR = (sA * leftPR) + (sB * rightPR);

    // Output Gain
    channelL[i] = outL * gainLinear;
    channelR[i] = outR * gainLinear;
  }
}

int VocalWidenerProcessor::computeLatencySamples(float offsetMs,
                                                 bool centered) const {
  const float totalLatencyMs =
      pitchLatencyMs + (centered ? (offsetMs * 0.5f) : 0.0f);
  return juce::roundToInt(
      (totalLatencyMs * static_cast<float>(currentSampleRate)) / 1000.0f);
}

void VocalWidenerProcessor::queueLatencyUpdate(int latencySamples) {
  pendingLatencySamples.store(latencySamples, std::memory_order_relaxed);
  triggerAsyncUpdate();
}

void VocalWidenerProcessor::handleAsyncUpdate() {
  setLatencySamples(pendingLatencySamples.load(std::memory_order_relaxed));
}

void VocalWidenerProcessor::getStateInformation(juce::MemoryBlock &destData) {
  auto state = apvts.copyState();
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void VocalWidenerProcessor::setStateInformation(const void *data,
                                                int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));
  if (xmlState.get() != nullptr)
    if (xmlState->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor *VocalWidenerProcessor::createEditor() {
  return new VocalWidenerEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new VocalWidenerProcessor();
}
