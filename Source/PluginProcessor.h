#pragma once

#include <JuceHeader.h>
#include "PadData.h"
#include "PadVoice.h"
#include <array>

class MPC16SamplerAudioProcessor : public juce::AudioProcessor
{
public:
    MPC16SamplerAudioProcessor();
    ~MPC16SamplerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Sampler Public API for UI ---
    mpc16::PadParams& getPad(int index) noexcept { return pads[static_cast<size_t>(juce::jlimit(0, mpc16::kNumPads - 1, index))]; }
    const mpc16::PadParams& getPad(int index) const noexcept { return pads[static_cast<size_t>(juce::jlimit(0, mpc16::kNumPads - 1, index))]; }

    int getSelectedPadIndex() const noexcept { return selectedPadIndex.load(std::memory_order_relaxed); }
    void setSelectedPadIndex(int idx) noexcept { selectedPadIndex.store(juce::jlimit(0, mpc16::kNumPads - 1, idx), std::memory_order_relaxed); }

    bool loadSampleForPad(int padIndex, const juce::File& audioFile);
    void loadFactoryKit();
    void resetPadToFactory(int padIndex);

    // Trigger/Release from UI thread (lock-free FIFO)
    void triggerPadFromUI(int padIndex, float velocityNormalized);
    void releasePadFromUI(int padIndex);

    // Global MPC performance modes & master bus controls
    std::atomic<bool> fullLevelMode { false };
    std::atomic<bool> sixteenLevelsPitchMode { false };
    std::atomic<int> globalVintageMode { static_cast<int>(mpc16::VintageMode::Clean) };
    std::atomic<float> masterGainDb { 0.0f };
    std::atomic<float> outputPeakL { 0.0f };
    std::atomic<float> outputPeakR { 0.0f };

    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

private:
    struct PendingPadEvent
    {
        int padIndex = 0;
        float velocity = 0.0f;
        bool isNoteOn = true;
    };

    void triggerNoteEvent(int padIdx, float velocityNorm);
    void releaseNoteEvent(int padIdx);
    int findPadForMidiNote(int midiNote) const noexcept;

    std::array<mpc16::PadParams, mpc16::kNumPads> pads;
    std::array<mpc16::PadVoice, mpc16::kMaxVoices> voices;
    int nextVoiceStealIndex = 0;

    std::atomic<int> selectedPadIndex { 0 };
    double currentSampleRate = 44100.0;

    // Lock-free UI -> Audio trigger ring buffer
    static constexpr int kFifoCapacity = 64;
    juce::AbstractFifo uiEventFifo { kFifoCapacity };
    std::array<PendingPadEvent, kFifoCapacity> uiEventBuffer;

    // Master bus vintage sampler decimator state
    float masterDecimateCounter = 0.0f;
    float masterHeldL = 0.0f;
    float masterHeldR = 0.0f;

    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MPC16SamplerAudioProcessor)
};
