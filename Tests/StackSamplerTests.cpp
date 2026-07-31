#include <JuceHeader.h>

#include "LayerParameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginUiModel.h"
#include "StackSamplerCore.h"

#include <cmath>
#include <cstdlib>
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

std::shared_ptr<const stacksampler::SampleData> makeConstantSample (
    int sampleCount = 8192,
    float value = 0.5f)
{
    auto sample = std::make_shared<stacksampler::SampleData>();
    sample->sampleRate = 48000.0;
    sample->audio.setSize (1, sampleCount);

    for (int index = 0; index < sampleCount; ++index)
        sample->audio.setSample (0, index, value);

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

bool writeTestAudioFile (juce::AudioFormat& format,
                         const juce::File& file,
                         const juce::AudioBuffer<float>& audio)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };
    if (stream == nullptr)
        return false;

    using Options = juce::AudioFormatWriterOptions;
    auto writer = format.createWriterFor (
        stream,
        Options {}.withSampleRate (48000.0)
                  .withNumChannels (audio.getNumChannels())
                  .withBitsPerSample (24));
    return writer != nullptr
        && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

bool waitForMessageCondition (const std::function<bool()>& condition,
                              int timeoutMilliseconds = 2000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                        + static_cast<double> (timeoutMilliseconds);
    while (! condition()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    return condition();
}

juce::File makeUniqueTestDirectory (const juce::String& prefix)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (prefix + "-" + juce::Uuid {}.toString());
}

float maximumStep (const juce::AudioBuffer<float>& buffer, float previousSample)
{
    auto result = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto current = buffer.getSample (0, sample);
        result = juce::jmax (result, std::abs (current - previousSample));
        previousSample = current;
    }

    return result;
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

void testWaveformMapping (TestContext& context)
{
    const auto forward = stacksampler::ui::visualTrimRange (0.10f, 0.65f, false);
    context.expectNear (forward.start, 0.10f, 0.0001f,
                        "forward waveform keeps the source start");
    context.expectNear (forward.end, 0.65f, 0.0001f,
                        "forward waveform keeps the source end");

    const auto reverse = stacksampler::ui::visualTrimRange (0.10f, 0.65f, true);
    context.expectNear (reverse.start, 0.35f, 0.0001f,
                        "reverse waveform mirrors the selected region start");
    context.expectNear (reverse.end, 0.90f, 0.0001f,
                        "reverse waveform mirrors the selected region end");
    context.expectNear (
        stacksampler::ui::sourcePositionFromVisual (0.20f, true),
        0.80f,
        0.0001f,
        "dragging the reverse IN edge maps back to the source end");
    context.expectNear (
        stacksampler::ui::sourcePositionFromVisual (0.90f, true),
        0.10f,
        0.0001f,
        "dragging the reverse OUT edge maps back to the source start");
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

void testClickSafeTransitions (TestContext& context)
{
    stacksampler::LayerRenderParameters parameters;
    auto mutedParameters = parameters;
    mutedParameters.mute = true;

    stacksampler::LayerEngine initiallyMutedEngine;
    initiallyMutedEngine.prepare (48000.0, 512);
    initiallyMutedEngine.setSample (makeConstantSample());
    initiallyMutedEngine.beginBlock (mutedParameters);
    juce::Random initiallyMutedRandom { 101 };
    initiallyMutedEngine.trigger (
        60, 1.0f, false, initiallyMutedRandom, mutedParameters);
    juce::AudioBuffer<float> initiallyMuted { 2, 128 };
    initiallyMuted.clear();
    initiallyMutedEngine.render (initiallyMuted, 0, 128, mutedParameters);
    context.expectNear (energy (initiallyMuted),
                        0.0f,
                        0.000001f,
                        "a layer first rendered muted does not leak");

    stacksampler::LayerEngine muteEngine;
    muteEngine.prepare (48000.0, 512);
    muteEngine.setSample (makeConstantSample());
    muteEngine.beginBlock (parameters);
    juce::Random muteRandom { 102 };
    muteEngine.trigger (60, 1.0f, false, muteRandom, parameters);
    juce::AudioBuffer<float> muteWarmup { 2, 64 };
    muteWarmup.clear();
    muteEngine.render (muteWarmup, 0, 64, parameters);
    const auto beforeMute = muteWarmup.getSample (0, 63);

    juce::AudioBuffer<float> fadeOut { 2, 320 };
    fadeOut.clear();
    muteEngine.render (fadeOut, 0, 320, mutedParameters);
    context.expect (maximumStep (fadeOut, beforeMute) < 0.01f,
                    "mute fades without a sample discontinuity");
    context.expect (std::abs (fadeOut.getSample (0, 319)) < 0.000001f,
                    "the mute ramp reaches silence");

    juce::AudioBuffer<float> fadeIn { 2, 320 };
    fadeIn.clear();
    muteEngine.render (fadeIn, 0, 320, parameters);
    context.expect (maximumStep (fadeIn, fadeOut.getSample (0, 319)) < 0.01f,
                    "unmute fades without a sample discontinuity");
    context.expect (fadeIn.getSample (0, 319) > 0.45f,
                    "the unmute ramp returns to full audibility");

    const auto rampSample = makeRampSample (4096);
    stacksampler::LayerEngine mutedProgressEngine;
    stacksampler::LayerEngine referenceProgressEngine;
    mutedProgressEngine.prepare (48000.0, 1024);
    referenceProgressEngine.prepare (48000.0, 1024);
    mutedProgressEngine.setSample (rampSample);
    referenceProgressEngine.setSample (rampSample);
    mutedProgressEngine.beginBlock (parameters);
    referenceProgressEngine.beginBlock (parameters);
    juce::Random mutedProgressRandom { 103 };
    juce::Random referenceProgressRandom { 103 };
    mutedProgressEngine.trigger (
        60, 1.0f, false, mutedProgressRandom, parameters);
    referenceProgressEngine.trigger (
        60, 1.0f, false, referenceProgressRandom, parameters);

    juce::AudioBuffer<float> referenceProgress { 2, 704 };
    referenceProgress.clear();
    referenceProgressEngine.render (referenceProgress, 0, 704, parameters);
    juce::AudioBuffer<float> progressWarmup { 2, 64 };
    juce::AudioBuffer<float> progressMuted { 2, 320 };
    juce::AudioBuffer<float> progressAudible { 2, 320 };
    progressWarmup.clear();
    progressMuted.clear();
    progressAudible.clear();
    mutedProgressEngine.render (progressWarmup, 0, 64, parameters);
    mutedProgressEngine.render (progressMuted, 0, 320, mutedParameters);
    mutedProgressEngine.render (progressAudible, 0, 320, parameters);
    context.expectNear (progressAudible.getSample (0, 319),
                        referenceProgress.getSample (0, 703),
                        0.000001f,
                        "muted voices continue advancing in time");

    stacksampler::LayerEngine highPassBypassEngine;
    highPassBypassEngine.prepare (48000.0, 1200);
    highPassBypassEngine.setSample (makeConstantSample());
    highPassBypassEngine.beginBlock (parameters);
    juce::Random highPassBypassRandom { 104 };
    highPassBypassEngine.trigger (
        60, 1.0f, false, highPassBypassRandom, parameters);
    juce::AudioBuffer<float> highPassWarmup { 2, 128 };
    highPassWarmup.clear();
    highPassBypassEngine.render (highPassWarmup, 0, 128, parameters);

    auto highPassParameters = parameters;
    highPassParameters.highPassHz = 1200.0f;
    highPassBypassEngine.beginBlock (highPassParameters);
    juce::AudioBuffer<float> highPassEnabled { 2, 1024 };
    highPassEnabled.clear();
    highPassBypassEngine.render (
        highPassEnabled, 0, 1024, highPassParameters);
    context.expect (
        std::abs (highPassEnabled.getSample (0, 0)
                  - highPassWarmup.getSample (0, 127))
            < 0.01f,
        "high-pass enable crosses the default boundary smoothly");
    context.expect (std::abs (highPassEnabled.getSample (0, 1023)) < 0.01f,
                    "the high-pass wet ramp reaches its filtered target");

    highPassBypassEngine.beginBlock (parameters);
    juce::AudioBuffer<float> highPassDisabled { 2, 1024 };
    highPassDisabled.clear();
    highPassBypassEngine.render (
        highPassDisabled, 0, 1024, parameters);
    context.expect (
        std::abs (highPassDisabled.getSample (0, 0)
                  - highPassEnabled.getSample (0, 1023))
            < 0.01f,
        "high-pass bypass returns to dry smoothly");
    context.expect (highPassDisabled.getSample (0, 1023) > 0.45f,
                    "high-pass bypass reaches the dry signal");

    stacksampler::LayerEngine lowPassBypassEngine;
    lowPassBypassEngine.prepare (48000.0, 1200);
    lowPassBypassEngine.setSample (makeConstantSample());
    lowPassBypassEngine.beginBlock (parameters);
    juce::Random lowPassBypassRandom { 105 };
    lowPassBypassEngine.trigger (
        60, 1.0f, false, lowPassBypassRandom, parameters);
    juce::AudioBuffer<float> lowPassWarmup { 2, 128 };
    lowPassWarmup.clear();
    lowPassBypassEngine.render (lowPassWarmup, 0, 128, parameters);

    auto lowPassParameters = parameters;
    lowPassParameters.lowPassHz = 50.0f;
    lowPassBypassEngine.beginBlock (lowPassParameters);
    juce::AudioBuffer<float> lowPassEnabled { 2, 1024 };
    lowPassEnabled.clear();
    lowPassBypassEngine.render (lowPassEnabled, 0, 1024, lowPassParameters);
    context.expect (
        std::abs (lowPassEnabled.getSample (0, 0)
                  - lowPassWarmup.getSample (0, 127))
            < 0.01f,
        "low-pass enable uses warm state at the default boundary");
    context.expect (lowPassEnabled.getSample (0, 1023) > 0.45f,
                    "low-pass transition preserves steady DC");

    auto lowPassCoefficientParameters = parameters;
    lowPassCoefficientParameters.lowPassHz = 100.0f;
    stacksampler::LayerEngine lowPassCoefficientEngine;
    lowPassCoefficientEngine.prepare (48000.0, 2048);
    lowPassCoefficientEngine.setSample (makeRampSample (8192));
    lowPassCoefficientEngine.beginBlock (lowPassCoefficientParameters);
    juce::Random lowPassCoefficientRandom { 106 };
    lowPassCoefficientEngine.trigger (
        60, 1.0f, false, lowPassCoefficientRandom, lowPassCoefficientParameters);
    juce::AudioBuffer<float> lowPassCoefficientWarmup { 2, 2048 };
    lowPassCoefficientWarmup.clear();
    lowPassCoefficientEngine.render (
        lowPassCoefficientWarmup, 0, 2048, lowPassCoefficientParameters);
    lowPassCoefficientParameters.lowPassHz = 10000.0f;
    lowPassCoefficientEngine.beginBlock (lowPassCoefficientParameters);
    juce::AudioBuffer<float> lowPassCoefficientChange { 2, 16 };
    lowPassCoefficientChange.clear();
    lowPassCoefficientEngine.render (
        lowPassCoefficientChange, 0, 16, lowPassCoefficientParameters);
    context.expect (
        std::abs (lowPassCoefficientChange.getSample (0, 0)
                  - lowPassCoefficientWarmup.getSample (0, 2047))
            < 0.001f,
        "low-pass coefficient automation is smoothed");

    auto highPassCoefficientParameters = parameters;
    highPassCoefficientParameters.highPassHz = 100.0f;
    stacksampler::LayerEngine highPassCoefficientEngine;
    highPassCoefficientEngine.prepare (48000.0, 2048);
    highPassCoefficientEngine.setSample (makeRampSample (8192));
    highPassCoefficientEngine.beginBlock (highPassCoefficientParameters);
    juce::Random highPassCoefficientRandom { 107 };
    highPassCoefficientEngine.trigger (
        60, 1.0f, false, highPassCoefficientRandom, highPassCoefficientParameters);
    juce::AudioBuffer<float> highPassCoefficientWarmup { 2, 2048 };
    highPassCoefficientWarmup.clear();
    highPassCoefficientEngine.render (
        highPassCoefficientWarmup, 0, 2048, highPassCoefficientParameters);
    highPassCoefficientParameters.highPassHz = 10000.0f;
    highPassCoefficientEngine.beginBlock (highPassCoefficientParameters);
    juce::AudioBuffer<float> highPassCoefficientChange { 2, 16 };
    highPassCoefficientChange.clear();
    highPassCoefficientEngine.render (
        highPassCoefficientChange, 0, 16, highPassCoefficientParameters);
    context.expect (
        std::abs (highPassCoefficientChange.getSample (0, 0)
                  - highPassCoefficientWarmup.getSample (0, 2047))
            < 0.001f,
        "high-pass coefficient automation is smoothed");
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

void testSampleLoading (TestContext& context)
{
    const auto directory = makeUniqueTestDirectory ("StackSampler-Format-Tests");
    context.expect (directory.createDirectory().wasOk(),
                    "the sample-format test directory is available");

    juce::AudioBuffer<float> audio { 2, 2048 };
    for (int sampleIndex = 0; sampleIndex < audio.getNumSamples(); ++sampleIndex)
    {
        const auto value = std::sin (static_cast<float> (sampleIndex) * 0.13f) * 0.5f;
        audio.setSample (0, sampleIndex, value);
        audio.setSample (1, sampleIndex, value * 0.8f);
    }

    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    const std::array files
    {
        directory.getChildFile ("format-wav.wav"),
        directory.getChildFile ("format-aiff.aiff"),
        directory.getChildFile ("format-flac.flac")
    };
    const std::array<juce::AudioFormat*, 3> formats { &wav, &aiff, &flac };

    StackSamplerAudioProcessor processor;
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        context.expect (writeTestAudioFile (*formats[index], files[index], audio),
                        "a supported sample fixture can be encoded");
        juce::String error;
        context.expect (processor.loadSample (static_cast<int> (index),
                                              files[index], &error),
                        "WAV, AIFF, and FLAC fixtures decode successfully");
        context.expect (error.isEmpty(), "successful sample loads clear the error");

        const auto sample = processor.getSampleData (static_cast<int> (index));
        context.expect (sample != nullptr && sample->audio.getNumSamples() == 2048,
                        "decoded sample audio reaches the layer engine");
        context.expect (sample != nullptr && sample->waveformMin.size() == 1024
                            && sample->waveformMax.size() == 1024,
                        "sample loading creates a bounded waveform summary");
    }

    const auto unsupported = directory.getChildFile ("not-a-sample.txt");
    unsupported.replaceWithText ("StackSampler format test");
    juce::String unsupportedError;
    context.expect (! processor.loadSample (0, unsupported, &unsupportedError),
                    "unsupported sample extensions are rejected");
    context.expect (unsupportedError == "Use WAV, AIFF or FLAC",
                    "unsupported files return an actionable inline error");

    StackSamplerAudioProcessor asyncProcessor;
    asyncProcessor.loadSampleAsync (0, files[2]);
    context.expect (asyncProcessor.getLayerDisplayState (0).status
                        == "Loading sample...",
                    "asynchronous drops report progress immediately");
    context.expect (
        waitForMessageCondition ([&asyncProcessor]
                                 { return asyncProcessor.getSampleData (0) != nullptr; }),
        "asynchronous sample decoding completes on the message thread");
    context.expect (asyncProcessor.getLayerDisplayState (0).name == "format-flac",
                    "asynchronous decoding publishes sample metadata");

    asyncProcessor.loadSampleAsync (1, files[0]);
    asyncProcessor.loadSampleAsync (1, files[1]);
    context.expect (
        waitForMessageCondition ([&asyncProcessor]
                                 {
                                     return asyncProcessor.getLayerDisplayState (1).name
                                         == "format-aiff";
        }),
        "the newest drop wins when two loads target one layer");

    asyncProcessor.loadSampleAsync (1, files[0]);
    asyncProcessor.loadSampleAsync (1, unsupported);
    asyncProcessor.loadSampleAsync (0, files[0]);
    context.expect (
        waitForMessageCondition ([&asyncProcessor]
                                 {
                                     return asyncProcessor.getLayerDisplayState (0).name
                                         == "format-wav";
                                 }),
        "a later sentinel load drains earlier queued decode work");
    context.expect (asyncProcessor.getLayerDisplayState (1).name == "format-aiff"
                        && asyncProcessor.getLayerDisplayState (1).status
                            == "Use WAV, AIFF or FLAC",
                    "an invalid newest drop cancels an older valid decode");

    asyncProcessor.loadSampleAsync (2, files[0]);
    asyncProcessor.removeLayer (2);
    asyncProcessor.loadSampleAsync (0, files[2]);
    context.expect (
        waitForMessageCondition ([&asyncProcessor]
                                 {
                                     return asyncProcessor.getLayerDisplayState (0).name
                                         == "format-flac";
                                 }),
        "a sentinel load drains work queued before layer removal");
    context.expect (asyncProcessor.getSampleData (2) == nullptr,
                    "removing a layer cancels a pending sample publication");

    {
        auto closingProcessor = std::make_unique<StackSamplerAudioProcessor>();
        closingProcessor->loadSampleAsync (0, files[0]);
        closingProcessor->loadSampleAsync (1, files[1]);
        closingProcessor.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        context.expect (true,
                        "closing during queued sample loads completes safely");
    }

    directory.deleteRecursively();
}

void testStateRoundTrip (TestContext& context)
{
    const auto directory = makeUniqueTestDirectory ("StackSampler-State-Test");
    context.expect (directory.createDirectory().wasOk(),
                    "the state test directory is available");

    juce::AudioBuffer<float> audio { 1, 128 };
    for (int sampleIndex = 0; sampleIndex < audio.getNumSamples(); ++sampleIndex)
        audio.setSample (0, sampleIndex,
                         static_cast<float> (sampleIndex) / 127.0f - 0.5f);

    juce::WavAudioFormat wav;
    const auto sampleFile = directory.getChildFile ("saved-layer.wav");
    context.expect (writeTestAudioFile (wav, sampleFile, audio),
                    "the state test sample can be encoded");

    StackSamplerAudioProcessor source;
    context.expect (source.loadSample (0, sampleFile),
                    "the state test sample loads before serialization");
    source.removeLayer (1);
    context.expect (source.addLayer(), "the state test creates a recycled layer order");
    source.applyQuickMode (2, stacksampler::QuickMode::texture);
    source.setHumanizeEnabled (true);

    if (auto* pitch = source.parameters.getParameter (
            stacksampler::parameterID (2, "pitch")))
        pitch->setValueNotifyingHost (pitch->convertTo0to1 (7.0f));

    juce::MemoryBlock state;
    source.getStateInformation (state);
    context.expect (! state.isEmpty(), "processor state serializes to a non-empty block");

    StackSamplerAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    context.expect (restored.getActiveLayers() == std::vector<int> ({ 0, 2, 1 }),
                    "state restoration preserves visible layer order");
    context.expect (restored.getSampleData (0) != nullptr,
                    "state restoration reloads an available sample path");
    context.expect (restored.getLayerDisplayState (0).name == "saved-layer",
                    "state restoration recovers sample metadata");
    context.expect (restored.getLayerDisplayState (2).quickMode
                        == stacksampler::QuickMode::texture,
                    "state restoration preserves the quick-start choice");
    context.expectNear (
        restored.parameters.getRawParameterValue (
            stacksampler::parameterID (2, "pitch"))->load(),
        7.0f,
        0.0001f,
        "state restoration preserves layer parameter values");
    context.expect (restored.isHumanizeEnabled(),
                    "state restoration preserves global Humanize");

    directory.deleteRecursively();
}

void testEditorLifecycle (TestContext& context)
{
    StackSamplerAudioProcessor processor;
    {
        StackSamplerAudioProcessorEditor editor { processor };
        for (const auto& size : std::array {
                 std::pair { 1060, 780 },
                 std::pair { 1280, 800 },
                 std::pair { 1560, 940 } })
        {
            editor.setSize (size.first, size.second);
            const auto image = editor.createComponentSnapshot (
                editor.getLocalBounds(), true, 0.5f);
            context.expect (image.isValid(),
                            "the editor renders at a supported window size");
            context.expect (image.getWidth() == size.first / 2
                                && image.getHeight() == size.second / 2,
                            "the responsive editor snapshot has the expected bounds");
        }
    }

    processor.applyQuickMode (0, stacksampler::QuickMode::snare);
    processor.removeLayer (1);
    context.expect (processor.addLayer(),
                    "the editor lifecycle test can recycle a visible layer bank");

    StackSamplerAudioProcessorEditor reorderedEditor { processor };
    reorderedEditor.setSize (1280, 800);
    const auto afterReorder = reorderedEditor.createComponentSnapshot (
        reorderedEditor.getLocalBounds(), true, 0.5f);
    context.expect (afterReorder.isValid(),
                    "the editor remains renderable after mode and layer-order changes");
}

juce::File makeSnapshotSample (const juce::File& directory,
                               const juce::String& name,
                               int sampleCount,
                               float decay,
                               float tone)
{
    const auto file = directory.getChildFile (name + ".wav");
    file.deleteFile();

    juce::AudioBuffer<float> audio (2, sampleCount);
    juce::Random random { name.hashCode64() };
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const auto progress = static_cast<float> (sampleIndex)
                            / static_cast<float> (juce::jmax (1, sampleCount - 1));
        const auto envelope = std::exp (-progress * decay);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        const auto signal = (std::sin (static_cast<float> (sampleIndex) * tone)
                             + noise * 0.55f)
                          * envelope * 0.62f;
        audio.setSample (0, sampleIndex, signal);
        audio.setSample (1, sampleIndex, signal * 0.93f);
    }

    std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };
    if (stream == nullptr)
        return {};

    juce::WavAudioFormat format;
    using Options = juce::AudioFormatWriterOptions;
    if (auto writer = format.createWriterFor (
            stream,
            Options {}.withSampleRate (48000.0)
                      .withNumChannels (2)
                      .withBitsPerSample (24)))
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());

    return file;
}

void renderEditorSnapshots()
{
    const auto* outputPath = std::getenv ("STACKSAMPLER_SNAPSHOT_DIR");
    if (outputPath == nullptr || *outputPath == '\0')
        return;

    const juce::File outputDirectory { juce::String::fromUTF8 (outputPath) };
    if (! outputDirectory.createDirectory())
        return;

    const auto sampleDirectory = makeUniqueTestDirectory ("StackSampler-UI-Samples");
    if (! sampleDirectory.createDirectory())
        return;

    const std::array samples
    {
        makeSnapshotSample (sampleDirectory, "Soft Clay Clap", 24000, 7.0f, 0.087f),
        makeSnapshotSample (sampleDirectory, "Glass Hat Reverse", 15000, 12.0f, 0.213f),
        makeSnapshotSample (sampleDirectory,
                            "Field Texture with a Deliberately Long Name",
                            52000, 2.2f, 0.031f)
    };

    StackSamplerAudioProcessor processor;
    for (std::size_t index = 0; index < samples.size(); ++index)
        processor.loadSample (static_cast<int> (index), samples[index]);
    processor.applyQuickMode (0, stacksampler::QuickMode::clap);
    processor.applyQuickMode (1, stacksampler::QuickMode::hiHat);
    processor.applyQuickMode (2, stacksampler::QuickMode::texture);

    if (auto* reverse = processor.parameters.getParameter (
            stacksampler::parameterID (1, "reverse")))
        reverse->setValueNotifyingHost (1.0f);
    if (auto* start = processor.parameters.getParameter (
            stacksampler::parameterID (2, "start")))
        start->setValueNotifyingHost (start->convertTo0to1 (0.10f));
    if (auto* end = processor.parameters.getParameter (
            stacksampler::parameterID (2, "end")))
        end->setValueNotifyingHost (end->convertTo0to1 (0.76f));

    const std::array sizes
    {
        std::pair { 1060, 780 },
        std::pair { 1280, 800 },
        std::pair { 1560, 940 }
    };

    {
        StackSamplerAudioProcessorEditor editor { processor };
        for (const auto& [width, height] : sizes)
        {
            editor.setSize (width, height);
            const auto image = editor.createComponentSnapshot (
                editor.getLocalBounds(), true, 1.0f);
            const auto destination = outputDirectory.getChildFile (
                "ui-" + juce::String (width) + "x" + juce::String (height) + ".png");
            destination.deleteFile();
            if (auto stream = destination.createOutputStream())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, *stream);
            }
        }

        editor.setSize (1280, 800);
        const auto retinaImage = editor.createComponentSnapshot (
            editor.getLocalBounds(), true, 2.0f);
        const auto retinaDestination = outputDirectory.getChildFile (
            "ui-retina-1280x800@2x.png");
        retinaDestination.deleteFile();
        if (auto stream = retinaDestination.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (retinaImage, *stream);
        }
    }

    if (auto* reverse = processor.parameters.getParameter (
            stacksampler::parameterID (0, "reverse")))
        reverse->setValueNotifyingHost (1.0f);

    {
        StackSamplerAudioProcessorEditor editor { processor };
        editor.setSize (1280, 800);
        const auto image = editor.createComponentSnapshot (
            editor.getLocalBounds(), true, 1.0f);
        const auto destination = outputDirectory.getChildFile ("ui-reverse-1280x800.png");
        destination.deleteFile();
        if (auto stream = destination.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, *stream);
        }
    }

    while (processor.addLayer())
    {
    }

    {
        StackSamplerAudioProcessorEditor editor { processor };
        editor.setSize (1280, 800);
        const auto image = editor.createComponentSnapshot (
            editor.getLocalBounds(), true, 1.0f);
        const auto destination = outputDirectory.getChildFile (
            "ui-32-layers-1280x800.png");
        destination.deleteFile();
        if (auto stream = destination.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, *stream);
        }
    }

    for (const auto bank : processor.getActiveLayers())
        processor.removeLayer (bank);

    {
        StackSamplerAudioProcessorEditor editor { processor };
        editor.setSize (1280, 800);
        const auto image = editor.createComponentSnapshot (
            editor.getLocalBounds(), true, 1.0f);
        const auto destination = outputDirectory.getChildFile (
            "ui-empty-1280x800.png");
        destination.deleteFile();
        if (auto stream = destination.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, *stream);
        }
    }

    sampleDirectory.deleteRecursively();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TestContext context;
    testParameters (context);
    testWaveformMapping (context);
    testQuickModes (context);
    testEngine (context);
    testClickSafeTransitions (context);
    testProcessorLayerLifecycle (context);
    testSampleLoading (context);
    testStateRoundTrip (context);
    testEditorLifecycle (context);
    renderEditorSnapshots();

    std::cout << context.checks << " checks, " << context.failures
              << " failures\n";
    return context.failures == 0 ? 0 : 1;
}
