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
constexpr double latencyCommitIdleWindowMs = 200.0;

struct HaasCompensationState {
  float leftCompDb = 0.0f;
  float rightCompDb = 0.0f;
  float earlierPath = -1.0f;
};

struct ProcessActivityGuard {
  explicit ProcessActivityGuard(std::atomic<int> &counterIn)
      : counter(counterIn) {
    counter.fetch_add(1, std::memory_order_acq_rel);
  }

  ~ProcessActivityGuard() { counter.fetch_sub(1, std::memory_order_acq_rel); }

  std::atomic<int> &counter;
};

bool isLatencyAffectingParameter(const juce::String &parameterID) {
  return parameterID == "offsetTime" || parameterID == "centeredTiming" ||
         parameterID == "pitchDiff";
}

HaasCompensationState computeHaasCompensationState(
    float leftDelayMs, float rightDelayMs, bool enabled, float amountNorm,
    float panSeparationWeight, bool hasMeaningfulPanSeparation) {
  HaasCompensationState state;

  if (!enabled || !hasMeaningfulPanSeparation)
    return state;

  const float deltaMs = std::abs(leftDelayMs - rightDelayMs);
  if (deltaMs <= haasCompDeadZoneMs)
    return state;

  const float effectiveDeltaMs =
      juce::jlimit(0.0f, haasCompMaxEffectiveDelayMs - haasCompDeadZoneMs,
                   deltaMs - haasCompDeadZoneMs);
  const float factor = 1.0f - std::exp(-effectiveDeltaMs / haasCompTauMs);

  // Stretch the control so 100% now lands where the old 30% setting was.
  // The 300% top end therefore reaches roughly the old 90% response.
  const float compDiffDb =
      factor * amountNorm * haasCompMaxDbAt100Percent * panSeparationWeight;

  // Convert the desired lead-vs-lag differential into constant-power path
  // gains so the compensation acts like image balancing, not an accidental
  // loudness knob.
  const float gainRatio = juce::Decibels::decibelsToGain(compDiffDb);
  const float earlyGain =
      std::sqrt(2.0f / (1.0f + (gainRatio * gainRatio)));
  const float lateGain = gainRatio * earlyGain;
  const float earlyCompDb = juce::Decibels::gainToDecibels(earlyGain);
  const float lateCompDb = juce::Decibels::gainToDecibels(lateGain);

  if (leftDelayMs < rightDelayMs) {
    state.leftCompDb = earlyCompDb;
    state.rightCompDb = lateCompDb;
    state.earlierPath = 0.0f;
  } else if (rightDelayMs < leftDelayMs) {
    state.leftCompDb = lateCompDb;
    state.rightCompDb = earlyCompDb;
    state.earlierPath = 1.0f;
  }

  return state;
}
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
  apvts.addParameterListener("offsetTime", this);
  apvts.addParameterListener("centeredTiming", this);
  apvts.addParameterListener("pitchDiff", this);

  setLanguageCode(apvts.state.getProperty(uiLanguageStateKey, "en").toString());
  syncLinkedPanStateFromParameters(false);
}

VocalWidenerProcessor::~VocalWidenerProcessor() {
  apvts.removeParameterListener("leftPan", this);
  apvts.removeParameterListener("rightPan", this);
  apvts.removeParameterListener("linkPan", this);
  apvts.removeParameterListener("offsetTime", this);
  apvts.removeParameterListener("centeredTiming", this);
  apvts.removeParameterListener("pitchDiff", this);
}

void VocalWidenerProcessor::parameterChanged(const juce::String &parameterID,
                                             float newValue) {
  if (isLatencyAffectingParameter(parameterID)) {
    handleLatencyParameterChanged(parameterID, newValue);
    return;
  }

  const auto mirroredTarget = static_cast<PanMirrorTarget>(
      panMirrorWriteTarget.load(std::memory_order_relaxed));
  if (((parameterID == "leftPan" && mirroredTarget == mirrorLeftPan) ||
       (parameterID == "rightPan" && mirroredTarget == mirrorRightPan)) &&
      std::abs(newValue -
               panMirrorWriteValue.load(std::memory_order_relaxed)) < 0.0001f) {
    return;
  }

  const bool linked =
      parameterID == "linkPan"
          ? (newValue > 0.5f)
          : (linkPanParam != nullptr &&
             linkPanParam->load(std::memory_order_relaxed) > 0.5f);

  if (!linked) {
    if (parameterID == "leftPan" || parameterID == "rightPan")
      linkedPanValue.store(newValue, std::memory_order_relaxed);

    pendingPanMirrorTarget.store(noPanMirrorPending, std::memory_order_relaxed);
    return;
  }

  if (parameterID == "leftPan") {
    linkedPanValue.store(newValue, std::memory_order_relaxed);
    scheduleLinkedPanMirror(mirrorRightPan, newValue);
  } else if (parameterID == "rightPan") {
    linkedPanValue.store(newValue, std::memory_order_relaxed);
    scheduleLinkedPanMirror(mirrorLeftPan, newValue);
  } else if (parameterID == "linkPan") {
    const float leftValue =
        leftPanParam != nullptr
            ? leftPanParam->load(std::memory_order_relaxed)
            : 100.0f;
    linkedPanValue.store(leftValue, std::memory_order_relaxed);
    scheduleLinkedPanMirror(mirrorRightPan, leftValue);
  }
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
      maxCenteredRightDelayMs + computeAdtSharedLatencyMs(maxPitchDiffCents) +
      computeAdtMaxExcursionMs(maxPitchDiffCents) + maxLatencyGuardMs;
  voiceLeft.prepare(sampleRate, maxVoiceDelayMs, 0x13579BDF);
  voiceRight.prepare(sampleRate, maxVoiceDelayMs, 0x2468ACE0);

  const int maxLatencySamples =
      juce::roundToInt(((computeAdtSharedLatencyMs(maxPitchDiffCents) +
                         maxOffsetMs) *
                        static_cast<float>(sampleRate)) /
                       1000.0f);
  dryDelayLeft.prepare(maxLatencySamples);
  dryDelayRight.prepare(maxLatencySamples);

  syncLatencyStateFromParameters(true);
  syncLinkedPanStateFromParameters(false);
}

void VocalWidenerProcessor::releaseResources() {}

void VocalWidenerProcessor::reset() {
  voiceLeft.reset();
  voiceRight.reset();

  if (dryDelayLeft.isPrepared())
    dryDelayLeft.reset();
  if (dryDelayRight.isPrepared())
    dryDelayRight.reset();

  currentLeftCompDb = 0.0f;
  currentRightCompDb = 0.0f;
  leftDelayReadout.store(0.0f, std::memory_order_relaxed);
  rightDelayReadout.store(0.0f, std::memory_order_relaxed);
  leftPitchReadout.store(0.0f, std::memory_order_relaxed);
  rightPitchReadout.store(0.0f, std::memory_order_relaxed);
  earlierPathReadout.store(-1.0f, std::memory_order_relaxed);
  leftCompReadout.store(0.0f, std::memory_order_relaxed);
  rightCompReadout.store(0.0f, std::memory_order_relaxed);
}

void VocalWidenerProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                         juce::MidiBuffer &midiMessages) {
  juce::ignoreUnused(midiMessages);
  processAudioBlock(buffer, false);
}

void VocalWidenerProcessor::processBlockBypassed(
    juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages) {
  juce::ignoreUnused(midiMessages);
  processAudioBlock(buffer, true);
}

void VocalWidenerProcessor::processAudioBlock(juce::AudioBuffer<float> &buffer,
                                              bool forceBypassed) {
  juce::ScopedNoDenormals noDenormals;
  ProcessActivityGuard processGuard(activeProcessBlockCount);
  const auto processExit = juce::ScopeGuard([this] {
    lastProcessBlockEndMs.store(juce::Time::getMillisecondCounterHiRes(),
                                std::memory_order_relaxed);
  });

  // Fast bypass and Non-Stereo Layout safe exit
  if (!isStereoLayout)
    return;

  // Load parameters (Realtime safe)
  const float requestedOffsetMs =
      offsetTimeParam->load(std::memory_order_relaxed);
  const bool requestedCentered =
      centeredTimingParam->load(std::memory_order_relaxed) > 0.5f;
  const bool parameterBypassed =
      bypassParam->load(std::memory_order_relaxed) > 0.5f;
  const bool bypassed = forceBypassed || parameterBypassed;
  const float requestedPitchDiff =
      pitchDiffParam->load(std::memory_order_relaxed);
  const auto committedLatencyState = getCommittedLatencyState();
  if (committedLatencyState.latencySamples != activeLatencySamples) {
    activeLatencySamples = committedLatencyState.latencySamples;
    dryDelayLeft.setDelaySamples(activeLatencySamples);
    dryDelayRight.setDelaySamples(activeLatencySamples);
  }

  const int requestedLatencySamples =
      computeLatencySamples(requestedOffsetMs, requestedCentered,
                            requestedPitchDiff);
  float offsetMs = requestedOffsetMs;
  bool centered = requestedCentered;
  float pDiff = requestedPitchDiff;

  if (requestedLatencySamples != committedLatencyState.latencySamples) {
    queueLatencyUpdate(requestedOffsetMs, requestedCentered, requestedPitchDiff,
                       requestedLatencySamples);
    offsetMs = committedLatencyState.offsetMs;
    centered = committedLatencyState.centered;
    pDiff = committedLatencyState.driftAmountCents;
  }

  const bool pitchShiftActive = isPitchShiftActive(pDiff);
  float gainLinear = juce::Decibels::decibelsToGain(
      outputGainParam->load(std::memory_order_relaxed));
  const bool linkPanEnabled =
      linkPanParam->load(std::memory_order_relaxed) > 0.5f;

  float leftPanAmount =
      juce::jlimit(0.0f, 100.0f, leftPanParam->load(std::memory_order_relaxed));
  float rightPanAmount = juce::jlimit(
      0.0f, 100.0f, rightPanParam->load(std::memory_order_relaxed));
  if (linkPanEnabled) {
    const float linkedPanAmount = juce::jlimit(
        0.0f, 100.0f, linkedPanValue.load(std::memory_order_relaxed));
    leftPanAmount = linkedPanAmount;
    rightPanAmount = linkedPanAmount;
  }
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

  // Calculate DSP state per path.
  // Equal-delay mode now splits the actual voice delays instead of relying on
  // host compensation alone. The reported latency stays at the centre point so
  // bypass and host PDC remain aligned to the perceived middle of the pair.
  const float delayTLeftMs = centered ? (offsetMs * 0.5f) : 0.0f;
  const float delayTRightMs =
      centered ? (offsetMs * 1.5f) : offsetMs;

  const auto [leftDriftAmountCents, rightDriftAmountCents] =
      computeVoiceDriftAmounts(pDiff);
  const float sharedAdtLatencyMs = computeAdtSharedLatencyMs(pDiff);
  const float readoutReferenceMs =
      sharedAdtLatencyMs + (centered ? offsetMs : 0.0f);

  voiceLeft.configure(delayTLeftMs, sharedAdtLatencyMs, leftDriftAmountCents);
  voiceRight.configure(delayTRightMs, sharedAdtLatencyMs, rightDriftAmountCents);

  // Precedence Compensation
  bool haasEnable = haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f &&
                    linkPanEnabled;
  float haasAmtNorm =
      (haasCompAmtParam->load(std::memory_order_relaxed) *
       haasCompNewToOldPercentScale) /
      100.0f;

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
  float displayedLeftCompDb = 0.0f;
  float displayedRightCompDb = 0.0f;
  float displayedEarlierPath = -1.0f;

  for (int i = 0; i < numSamples; ++i) {
    const float sourceL = inputL[i];
    const float sourceR = inputR != nullptr ? inputR[i] : sourceL;
    const float dryL = dryDelayLeft.processSample(sourceL);
    const float dryR = dryDelayRight.processSample(sourceR);

    // Path A (left voice / input)
    float sA = voiceLeft.processSample(sourceL);

    // Path B (right voice / input)
    float sB = voiceRight.processSample(sourceR);

    const auto haasState = computeHaasCompensationState(
        voiceLeft.getCurrentDelayMs(), voiceRight.getCurrentDelayMs(),
        haasEnable, haasAmtNorm, panSeparationWeight,
        hasMeaningfulPanSeparation);
    currentLeftCompDb += (haasState.leftCompDb - currentLeftCompDb) * smoothCoeff;
    currentRightCompDb +=
        (haasState.rightCompDb - currentRightCompDb) * smoothCoeff;

    const float gPrecLeft = juce::Decibels::decibelsToGain(currentLeftCompDb);
    const float gPrecRight = juce::Decibels::decibelsToGain(currentRightCompDb);
    sA *= gPrecLeft;
    sB *= gPrecRight;

    displayedLeftCompDb = haasState.leftCompDb;
    displayedRightCompDb = haasState.rightCompDb;
    displayedEarlierPath = haasState.earlierPath;

    // Pan and Mix
    float outL = (sA * leftPL) + (sB * rightPL);
    float outR = (sA * leftPR) + (sB * rightPR);

    if (bypassed) {
      channelL[i] = dryL;
      channelR[i] = dryR;
    } else {
      // Output Gain
      channelL[i] = outL * gainLinear;
      channelR[i] = outR * gainLinear;
    }
  }

  leftDelayReadout.store(voiceLeft.getCurrentDelayMs() - readoutReferenceMs,
                         std::memory_order_relaxed);
  rightDelayReadout.store(voiceRight.getCurrentDelayMs() - readoutReferenceMs,
                          std::memory_order_relaxed);
  leftPitchReadout.store(
      pitchShiftActive ? voiceLeft.getCurrentDriftCents() : 0.0f,
      std::memory_order_relaxed);
  rightPitchReadout.store(
      pitchShiftActive ? voiceRight.getCurrentDriftCents() : 0.0f,
      std::memory_order_relaxed);
  earlierPathReadout.store(displayedEarlierPath, std::memory_order_relaxed);
  leftCompReadout.store(displayedLeftCompDb, std::memory_order_relaxed);
  rightCompReadout.store(displayedRightCompDb, std::memory_order_relaxed);
}

bool VocalWidenerProcessor::isPitchShiftActive(float pitchDiffCents) const {
  return std::abs(pitchDiffCents) >= 0.01f;
}

float VocalWidenerProcessor::computeAdtMaxExcursionMs(
    float driftAmountCents) const {
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
  return static_cast<float>(excursionSeconds * 1000.0);
}

int VocalWidenerProcessor::computeLatencySamples(float offsetMs, bool centered,
                                                 float driftAmountCents) const {
  const float totalLatencyMs =
      computeAdtSharedLatencyMs(driftAmountCents) +
      (centered ? offsetMs : 0.0f);
  return juce::roundToInt(
      (totalLatencyMs * static_cast<float>(currentSampleRate)) / 1000.0f);
}

float VocalWidenerProcessor::computeAdtSharedLatencyMs(float driftAmountCents) const {
  const float excursionMs = computeAdtMaxExcursionMs(driftAmountCents);
  if (excursionMs < 0.001f)
    return 0.0f;

  return excursionMs + 0.5f;
}

std::pair<float, float> VocalWidenerProcessor::computeVoiceDriftAmounts(
    float driftAmountCents) const {
  const float clampedAmount = juce::jmax(0.0f, driftAmountCents);
  return {clampedAmount, clampedAmount};
}

float VocalWidenerProcessor::getReportedLatencyMs() const {
  if (currentSampleRate <= 0.0)
    return 0.0f;

  const int latencySamples = getCommittedLatencyState().latencySamples;

  return (static_cast<float>(latencySamples) * 1000.0f) /
         static_cast<float>(currentSampleRate);
}

double VocalWidenerProcessor::getTailLengthSeconds() const {
  const double maxTailMs =
      static_cast<double>(maxCenteredRightDelayMs) +
      static_cast<double>(computeAdtSharedLatencyMs(maxPitchDiffCents)) +
      static_cast<double>(computeAdtMaxExcursionMs(maxPitchDiffCents));
  return maxTailMs /
         1000.0;
}

juce::AudioProcessorParameter* VocalWidenerProcessor::getBypassParameter() const {
  return apvts.getParameter("bypass");
}

void VocalWidenerProcessor::queueLatencyUpdate(float offsetMs, bool centered,
                                               float driftAmountCents,
                                               int latencySamples) {
  pendingLatencyOffsetMs.store(offsetMs, std::memory_order_relaxed);
  pendingLatencyCentered.store(centered ? 1 : 0, std::memory_order_relaxed);
  pendingLatencyDriftAmountCents.store(driftAmountCents,
                                       std::memory_order_relaxed);
  pendingLatencySamples.store(latencySamples, std::memory_order_relaxed);
  pendingLatencyUpdate.store(1, std::memory_order_relaxed);
  triggerAsyncUpdate();
}

void VocalWidenerProcessor::handleLatencyParameterChanged(
    const juce::String &parameterID, float newValue) {
  const float offsetMs =
      parameterID == "offsetTime"
          ? newValue
          : (offsetTimeParam != nullptr
                 ? offsetTimeParam->load(std::memory_order_relaxed)
                 : 0.0f);
  const bool centered =
      parameterID == "centeredTiming"
          ? (newValue > 0.5f)
          : (centeredTimingParam != nullptr &&
             centeredTimingParam->load(std::memory_order_relaxed) > 0.5f);
  const float driftAmountCents =
      parameterID == "pitchDiff"
          ? newValue
          : (pitchDiffParam != nullptr
                 ? pitchDiffParam->load(std::memory_order_relaxed)
                 : 0.0f);
  const int latencySamples =
      computeLatencySamples(offsetMs, centered, driftAmountCents);
  const auto committedLatencyState = getCommittedLatencyState();
  const bool matchesCommitted =
      committedLatencyState.latencySamples == latencySamples &&
      std::abs(committedLatencyState.offsetMs - offsetMs) < 1.0e-4f &&
      committedLatencyState.centered == centered &&
      std::abs(committedLatencyState.driftAmountCents - driftAmountCents) <
          1.0e-4f;
  const bool hasPendingLatencyUpdate =
      pendingLatencyUpdate.load(std::memory_order_relaxed) != 0;

  if (matchesCommitted && !hasPendingLatencyUpdate)
    return;

  if (canSynchronouslyCommitLatencyChange()) {
    const juce::ScopedLock callbackLock(getCallbackLock());

    if (canSynchronouslyCommitLatencyChange()) {
      syncLatencyStateFromParameters(true);
      return;
    }
  }

  queueLatencyUpdate(offsetMs, centered, driftAmountCents, latencySamples);
}

bool VocalWidenerProcessor::canSynchronouslyCommitLatencyChange() const {
  if (activeProcessBlockCount.load(std::memory_order_acquire) != 0)
    return false;

  const double lastBlockEndMs =
      lastProcessBlockEndMs.load(std::memory_order_relaxed);

  if (lastBlockEndMs <= 0.0)
    return true;

  return (juce::Time::getMillisecondCounterHiRes() - lastBlockEndMs) >=
         latencyCommitIdleWindowMs;
}

void VocalWidenerProcessor::syncLatencyStateFromParameters(
    bool updateHostLatencyNow) {
  const float offsetMs =
      offsetTimeParam != nullptr
          ? offsetTimeParam->load(std::memory_order_relaxed)
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

  storeCommittedLatencyState(offsetMs, centered, driftAmountCents,
                             latencySamples);
  activeLatencySamples = latencySamples;
  pendingLatencyUpdate.store(0, std::memory_order_relaxed);
  pendingLatencySamples.store(latencySamples, std::memory_order_relaxed);

  if (dryDelayLeft.isPrepared())
    dryDelayLeft.setDelaySamples(latencySamples);
  if (dryDelayRight.isPrepared())
    dryDelayRight.setDelaySamples(latencySamples);

  if (updateHostLatencyNow)
    setLatencySamples(latencySamples);
}

VocalWidenerProcessor::LatencyStateSnapshot
VocalWidenerProcessor::getCommittedLatencyState() const {
  LatencyStateSnapshot snapshot;

  for (;;) {
    const auto versionStart =
        committedLatencyStateVersion.load(std::memory_order_acquire);
    if ((versionStart & 1u) != 0u)
      continue;

    snapshot.offsetMs = committedOffsetMs.load(std::memory_order_relaxed);
    snapshot.centered =
        committedCentered.load(std::memory_order_relaxed) > 0;
    snapshot.driftAmountCents =
        committedDriftAmountCents.load(std::memory_order_relaxed);
    snapshot.latencySamples =
        committedLatencySamples.load(std::memory_order_relaxed);

    const auto versionEnd =
        committedLatencyStateVersion.load(std::memory_order_acquire);
    if (versionStart == versionEnd)
      return snapshot;
  }
}

void VocalWidenerProcessor::storeCommittedLatencyState(float offsetMs,
                                                       bool centered,
                                                       float driftAmountCents,
                                                       int latencySamples) {
  committedLatencyStateVersion.fetch_add(1, std::memory_order_acq_rel);
  committedOffsetMs.store(offsetMs, std::memory_order_relaxed);
  committedCentered.store(centered ? 1 : 0, std::memory_order_relaxed);
  committedDriftAmountCents.store(driftAmountCents,
                                  std::memory_order_relaxed);
  committedLatencySamples.store(latencySamples, std::memory_order_relaxed);
  committedLatencyStateVersion.fetch_add(1, std::memory_order_release);
}

void VocalWidenerProcessor::syncLinkedPanStateFromParameters(
    bool scheduleMirror) {
  const float leftValue =
      leftPanParam != nullptr
          ? leftPanParam->load(std::memory_order_relaxed)
          : 100.0f;
  const bool linked =
      linkPanParam != nullptr &&
      linkPanParam->load(std::memory_order_relaxed) > 0.5f;

  linkedPanValue.store(leftValue, std::memory_order_relaxed);

  if (!linked) {
    pendingPanMirrorTarget.store(noPanMirrorPending, std::memory_order_relaxed);
    return;
  }

  if (scheduleMirror)
    this->scheduleLinkedPanMirror(mirrorRightPan, leftValue);
}

void VocalWidenerProcessor::scheduleLinkedPanMirror(PanMirrorTarget target,
                                                    float value) {
  pendingPanMirrorValue.store(value, std::memory_order_relaxed);
  pendingPanMirrorTarget.store(target, std::memory_order_relaxed);
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
  if (pendingLatencyUpdate.exchange(0, std::memory_order_relaxed) != 0) {
    const int latencySamples =
        pendingLatencySamples.load(std::memory_order_relaxed);
    const float offsetMs =
        pendingLatencyOffsetMs.load(std::memory_order_relaxed);
    const bool centered =
        pendingLatencyCentered.load(std::memory_order_relaxed) > 0;
    const float driftAmountCents =
        pendingLatencyDriftAmountCents.load(std::memory_order_relaxed);

    setLatencySamples(latencySamples);
    storeCommittedLatencyState(offsetMs, centered, driftAmountCents,
                               latencySamples);
  }

  const auto target = static_cast<PanMirrorTarget>(
      pendingPanMirrorTarget.exchange(noPanMirrorPending,
                                      std::memory_order_relaxed));
  if (target == noPanMirrorPending)
    return;

  const bool linked =
      linkPanParam != nullptr &&
      linkPanParam->load(std::memory_order_relaxed) > 0.5f;
  if (!linked)
    return;

  auto *parameter =
      apvts.getParameter(target == mirrorLeftPan ? "leftPan" : "rightPan");
  if (parameter == nullptr)
    return;

  const float mirroredValue = juce::jlimit(
      0.0f, 100.0f, pendingPanMirrorValue.load(std::memory_order_relaxed));
  const float normalizedValue = parameter->convertTo0to1(mirroredValue);
  if (std::abs(parameter->getValue() - normalizedValue) < 1.0e-6f)
    return;

  panMirrorWriteTarget.store(target, std::memory_order_relaxed);
  panMirrorWriteValue.store(mirroredValue, std::memory_order_relaxed);
  parameter->setValueNotifyingHost(normalizedValue);
  panMirrorWriteTarget.store(noPanMirrorPending, std::memory_order_relaxed);
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
      syncLatencyStateFromParameters(true);
      syncLinkedPanStateFromParameters(true);
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
