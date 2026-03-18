#pragma once

#include <JuceHeader.h>

//==============================================================================
namespace dsp_utils 
{
    class AdtDriftVoice
    {
    public:
        void prepare(double sampleRate, float maxDelayMs, int randomSeed) {
            fs = sampleRate;
            buffer.setSize(1, static_cast<int>((maxDelayMs * fs) / 1000.0) + 16);
            random.setSeed(randomSeed);
            reset();
        }

        void reset() {
            buffer.clear();
            writePos = 0;
            centerDelaySamples = 0.0;
            currentDelaySamples = 0.0;
            segmentStartDelaySamples = 0.0;
            segmentTargetDelaySamples = 0.0;
            segmentLengthSamples = 0;
            segmentProgressSamples = 0;
            maxExcursionSamples = 0.0;
            maxDriftCents = 0.0f;
            currentDriftCents = 0.0f;
            crossfadeFromDelaySamples = 0.0;
            crossfadeSamplesRemaining = 0;
            crossfadeSamplesTotal = 0;
            hasDelayState = false;
        }

        void configure(float baseDelayMs, float sharedDelayBaseMs,
                       float maxDriftCentsToUse) {
            const bool wasDriftActive = maxDriftCents >= 0.01f;
            const double previousDelaySamples = currentDelaySamples;
            centerDelaySamples =
                ((baseDelayMs + sharedDelayBaseMs) * fs) / 1000.0;
            maxDriftCents = juce::jmax(0.0f, maxDriftCentsToUse);

            if (maxDriftCents < 0.01f) {
                if (wasDriftActive && hasDelayState &&
                    std::abs(previousDelaySamples - centerDelaySamples) > 0.001)
                    startModeCrossfade(previousDelaySamples);

                currentDelaySamples = centerDelaySamples;
                segmentStartDelaySamples = centerDelaySamples;
                segmentTargetDelaySamples = centerDelaySamples;
                segmentLengthSamples = 0;
                segmentProgressSamples = 0;
                currentDriftCents = 0.0f;
                maxExcursionSamples = 0.0;
                hasDelayState = true;
                return;
            }

            const double maxSpeedDelta =
                std::abs(1.0 - std::pow(2.0, maxDriftCents / 1200.0));
            maxExcursionSamples =
                juce::jmax(1.0, maxSpeedDelta * (maxSegmentSeconds * fs) /
                                    smoothstepPeakSlope);

            const double minDelay = centerDelaySamples - maxExcursionSamples;
            const double maxDelay = centerDelaySamples + maxExcursionSamples;

            const bool driftJustEnabled = !wasDriftActive;

            if (!hasDelayState) {
                currentDelaySamples = centerDelaySamples;
                segmentStartDelaySamples = centerDelaySamples;
                segmentTargetDelaySamples = centerDelaySamples;
                hasDelayState = true;
            } else if (driftJustEnabled) {
                if (std::abs(previousDelaySamples - centerDelaySamples) > 0.001)
                    startModeCrossfade(previousDelaySamples);

                // Enter ADT from the latency-compensated center point so the
                // wet path matches the committed shared latency before the
                // first randomized drift segment begins.
                currentDelaySamples = centerDelaySamples;
                segmentStartDelaySamples = centerDelaySamples;
                segmentTargetDelaySamples = centerDelaySamples;
                segmentLengthSamples = 0;
                segmentProgressSamples = 0;
            }

            currentDelaySamples = juce::jlimit(minDelay, maxDelay,
                                               currentDelaySamples);
            segmentStartDelaySamples = juce::jlimit(minDelay, maxDelay,
                                                    segmentStartDelaySamples);
            segmentTargetDelaySamples = juce::jlimit(minDelay, maxDelay,
                                                     segmentTargetDelaySamples);

            if (segmentLengthSamples <= 0) {
                startNewSegment();
                return;
            }
        }

        float processSample(float in) {
            auto* writePtr = buffer.getWritePointer(0);
            const int bufferSize = buffer.getNumSamples();
            writePtr[writePos] = in;

            if (maxDriftCents < 0.01f && centerDelaySamples <= 0.001 &&
                crossfadeSamplesRemaining <= 0) {
                writePos = (writePos + 1) % bufferSize;
                currentDriftCents = 0.0f;
                return in;
            }

            if (maxDriftCents < 0.01f) {
                currentDelaySamples = centerDelaySamples;
                currentDriftCents = 0.0f;
            } else {
                if (segmentLengthSamples <= 0 ||
                    segmentProgressSamples >= segmentLengthSamples)
                    startNewSegment();

                const double t = juce::jlimit(
                    0.0, 1.0,
                    static_cast<double>(segmentProgressSamples) /
                        static_cast<double>(segmentLengthSamples));
                const double eased = (t * t) * (3.0 - (2.0 * t));
                const double easedDerivative =
                    (6.0 * t * (1.0 - t)) /
                    static_cast<double>(segmentLengthSamples);
                const double delaySpan =
                    segmentTargetDelaySamples - segmentStartDelaySamples;

                currentDelaySamples =
                    segmentStartDelaySamples + (delaySpan * eased);

                const double delayDeltaPerSample =
                    delaySpan * easedDerivative;
                const double speedRatio =
                    juce::jmax(1.0e-6, 1.0 - delayDeltaPerSample);
                currentDriftCents =
                    static_cast<float>(1200.0 * std::log2(speedRatio));

                ++segmentProgressSamples;
            }

            const float primaryOut =
                readDelayedSample(writePtr, bufferSize, currentDelaySamples);
            float out = primaryOut;

            if (crossfadeSamplesRemaining > 0) {
                const float previousOut = readDelayedSample(
                    writePtr, bufferSize, crossfadeFromDelaySamples);
                const float progress =
                    1.0f - (static_cast<float>(crossfadeSamplesRemaining) /
                            static_cast<float>(crossfadeSamplesTotal));
                const float fadeOut =
                    std::cos(progress * juce::MathConstants<float>::halfPi);
                const float fadeIn =
                    std::sin(progress * juce::MathConstants<float>::halfPi);
                out = (previousOut * fadeOut) + (primaryOut * fadeIn);
                --crossfadeSamplesRemaining;
            }

            writePos = (writePos + 1) % bufferSize;
            return out;
        }

        float getCurrentDriftCents() const { return currentDriftCents; }
        float getCurrentDelayMs() const {
            return fs > 0.0
                       ? static_cast<float>((currentDelaySamples * 1000.0) / fs)
                       : 0.0f;
        }

        float getSharedLatencyMs() const {
            return static_cast<float>((maxExcursionSamples * 1000.0) / fs);
        }

    private:
        float readDelayedSample(const float* writePtr, int bufferSize,
                                double delaySamples) const {
            double readPos = static_cast<double>(writePos) - delaySamples;
            while (readPos < 0.0)
                readPos += static_cast<double>(bufferSize);
            while (readPos >= static_cast<double>(bufferSize))
                readPos -= static_cast<double>(bufferSize);

            const int idx1 = static_cast<int>(readPos);
            const float frac = static_cast<float>(readPos - idx1);
            const auto sampleAt = [&](int index) {
                while (index < 0)
                    index += bufferSize;
                while (index >= bufferSize)
                    index -= bufferSize;
                return writePtr[index];
            };

            const float y0 = sampleAt(idx1 - 1);
            const float y1 = sampleAt(idx1);
            const float y2 = sampleAt(idx1 + 1);
            const float y3 = sampleAt(idx1 + 2);

            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - (2.5f * y1) + (2.0f * y2) - (0.5f * y3);
            const float c3 = (0.5f * (y3 - y0)) + (1.5f * (y1 - y2));

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }

        void startModeCrossfade(double fromDelaySamples) {
            if (fs <= 0.0)
                return;

            crossfadeFromDelaySamples = juce::jmax(0.0, fromDelaySamples);
            crossfadeSamplesTotal =
                juce::jmax(1, juce::roundToInt((modeCrossfadeMs * fs) / 1000.0));
            crossfadeSamplesRemaining = crossfadeSamplesTotal;
        }
        void startNewSegment() {
            if (maxDriftCents < 0.01f || fs <= 0.0) {
                segmentLengthSamples = 0;
                segmentProgressSamples = 0;
                segmentStartDelaySamples = centerDelaySamples;
                segmentTargetDelaySamples = centerDelaySamples;
                currentDelaySamples = centerDelaySamples;
                currentDriftCents = 0.0f;
                return;
            }

            const int minSamples = juce::jmax(
                1, juce::roundToInt(minSegmentSeconds * static_cast<float>(fs)));
            const int maxSamples = juce::jmax(
                minSamples + 1,
                juce::roundToInt(maxSegmentSeconds * static_cast<float>(fs)));

            segmentLengthSamples = minSamples +
                                   random.nextInt(maxSamples - minSamples + 1);
            segmentProgressSamples = 0;
            segmentStartDelaySamples = currentDelaySamples;

            const double maxSpeedDelta =
                std::abs(1.0 - std::pow(2.0, maxDriftCents / 1200.0));
            const double maxStepSamples =
                (maxSpeedDelta * static_cast<double>(segmentLengthSamples)) /
                smoothstepPeakSlope;
            const double minDelay = centerDelaySamples - maxExcursionSamples;
            const double maxDelay = centerDelaySamples + maxExcursionSamples;
            const double unclampedTarget =
                currentDelaySamples +
                juce::jmap(random.nextFloat(), -static_cast<float>(maxStepSamples),
                           static_cast<float>(maxStepSamples));

            segmentTargetDelaySamples =
                juce::jlimit(minDelay, maxDelay, unclampedTarget);
        }

        static constexpr float minSegmentSeconds = 0.35f;
        static constexpr float maxSegmentSeconds = 1.25f;
        static constexpr float modeCrossfadeMs = 5.0f;
        static constexpr double smoothstepPeakSlope = 1.5;

        juce::AudioBuffer<float> buffer;
        juce::Random random;
        int writePos = 0;
        double fs = 44100.0;
        double centerDelaySamples = 0.0;
        double currentDelaySamples = 0.0;
        double segmentStartDelaySamples = 0.0;
        double segmentTargetDelaySamples = 0.0;
        double maxExcursionSamples = 0.0;
        int segmentLengthSamples = 0;
        int segmentProgressSamples = 0;
        float maxDriftCents = 0.0f;
        float currentDriftCents = 0.0f;
        double crossfadeFromDelaySamples = 0.0;
        int crossfadeSamplesRemaining = 0;
        int crossfadeSamplesTotal = 0;
        bool hasDelayState = false;
    };

    // Circular fraction delay
    class SmoothedDelay
    {
    public:
        void prepare(double sampleRate, float maxDelayMs = 100.0f) {
            fs = sampleRate;
            maxDelayMsPrepared = juce::jmax(0.0f, maxDelayMs);
            buffer.setSize(1, static_cast<int>((maxDelayMs * fs) / 1000.0) + 10);
            buffer.clear();
            writePos = 0;
            currentDelayMs = 0.0f;
            targetDelayMs = 0.0f;
        }

        void setTargetDelayMs(float ms) {
            targetDelayMs = juce::jlimit(0.0f, maxDelayMsPrepared, ms);
        }

        float processSample(float in) {
            auto* writePtr = buffer.getWritePointer(0);
            const int bufferSize = buffer.getNumSamples();

            // 1-pole smoother to prevent zipper noise on delay changes
            currentDelayMs += (targetDelayMs - currentDelayMs) * 0.005f;

            float delaySamples = (currentDelayMs * static_cast<float>(fs)) / 1000.0f;
            writePtr[writePos] = in;

            float readPos = static_cast<float>(writePos) - delaySamples;
            while (readPos < 0.0f)
                readPos += static_cast<float>(bufferSize);
            while (readPos >= static_cast<float>(bufferSize))
                readPos -= static_cast<float>(bufferSize);

            int idx1 = static_cast<int>(readPos);
            int idx2 = (idx1 + 1) % bufferSize;
            float frac = readPos - static_cast<float>(idx1);

            float out = writePtr[idx1] * (1.0f - frac) + writePtr[idx2] * frac;

            writePos = (writePos + 1) % bufferSize;
            return out;
        }

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        double fs = 44100.0;
        float maxDelayMsPrepared = 0.0f;
        float currentDelayMs = 0.0f, targetDelayMs = 0.0f;
    };
} // namespace dsp_utils

class VocalWidenerProcessor : public juce::AudioProcessor,
                              public juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater
{
public:
    VocalWidenerProcessor();
    ~VocalWidenerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "topaz pan"; }
    static juce::String getVersionTag() { return "v0.3.0-alpha"; }
    static juce::URL getReleasesPageUrl() { return juce::URL("https://github.com/Pachii/topaz-pan/releases"); }
    static juce::URL getLatestReleaseApiUrl() { return juce::URL("https://api.github.com/repos/Pachii/topaz-pan/releases/latest"); }
    static juce::String normaliseLanguageCode(juce::String languageCode);
    float getReportedLatencyMs() const;
    juce::String getLanguageCode() const;
    void setLanguageCode(const juce::String& languageCode);
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState apvts;

    // Fast-access cached parameters for realtime safety
    std::atomic<float>* offsetTimeParam = nullptr;
    std::atomic<float>* leftPanParam = nullptr;
    std::atomic<float>* rightPanParam = nullptr;
    std::atomic<float>* centeredTimingParam = nullptr;
    std::atomic<float>* pitchDiffParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* linkPanParam = nullptr;
    std::atomic<float>* flipPanParam = nullptr;
    std::atomic<float>* haasCompEnableParam = nullptr;
    std::atomic<float>* haasCompAmtParam = nullptr;

    // Readouts for UI
    std::atomic<float> leftDelayReadout {0.0f};
    std::atomic<float> rightDelayReadout {0.0f};
    std::atomic<float> leftPitchReadout {0.0f};
    std::atomic<float> rightPitchReadout {0.0f};
    
    std::atomic<float> earlierPathReadout {-1.0f}; // 0 = Left voice, 1 = Right voice, -1 = Tie
    std::atomic<float> leftCompReadout {0.0f};
    std::atomic<float> rightCompReadout {0.0f};

    // State
    bool isStereoLayout = false;

private:
    static constexpr float maxOffsetMs = 50.0f;
    static constexpr float maxCenteredRightDelayMs = maxOffsetMs * 1.5f;
    static constexpr float maxPitchDiffCents = 20.0f;
    static constexpr float maxLatencyGuardMs = 2.0f;

    enum PanMirrorTarget
    {
        noPanMirrorPending = 0,
        mirrorLeftPan = 1,
        mirrorRightPan = 2
    };

    class IntegerDelay
    {
    public:
        void prepare(int maximumDelaySamples)
        {
            buffer.setSize(1, maximumDelaySamples + 1);
            buffer.clear();
            writePos = 0;
            delaySamples = 0;
        }

        void setDelaySamples(int newDelaySamples)
        {
            jassert(buffer.getNumSamples() > 0);
            delaySamples = juce::jlimit(0, buffer.getNumSamples() - 1, newDelaySamples);
        }

        void reset()
        {
            buffer.clear();
            writePos = 0;
        }

        float processSample(float in)
        {
            auto* writePtr = buffer.getWritePointer(0);
            const int bufferSize = buffer.getNumSamples();

            writePtr[writePos] = in;

            int readPos = writePos - delaySamples;
            if (readPos < 0)
                readPos += bufferSize;

            float out = writePtr[readPos];
            writePos = (writePos + 1) % bufferSize;
            return out;
        }

        bool isPrepared() const { return buffer.getNumSamples() > 0; }

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        int delaySamples = 0;
    };

    struct LatencyStateSnapshot
    {
        float offsetMs = 0.0f;
        bool centered = false;
        float driftAmountCents = 0.0f;
        int latencySamples = 0;
    };

    void handleAsyncUpdate() override;
    void processAudioBlock(juce::AudioBuffer<float>& buffer, bool forceBypassed);
    void handleLatencyParameterChanged(const juce::String& parameterID,
                                       float newValue);
    int computeLatencySamples(float offsetMs, bool centered,
                              float driftAmountCents) const;
    void queueLatencyUpdate(float offsetMs, bool centered,
                            float driftAmountCents, int latencySamples);
    bool canSynchronouslyCommitLatencyChange() const;
    void syncLatencyStateFromParameters(bool updateHostLatencyNow);
    void syncLinkedPanStateFromParameters(bool scheduleMirror);
    void scheduleLinkedPanMirror(PanMirrorTarget target, float value);
    bool isPitchShiftActive(float pitchDiffCents) const;
    float computeAdtMaxExcursionMs(float driftAmountCents) const;
    float computeAdtSharedLatencyMs(float driftAmountCents) const;
    std::pair<float, float> computeVoiceDriftAmounts(float driftAmountCents) const;
    LatencyStateSnapshot getCommittedLatencyState() const;
    void storeCommittedLatencyState(float offsetMs, bool centered,
                                    float driftAmountCents, int latencySamples);

    float currentLeftCompDb = 0.0f;
    float currentRightCompDb = 0.0f;
    double currentSampleRate = 44100.0;
    int activeLatencySamples = 0;
    juce::String languageCode {"en"};
    std::atomic<float> linkedPanValue {100.0f};
    std::atomic<int> pendingPanMirrorTarget {noPanMirrorPending};
    std::atomic<float> pendingPanMirrorValue {100.0f};
    std::atomic<int> panMirrorWriteTarget {noPanMirrorPending};
    std::atomic<float> panMirrorWriteValue {0.0f};
    std::atomic<uint32_t> committedLatencyStateVersion {0};
    std::atomic<float> committedOffsetMs {0.0f};
    std::atomic<int> committedCentered {0};
    std::atomic<float> committedDriftAmountCents {0.0f};
    std::atomic<int> committedLatencySamples {0};
    std::atomic<int> pendingLatencyUpdate {0};
    std::atomic<float> pendingLatencyOffsetMs {0.0f};
    std::atomic<int> pendingLatencyCentered {0};
    std::atomic<float> pendingLatencyDriftAmountCents {0.0f};
    std::atomic<int> pendingLatencySamples {0};
    std::atomic<int> activeProcessBlockCount {0};
    std::atomic<double> lastProcessBlockEndMs {0.0};

    dsp_utils::AdtDriftVoice voiceLeft, voiceRight;
    IntegerDelay dryDelayLeft, dryDelayRight;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalWidenerProcessor)
};
