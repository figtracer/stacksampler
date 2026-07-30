#include "LayerParameters.h"

#include <memory>
#include <vector>

namespace stacksampler
{
namespace
{
using Parameter = std::unique_ptr<juce::RangedAudioParameter>;

juce::String parameterName (int bank, const char* name)
{
    return "Layer " + juce::String (bank + 1).paddedLeft ('0', 2) + " " + name;
}

juce::NormalisableRange<float> frequencyRange()
{
    juce::NormalisableRange<float> range { 20.0f, 20000.0f, 0.01f };
    range.setSkewForCentre (1000.0f);
    return range;
}

juce::NormalisableRange<float> timeRange (float minimum, float maximum)
{
    return { minimum, maximum, 0.1f, 0.35f };
}

void addFloat (std::vector<Parameter>& parameters,
               int bank,
               const char* suffix,
               const char* name,
               juce::NormalisableRange<float> range,
               float defaultValue)
{
    parameters.push_back (
        std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { parameterID (bank, suffix), 1 },
            parameterName (bank, name),
            std::move (range),
            defaultValue));
}

void addBool (std::vector<Parameter>& parameters,
              int bank,
              const char* suffix,
              const char* name,
              bool defaultValue)
{
    parameters.push_back (
        std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { parameterID (bank, suffix), 1 },
            parameterName (bank, name),
            defaultValue));
}

std::atomic<float>* getParameter (juce::AudioProcessorValueTreeState& state,
                                  int bank,
                                  const char* suffix)
{
    auto* value = state.getRawParameterValue (parameterID (bank, suffix));
    jassert (value != nullptr);
    return value;
}
}

juce::String parameterID (int bank, const char* suffix)
{
    jassert (juce::isPositiveAndBelow (bank, kMaxLayers));
    jassert (suffix != nullptr);
    return "layer." + juce::String (bank).paddedLeft ('0', 2) + "." + suffix;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<Parameter> parameters;
    parameters.reserve (static_cast<std::size_t> (kMaxLayers * 20 + 1));

    for (int bank = 0; bank < kMaxLayers; ++bank)
    {
        addFloat (parameters, bank, "volume", "Volume",
                  { -60.0f, 6.0f, 0.01f }, 0.0f);
        addFloat (parameters, bank, "pan", "Pan",
                  { -1.0f, 1.0f, 0.001f }, 0.0f);
        addFloat (parameters, bank, "pitch", "Pitch",
                  { -24.0f, 24.0f, 1.0f }, 0.0f);
        addFloat (parameters, bank, "fine", "Fine Tune",
                  { -100.0f, 100.0f, 0.1f }, 0.0f);
        addFloat (parameters, bank, "start", "Start",
                  { 0.0f, 0.99f, 0.0001f }, 0.0f);
        addFloat (parameters, bank, "end", "End",
                  { 0.01f, 1.0f, 0.0001f }, 1.0f);
        addBool (parameters, bank, "reverse", "Reverse", false);
        addFloat (parameters, bank, "gain", "Gain",
                  { -24.0f, 24.0f, 0.01f }, 0.0f);
        addFloat (parameters, bank, "attack", "Attack",
                  timeRange (0.0f, 2000.0f), 0.0f);
        addFloat (parameters, bank, "decay", "Decay",
                  timeRange (10.0f, 30000.0f), 30000.0f);
        addFloat (parameters, bank, "release", "Release",
                  timeRange (1.0f, 5000.0f), 50.0f);
        addFloat (parameters, bank, "lowpass", "Low Pass",
                  frequencyRange(), 20000.0f);
        addFloat (parameters, bank, "highpass", "High Pass",
                  frequencyRange(), 20.0f);
        addFloat (parameters, bank, "drive", "Drive",
                  { 0.0f, 24.0f, 0.01f }, 0.0f);
        addFloat (parameters, bank, "saturation", "Saturation",
                  { 0.0f, 1.0f, 0.001f }, 0.0f);
        addFloat (parameters, bank, "width", "Stereo Width",
                  { 0.0f, 2.0f, 0.001f }, 1.0f);
        addFloat (parameters, bank, "transient", "Transient",
                  { -1.0f, 1.0f, 0.001f }, 0.0f);
        addFloat (parameters, bank, "tail", "Tail",
                  { -1.0f, 1.0f, 0.001f }, 0.0f);
        addBool (parameters, bank, "mute", "Mute", false);
        addBool (parameters, bank, "solo", "Solo", false);
    }

    parameters.push_back (
        std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "humanize", 1 },
            "Humanize",
            false));

    return { parameters.begin(), parameters.end() };
}

QuickModeSettings quickModeSettings (QuickMode mode) noexcept
{
    switch (mode)
    {
        case QuickMode::clap:
            return { 0.0f, 350.0f, 90.0f, 15000.0f, 180.0f,
                     2.0f, 0.12f, 1.15f, 0.15f, -0.10f };
        case QuickMode::snare:
            return { 0.0f, 500.0f, 100.0f, 14000.0f, 90.0f,
                     3.0f, 0.18f, 1.0f, 0.25f, -0.05f };
        case QuickMode::hiHat:
            return { 0.0f, 120.0f, 30.0f, 18000.0f, 500.0f,
                     1.5f, 0.10f, 1.05f, 0.20f, -0.35f };
        case QuickMode::perc:
            return { 0.0f, 300.0f, 70.0f, 16000.0f, 80.0f,
                     2.0f, 0.12f, 1.0f, 0.15f, -0.10f };
        case QuickMode::texture:
            return { 40.0f, 8000.0f, 1500.0f, 12000.0f, 30.0f,
                     1.0f, 0.08f, 1.40f, -0.20f, 0.30f };
        case QuickMode::vocal:
            return { 5.0f, 3000.0f, 200.0f, 18000.0f, 80.0f,
                     1.0f, 0.05f, 1.10f, 0.0f, 0.0f };
        case QuickMode::eightOhEight:
            return { 0.0f, 8000.0f, 350.0f, 8000.0f, 20.0f,
                     4.0f, 0.25f, 0.0f, 0.05f, 0.20f };
        case QuickMode::none:
        default:
            return {};
    }
}

std::array<QuickMode, 7> selectableQuickModes() noexcept
{
    return { QuickMode::clap,
             QuickMode::snare,
             QuickMode::hiHat,
             QuickMode::perc,
             QuickMode::texture,
             QuickMode::vocal,
             QuickMode::eightOhEight };
}

juce::String quickModeName (QuickMode mode)
{
    switch (mode)
    {
        case QuickMode::clap: return "Clap";
        case QuickMode::snare: return "Snare";
        case QuickMode::hiHat: return "Hi-Hat";
        case QuickMode::perc: return "Perc";
        case QuickMode::texture: return "Texture";
        case QuickMode::vocal: return "Vocal";
        case QuickMode::eightOhEight: return "808";
        case QuickMode::none:
        default: return "None";
    }
}

LayerParameterRefs::LayerParameterRefs (juce::AudioProcessorValueTreeState& state,
                                        int bank)
    : volume (getParameter (state, bank, "volume")),
      pan (getParameter (state, bank, "pan")),
      pitch (getParameter (state, bank, "pitch")),
      fine (getParameter (state, bank, "fine")),
      sampleStart (getParameter (state, bank, "start")),
      sampleEnd (getParameter (state, bank, "end")),
      reverse (getParameter (state, bank, "reverse")),
      gain (getParameter (state, bank, "gain")),
      attack (getParameter (state, bank, "attack")),
      decay (getParameter (state, bank, "decay")),
      release (getParameter (state, bank, "release")),
      lowPass (getParameter (state, bank, "lowpass")),
      highPass (getParameter (state, bank, "highpass")),
      drive (getParameter (state, bank, "drive")),
      saturation (getParameter (state, bank, "saturation")),
      width (getParameter (state, bank, "width")),
      transient (getParameter (state, bank, "transient")),
      tail (getParameter (state, bank, "tail")),
      mute (getParameter (state, bank, "mute")),
      solo (getParameter (state, bank, "solo"))
{
}

float LayerParameterRefs::read (const std::atomic<float>* parameter) noexcept
{
    return parameter != nullptr ? parameter->load (std::memory_order_relaxed) : 0.0f;
}

LayerRenderParameters LayerParameterRefs::snapshot() const noexcept
{
    LayerRenderParameters result;
    result.volumeDb = read (volume);
    result.pan = read (pan);
    result.pitchSemitones = read (pitch);
    result.fineTuneCents = read (fine);
    result.start = juce::jlimit (0.0f, 0.99f, read (sampleStart));
    result.end = juce::jlimit (0.01f, 1.0f, read (sampleEnd));

    if (result.end <= result.start)
    {
        result.end = juce::jmin (1.0f, result.start + 0.01f);

        if (result.end <= result.start)
            result.start = juce::jmax (0.0f, result.end - 0.01f);
    }

    result.reverse = read (reverse) >= 0.5f;
    result.gainDb = read (gain);
    result.attackMs = read (attack);
    result.decayMs = read (decay);
    result.releaseMs = read (release);
    result.lowPassHz = read (lowPass);
    result.highPassHz = read (highPass);
    result.driveDb = read (drive);
    result.saturation = read (saturation);
    result.stereoWidth = read (width);
    result.transient = read (transient);
    result.tail = read (tail);
    result.mute = isMuted();
    result.solo = isSoloed();
    return result;
}

bool LayerParameterRefs::isMuted() const noexcept
{
    return read (mute) >= 0.5f;
}

bool LayerParameterRefs::isSoloed() const noexcept
{
    return read (solo) >= 0.5f;
}
}
