#include <JuceHeader.h>

#include "LayerParameters.h"
#include "PluginProcessor.h"
#include "StackSamplerCore.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace
{
class DummyProcessor final : public juce::AudioProcessor
{
public:
    DummyProcessor() : AudioProcessor (BusesProperties()) {}

    const juce::String getName() const override { return "StackSamplerTests"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

struct TestContext
{
    void expect (bool condition, const std::string& message)
    {
        ++checks;
        if (condition)
            return;

        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    void expectNear (float actual,
                     float expected,
                     float tolerance,
                     const std::string& message)
    {
        expect (std::abs (actual - expected) <= tolerance,
                message + " (actual=" + std::to_string (actual)
                    + ", expected=" + std::to_string (expected) + ")");
    }

    int checks = 0;
    int failures = 0;
};

std::shared_ptr<const stacksampler::SampleData> makeRampSample (int sampleCount = 512)
{
    auto sample = std::make_shared<stacksampler::SampleData>();
    sample->sampleRate = 48000.0;
    sample->audio.setSize (1, sampleCount);

    for (int index = 0; index < sampleCount; ++index)
        sample->audio.setSample (0,
                                 index,
                                 static_cast<float> (index)
                                     / static_cast<float> (sampleCount - 1));

    return sample;
}

float energy (const juce::AudioBuffer<float>& buffer)
{
    auto total = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            total += std::abs (buffer.getSample (channel, sample));
    return total;
}

void testParameters (TestContext& context)
{
    DummyProcessor processor;
    juce::AudioProcessorValueTreeState state {
        processor,
        nullptr,
        juce::Identifier { "TEST_PARAMETERS" },
        stacksampler::createParameterLayout()
    };

    context.expect (processor.getParameters().size()
                        == static_cast<std::size_t> (
                            stacksampler::kMaxLayers * 20 + 1),
                    "the layout exposes 641 stable parameters");

    std::set<std::string> ids;
    for (const auto* parameter : processor.getParameters())
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter);
        context.expect (ranged != nullptr, "every parameter is ranged");
        if (ranged != nullptr)
            ids.insert (ranged->getParameterID().toStdString());
    }

    context.expect (ids.size()
                        == static_cast<std::size_t> (
                            processor.getParameters().size()),
                    "all parameter IDs are unique");
    context.expect (ids.count ("layer.00.volume") == 1,
                    "layer 1 volume ID remains stable");
    context.expect (ids.count ("layer.31.tail") == 1,
                    "layer 32 tail ID remains stable");
    context.expect (ids.count ("humanize") == 1,
                    "the global humanize ID is present");

    stacksampler::LayerParameterRefs refs { state, 0 };
    const auto defaults = refs.snapshot();
    context.expectNear (defaults.volumeDb, 0.0f, 0.0001f, "volume defaults to unity");
    context.expectNear (defaults.start, 0.0f, 0.0001f, "start defaults to zero");
    context.expectNear (defaults.end, 1.0f, 0.0001f, "end defaults to full length");
    context.expectNear (defaults.lowPassHz,
                        20000.0f,
                        0.1f,
                        "low pass defaults open");
    context.expectNear (defaults.stereoWidth,
                        1.0f,
                        0.0001f,
                        "width defaults to stereo");
    context.expect (! refs.isMuted() && ! refs.isSoloed(),
                    "layers default audible and not soloed");
}

void testQuickModes (TestContext& context)
{
    const auto modes = stacksampler::selectableQuickModes();
    context.expect (modes.size() == 7, "seven quick modes are selectable");
    context.expect (stacksampler::quickModeName (modes.front()) == "Clap",
                    "the first quick mode is Clap");
    context.expect (stacksampler::quickModeName (modes.back()) == "808",
                    "the final quick mode is 808");

    const auto hat = stacksampler::quickModeSettings (
        stacksampler::QuickMode::hiHat);
    context.expect (hat.decayMs < 200.0f && hat.highPassHz >= 500.0f,
                    "Hi-Hat applies a short, high-passed shape");

    const auto texture = stacksampler::quickModeSettings (
        stacksampler::QuickMode::texture);
    context.expect (texture.attackMs > 0.0f && texture.width > 1.0f
                        && texture.tail > 0.0f,
                    "Texture opens attack, width, and tail");

    const auto bass = stacksampler::quickModeSettings (
        stacksampler::QuickMode::eightOhEight);
    context.expectNear (bass.width, 0.0f, 0.0001f, "808 collapses to mono");
    context.expect (bass.lowPassHz <= 8000.0f && bass.driveDb > 0.0f,
                    "808 applies low-pass and drive");
}

juce::AudioBuffer<float> renderHit (int midiNote,
                                    const stacksampler::LayerRenderParameters& parameters,
                                    int outputSamples,
                                    bool humanize = false,
                                    int randomSeed = 1234)
{
    stacksampler::LayerEngine engine;
    engine.prepare (48000.0, outputSamples);
    engine.setSample (makeRampSample());
    engine.beginBlock (parameters);
    juce::Random random { randomSeed };
    engine.trigger (midiNote, 1.0f, humanize, random, parameters);

    juce::AudioBuffer<float> output { 2, outputSamples };
    output.clear();
    engine.render (output, 0, outputSamples, parameters);
    return output;
}

void testEngine (TestContext& context)
{
    stacksampler::LayerRenderParameters parameters;
    const auto normal = renderHit (60, parameters, 64);
    context.expect (energy (normal) > 1.0f, "MIDI 60 renders the sample");

    const auto octave = renderHit (72, parameters, 64);
    context.expect (octave.getSample (0, 20) > normal.getSample (0, 20) * 1.7f,
                    "MIDI 72 advances through the source at roughly double speed");

    auto reversedParameters = parameters;
    reversedParameters.reverse = true;
    const auto reversed = renderHit (60, reversedParameters, 8);
    context.expect (reversed.getSample (0, 0) > 0.95f,
                    "reverse begins at the end of the selected region");

    auto trimmedParameters = parameters;
    trimmedParameters.start = 0.5f;
    const auto trimmed = renderHit (60, trimmedParameters, 8);
    context.expect (trimmed.getSample (0, 0) > 0.48f
                        && trimmed.getSample (0, 0) < 0.53f,
                    "start selects the expected source position");

    stacksampler::LayerEngine mutedEngine;
    mutedEngine.prepare (48000.0, 64);
    mutedEngine.setSample (makeRampSample());
    mutedEngine.beginBlock (parameters);
    juce::Random random { 7 };
    mutedEngine.trigger (60, 1.0f, false, random, parameters);
    juce::AudioBuffer<float> mutedOutput { 2, 16 };
    mutedOutput.clear();
    auto mutedParameters = parameters;
    mutedParameters.mute = true;
    mutedEngine.render (mutedOutput, 0, 12, mutedParameters);
    context.expectNear (energy (mutedOutput), 0.0f, 0.0001f, "mute is silent");
    mutedEngine.render (mutedOutput, 12, 1, parameters);
    context.expect (mutedOutput.getSample (0, 12) > 0.015f,
                    "muted voices continue advancing in time");

    stacksampler::LayerEngine fullEngine;
    stacksampler::LayerEngine segmentedEngine;
    fullEngine.prepare (48000.0, 64);
    segmentedEngine.prepare (48000.0, 64);
    const auto sample = makeRampSample();
    fullEngine.setSample (sample);
    segmentedEngine.setSample (sample);
    fullEngine.beginBlock (parameters);
    segmentedEngine.beginBlock (parameters);
    juce::Random firstRandom { 99 };
    juce::Random secondRandom { 99 };
    fullEngine.trigger (60, 1.0f, false, firstRandom, parameters);
    segmentedEngine.trigger (60, 1.0f, false, secondRandom, parameters);
    juce::AudioBuffer<float> full { 2, 48 };
    juce::AudioBuffer<float> segmented { 2, 48 };
    full.clear();
    segmented.clear();
    fullEngine.render (full, 0, 48, parameters);
    segmentedEngine.render (segmented, 0, 17, parameters);
    segmentedEngine.render (segmented, 17, 31, parameters);
    context.expect (std::abs (full.getRMSLevel (0, 0, 48)
                              - segmented.getRMSLevel (0, 0, 48))
                        < 0.00001f,
                    "segmented MIDI rendering is continuous");

    const auto humanizedA = renderHit (60, parameters, 256, true, 42);
    const auto humanizedB = renderHit (60, parameters, 256, true, 42);
    auto maximumDifference = 0.0f;
    for (int sampleIndex = 0; sampleIndex < 256; ++sampleIndex)
        maximumDifference = juce::jmax (
            maximumDifference,
            std::abs (humanizedA.getSample (0, sampleIndex)
                      - humanizedB.getSample (0, sampleIndex)));
    context.expectNear (maximumDifference,
                        0.0f,
                        0.000001f,
                        "humanize is deterministic for a stored seed");

    auto quietParameters = parameters;
    quietParameters.volumeDb = -60.0f;
    quietParameters.gainDb = -60.0f;
    const auto quiet = renderHit (60, quietParameters, 64);
    context.expect (energy (quiet) < 0.001f,
                    "the first block starts at restored gain targets");

    stacksampler::LayerEngine replacementEngine;
    replacementEngine.prepare (48000.0, 64);
    auto firstSample = makeRampSample();
    std::weak_ptr<const stacksampler::SampleData> retired = firstSample;
    replacementEngine.setSample (firstSample);
    replacementEngine.beginBlock (parameters);
    firstSample.reset();
    replacementEngine.setSample (makeRampSample());
    replacementEngine.beginBlock (parameters);
    replacementEngine.collectRetiredSamples();
    context.expect (retired.expired(),
                    "replaced sample storage is reclaimed off the audio path");
}

void testProcessorLayerLifecycle (TestContext& context)
{
    StackSamplerAudioProcessor processor;
    context.expect (processor.getActiveLayers().size()
                        == static_cast<std::size_t> (stacksampler::kInitialLayers),
                    "a new processor opens with three layers");

    auto* mute = processor.parameters.getParameter (
        stacksampler::parameterID (1, "mute"));
    auto* pitch = processor.parameters.getParameter (
        stacksampler::parameterID (1, "pitch"));
    context.expect (mute != nullptr && pitch != nullptr,
                    "layer bank parameters are available");

    if (mute != nullptr && pitch != nullptr)
    {
        mute->setValueNotifyingHost (1.0f);
        pitch->setValueNotifyingHost (1.0f);
    }

    processor.removeLayer (1);
    context.expect (processor.addLayer(), "a removed layer bank can be reused");
    const auto active = processor.getActiveLayers();
    context.expect (! active.empty() && active.back() == 1,
                    "the reused bank is appended to the visible order");
    context.expect (processor.parameters.getRawParameterValue (
                        stacksampler::parameterID (1, "mute"))->load()
                        < 0.5f,
                    "a reused bank does not inherit mute");
    context.expectNear (
        processor.parameters.getRawParameterValue (
            stacksampler::parameterID (1, "pitch"))->load(),
        0.0f,
        0.0001f,
        "a reused bank does not inherit pitch");

    while (processor.addLayer())
    {
    }

    context.expect (processor.getActiveLayers().size()
                        == static_cast<std::size_t> (stacksampler::kMaxLayers),
                    "layers can grow to the fixed 32-bank host-safe limit");
    context.expect (! processor.addLayer(), "layer 33 is rejected cleanly");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TestContext context;
    testParameters (context);
    testQuickModes (context);
    testEngine (context);
    testProcessorLayerLifecycle (context);

    std::cout << context.checks << " checks, " << context.failures
              << " failures\n";
    return context.failures == 0 ? 0 : 1;
}
