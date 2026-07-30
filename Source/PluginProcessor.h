#pragma once

#include <JuceHeader.h>

#include "LayerParameters.h"
#include "StackSamplerCore.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class StackSamplerAudioProcessor final : public juce::AudioProcessor,
                                         public juce::ChangeBroadcaster,
                                         private juce::Timer
{
public:
    struct LayerDisplayState
    {
        int bank = 0;
        juce::String name;
        juce::String filePath;
        juce::String status;
        stacksampler::QuickMode quickMode = stacksampler::QuickMode::none;
        bool hasSample = false;
    };

    StackSamplerAudioProcessor();
    ~StackSamplerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    std::vector<int> getActiveLayers() const;
    LayerDisplayState getLayerDisplayState (int bank) const;
    std::shared_ptr<const stacksampler::SampleData> getSampleData (int bank) const;

    bool addLayer();
    void removeLayer (int bank);
    bool loadSample (int bank, const juce::File& file, juce::String* errorMessage = nullptr);
    void applyQuickMode (int bank, stacksampler::QuickMode mode);
    void randomizeLayer (int bank);

    bool isHumanizeEnabled() const;
    void setHumanizeEnabled (bool enabled);

    juce::AudioProcessorValueTreeState parameters;

private:
    struct LayerMetadata
    {
        std::atomic<bool> active { false };
        juce::String name;
        juce::String filePath;
        juce::String status;
        stacksampler::QuickMode quickMode = stacksampler::QuickMode::none;
    };

    static constexpr int stateVersion = 1;

    void renderSegment (juce::AudioBuffer<float>& output,
                        int startSample,
                        int numSamples,
                        bool anyLayerSoloed,
                        const std::array<stacksampler::LayerRenderParameters,
                                         stacksampler::kMaxLayers>& snapshots);
    void triggerNote (int midiNote,
                      float velocity,
                      bool humanize,
                      const std::array<stacksampler::LayerRenderParameters,
                                       stacksampler::kMaxLayers>& snapshots);
    void releaseNote (int midiNote);
    void resetToInitialLayers();
    void resetLayerParameters (int bank);
    void setParameterValue (const juce::String& parameterId, float actualValue);
    void applyPendingHumanizeSeed();
    void timerCallback() override;

    juce::AudioFormatManager formatManager;
    std::array<stacksampler::LayerEngine, stacksampler::kMaxLayers> layerEngines;
    std::array<std::unique_ptr<stacksampler::LayerParameterRefs>,
               stacksampler::kMaxLayers> parameterRefs;
    std::array<LayerMetadata, stacksampler::kMaxLayers> layerMetadata;

    mutable juce::CriticalSection metadataLock;
    std::vector<int> layerOrder;

    juce::Random humanizeRandom;
    std::atomic<juce::int64> humanizeSeed { 0x535441434b };
    std::atomic<std::uint64_t> humanizeSeedGeneration { 1 };
    std::uint64_t appliedHumanizeSeedGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackSamplerAudioProcessor)
};
