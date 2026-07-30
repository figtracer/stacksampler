#pragma once

#include <JuceHeader.h>

#include <array>

namespace stacksampler
{
inline constexpr int kMaxLayers = 32;
inline constexpr int kInitialLayers = 3;

enum class QuickMode
{
    none = 0,
    clap,
    snare,
    hiHat,
    perc,
    texture,
    vocal,
    eightOhEight
};

struct QuickModeSettings
{
    float attackMs = 0.0f;
    float decayMs = 30000.0f;
    float releaseMs = 50.0f;
    float lowPassHz = 20000.0f;
    float highPassHz = 20.0f;
    float driveDb = 0.0f;
    float saturation = 0.0f;
    float width = 1.0f;
    float transient = 0.0f;
    float tail = 0.0f;
};

struct LayerRenderParameters
{
    float volumeDb = 0.0f;
    float pan = 0.0f;
    float pitchSemitones = 0.0f;
    float fineTuneCents = 0.0f;
    float start = 0.0f;
    float end = 1.0f;
    bool reverse = false;
    float gainDb = 0.0f;
    float attackMs = 0.0f;
    float decayMs = 30000.0f;
    float releaseMs = 50.0f;
    float lowPassHz = 20000.0f;
    float highPassHz = 20.0f;
    float driveDb = 0.0f;
    float saturation = 0.0f;
    float stereoWidth = 1.0f;
    float transient = 0.0f;
    float tail = 0.0f;
    bool mute = false;
    bool solo = false;
};

juce::String parameterID (int bank, const char* suffix);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

QuickModeSettings quickModeSettings (QuickMode mode) noexcept;
std::array<QuickMode, 7> selectableQuickModes() noexcept;
juce::String quickModeName (QuickMode mode);

class LayerParameterRefs
{
public:
    LayerParameterRefs (juce::AudioProcessorValueTreeState& state, int bank);

    LayerRenderParameters snapshot() const noexcept;
    bool isMuted() const noexcept;
    bool isSoloed() const noexcept;

private:
    static float read (const std::atomic<float>* parameter) noexcept;

    std::atomic<float>* volume = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* pitch = nullptr;
    std::atomic<float>* fine = nullptr;
    std::atomic<float>* sampleStart = nullptr;
    std::atomic<float>* sampleEnd = nullptr;
    std::atomic<float>* reverse = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* lowPass = nullptr;
    std::atomic<float>* highPass = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* saturation = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* transient = nullptr;
    std::atomic<float>* tail = nullptr;
    std::atomic<float>* mute = nullptr;
    std::atomic<float>* solo = nullptr;
};
}
