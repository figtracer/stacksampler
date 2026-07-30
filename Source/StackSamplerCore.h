#pragma once

#include <JuceHeader.h>

#include "LayerParameters.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace stacksampler
{
struct SampleData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
};

class LayerEngine
{
public:
    LayerEngine() = default;
    ~LayerEngine() = default;

    void prepare (double sampleRate, int maximumExpectedSamplesPerBlock);
    void reset();
    void releaseResources() noexcept;

    void setSample (std::shared_ptr<const SampleData> newSample);
    std::shared_ptr<const SampleData> getSample() const;
    void collectRetiredSamples();

    void beginBlock (const LayerRenderParameters& parameters);
    void trigger (int midiNote,
                  float velocity,
                  bool humanize,
                  juce::Random& random,
                  const LayerRenderParameters& parameters);
    void release (int midiNote);
    void stopAllVoices() noexcept;
    void render (juce::AudioBuffer<float>& destination,
                 int startSample,
                 int numSamples,
                 const LayerRenderParameters& parameters);

private:
    static constexpr int voiceCount = 16;

    enum class EnvelopeStage
    {
        attack,
        decay,
        release,
        stopped
    };

    struct Voice
    {
        const SampleData* sample = nullptr;
        double sourcePosition = 0.0;
        double sourceStep = 1.0;
        int regionStart = 0;
        int regionEnd = 0;
        int midiNote = -1;
        int delaySamples = 0;
        int attackSamples = 0;
        int decaySamples = 1;
        int releaseSamples = 1;
        int stageSample = 0;
        float envelopeLevel = 0.0f;
        float releaseStartLevel = 0.0f;
        float velocity = 0.0f;
        float humanGain = 1.0f;
        float humanPan = 0.0f;
        float transient = 0.0f;
        float tail = 0.0f;
        std::uint64_t age = 0;
        bool reverse = false;
        EnvelopeStage stage = EnvelopeStage::stopped;

        bool isActive() const noexcept
        {
            return stage != EnvelopeStage::stopped && sample != nullptr;
        }

        void stop() noexcept
        {
            sample = nullptr;
            stage = EnvelopeStage::stopped;
            midiNote = -1;
            envelopeLevel = 0.0f;
        }
    };

    void consumeControlRequests();
    void clearVoices() noexcept;
    void clearFilterState() noexcept;
    void updateTargets (const LayerRenderParameters& parameters) noexcept;
    void renderChunk (juce::AudioBuffer<float>& destination,
                      int startSample,
                      int numSamples,
                      const LayerRenderParameters& parameters);
    void renderVoice (Voice& voice, int numSamples);
    float nextEnvelopeSample (Voice& voice) noexcept;
    float readSample (const Voice& voice, int channel) const noexcept;

    static void panGains (float pan, float& left, float& right) noexcept;

    struct RetiredSample
    {
        std::uint64_t safeAfterGeneration = 0;
        std::shared_ptr<const SampleData> sample;
    };

    mutable juce::CriticalSection sampleOwnershipLock;
    std::shared_ptr<const SampleData> ownedSample;
    std::vector<RetiredSample> retiredSamples;
    std::atomic<const SampleData*> publishedSample { nullptr };
    const SampleData* blockSample = nullptr;
    std::atomic<std::uint64_t> sampleGeneration { 0 };
    std::atomic<std::uint64_t> consumedSampleGeneration { 0 };
    std::atomic<std::uint64_t> resetGeneration { 0 };
    std::uint64_t appliedSampleGeneration = 0;
    std::uint64_t appliedResetGeneration = 0;

    std::array<Voice, voiceCount> voices;
    std::uint64_t nextVoiceAge = 1;

    juce::AudioBuffer<float> scratch;
    double outputSampleRate = 44100.0;
    int preparedBlockSize = 1;

    std::array<float, 2> highPassState {};
    std::array<float, 2> lowPassState {};
    float highPassCoefficient = 1.0f;
    float lowPassCoefficient = 1.0f;
    bool highPassEnabled = false;
    bool lowPassEnabled = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> volume;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pan;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> width;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturation;
    bool parameterTargetsInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEngine)
};
}
