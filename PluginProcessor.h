#pragma once

#include <JuceHeader.h>

//==============================================================================
namespace dsp_utils 
{
    // A classic dual-window time-domain micro pitch shifter
    class MicroPitchShifter
    {
    public:
        void prepare(double sampleRate, float windowMs = 30.0f) {
            fs = sampleRate;
            windowSamples = (windowMs * fs) / 1000.0;
            buffer.setSize(1, static_cast<int>(windowSamples * 2.0) + 100);
            buffer.clear();
            writePos = 0;
            phase = 0.0;
            deltaPhase = 0.0;
        }

        void setPitchOffsetCents(float cents) {
            if (std::abs(cents) < 0.01f) {
                deltaPhase = 0.0;
                // Zero cents should always land on the neutral half-window read.
                phase = 0.0;
            } else {
                double speedRatio = std::pow(2.0, cents / 1200.0);
                deltaPhase = (1.0 - speedRatio) / windowSamples;
            }
        }

        float processSample(float in) {
            if (windowSamples <= 0.0) return in;

            auto* writePtr = buffer.getWritePointer(0);
            const int bufferSize = buffer.getNumSamples();
            writePtr[writePos] = in;

            auto readInterp = [&](double p) {
                double delay = p * windowSamples;
                double readPos = static_cast<double>(writePos) - delay;
                if (readPos < 0.0) readPos += bufferSize;

                int idx1 = static_cast<int>(readPos);
                int idx2 = (idx1 + 1) % bufferSize;
                double frac = readPos - idx1;
                return writePtr[idx1] * (1.0f - static_cast<float>(frac)) + writePtr[idx2] * static_cast<float>(frac);
            };

            auto getTriangleGain = [](double p) {
                return static_cast<float>(p < 0.5 ? 2.0 * p : 2.0 * (1.0 - p));
            };

            if (std::abs(deltaPhase) < 1.0e-9) {
                const float out = readInterp(0.5);
                writePos = (writePos + 1) % bufferSize;
                return out;
            }

            double phase2 = std::fmod(phase + 0.5, 1.0);
            
            float s1 = readInterp(phase);
            float s2 = readInterp(phase2);
            float g1 = getTriangleGain(phase);
            float g2 = getTriangleGain(phase2);

            float out = (s1 * g1) + (s2 * g2);

            writePos = (writePos + 1) % bufferSize;
            phase += deltaPhase;
            if (phase >= 1.0) phase -= 1.0;
            else if (phase < 0.0) phase += 1.0;

            return out;
        }

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        double fs = 44100.0;
        double windowSamples = 0.0;
        double phase = 0.0, deltaPhase = 0.0;
    };

    // Circular fraction delay
    class SmoothedDelay
    {
    public:
        void prepare(double sampleRate, float maxDelayMs = 100.0f) {
            fs = sampleRate;
            buffer.setSize(1, static_cast<int>((maxDelayMs * fs) / 1000.0) + 10);
            buffer.clear();
            writePos = 0;
            currentDelayMs = 0.0f;
            targetDelayMs = 0.0f;
        }

        void setTargetDelayMs(float ms) { targetDelayMs = ms; }

        float processSample(float in) {
            auto* writePtr = buffer.getWritePointer(0);
            const int bufferSize = buffer.getNumSamples();

            // 1-pole smoother to prevent zipper noise on delay changes
            currentDelayMs += (targetDelayMs - currentDelayMs) * 0.005f;

            float delaySamples = (currentDelayMs * static_cast<float>(fs)) / 1000.0f;
            writePtr[writePos] = in;

            float readPos = static_cast<float>(writePos) - delaySamples;
            if (readPos < 0.0f) readPos += static_cast<float>(bufferSize);

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
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vocal Widener"; }
    static juce::String getVersionTag() { return "v0.1.0-alpha"; }
    static juce::URL getReleasesPageUrl() { return juce::URL("https://github.com/Pachii/topaz-pan/releases"); }
    static juce::URL getLatestReleaseApiUrl() { return juce::URL("https://api.github.com/repos/Pachii/topaz-pan/releases/latest"); }
    float getReportedLatencyMs() const;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    std::atomic<float>* equalPitchShiftParam = nullptr;
    std::atomic<float>* pitchDiffParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* linkPanParam = nullptr;
    std::atomic<float>* flipPanParam = nullptr;
    std::atomic<float>* haasCompEnableParam = nullptr;
    std::atomic<float>* haasCompAmtParam = nullptr;

    std::atomic<bool> isLinkingPan {false};

    // Readouts for UI
    std::atomic<float> leftDelayReadout {0.0f};
    std::atomic<float> rightDelayReadout {0.0f};
    std::atomic<float> leftPitchReadout {0.0f};
    std::atomic<float> rightPitchReadout {0.0f};
    
    std::atomic<float> earlierPathReadout {-1.0f}; // 0 = Left, 1 = Right, -1 = Tie
    std::atomic<float> leftCompReadout {0.0f};
    std::atomic<float> rightCompReadout {0.0f};

    // State
    bool isStereoLayout = false;

private:
    static constexpr float maxOffsetMs = 50.0f;
    static constexpr float pitchWindowMs = 30.0f;
    static constexpr float pitchLatencyMs = pitchWindowMs * 0.5f;
    static constexpr float maxReportedLatencyMs = pitchLatencyMs + (maxOffsetMs * 0.5f);

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

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        int delaySamples = 0;
    };

    void handleAsyncUpdate() override;
    int computeLatencySamples(float offsetMs, bool centered) const;
    void queueLatencyUpdate(int latencySamples);

    float currentLeftCompDb = 0.0f;
    float currentRightCompDb = 0.0f;
    double currentSampleRate = 44100.0;
    std::atomic<int> pendingLatencySamples {0};
    int activeLatencySamples = 0;

    dsp_utils::SmoothedDelay delayLeft, delayRight;
    dsp_utils::MicroPitchShifter pitchLeft, pitchRight;
    IntegerDelay dryDelayLeft, dryDelayRight;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalWidenerProcessor)
};
