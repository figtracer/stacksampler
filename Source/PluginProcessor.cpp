#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int waveformBinCount = 320;
constexpr double maximumTailSeconds = 33.0;

constexpr std::array<const char*, 20> layerParameterSuffixes
{{
    "volume",
    "pan",
    "pitch",
    "fine",
    "start",
    "end",
    "reverse",
    "gain",
    "attack",
    "decay",
    "release",
    "lowpass",
    "highpass",
    "drive",
    "saturation",
    "width",
    "transient",
    "tail",
    "mute",
    "solo"
}};

bool hasSupportedExtension (const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif" || extension == ".aiff"
        || extension == ".flac";
}
}

StackSamplerAudioProcessor::StackSamplerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output",
                                                    juce::AudioChannelSet::stereo(),
                                                    true)),
      parameters (*this,
                  nullptr,
                  juce::Identifier { "STACKSAMPLER_PARAMETERS" },
                  stacksampler::createParameterLayout())
{
    formatManager.registerBasicFormats();

    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
        parameterRefs[static_cast<std::size_t> (bank)]
            = std::make_unique<stacksampler::LayerParameterRefs> (parameters, bank);

    humanizeRandom.setSeed (humanizeSeed.load (std::memory_order_relaxed));
    appliedHumanizeSeedGeneration
        = humanizeSeedGeneration.load (std::memory_order_relaxed);
    resetToInitialLayers();
    startTimer (500);
}

StackSamplerAudioProcessor::~StackSamplerAudioProcessor()
{
    stopTimer();

    for (auto& engine : layerEngines)
        engine.collectRetiredSamples();
}

const juce::String StackSamplerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StackSamplerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool StackSamplerAudioProcessor::producesMidi() const
{
    return false;
}

bool StackSamplerAudioProcessor::isMidiEffect() const
{
    return false;
}

double StackSamplerAudioProcessor::getTailLengthSeconds() const
{
    return maximumTailSeconds;
}

int StackSamplerAudioProcessor::getNumPrograms()
{
    return 1;
}

int StackSamplerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void StackSamplerAudioProcessor::setCurrentProgram (int)
{
}

const juce::String StackSamplerAudioProcessor::getProgramName (int)
{
    return {};
}

void StackSamplerAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void StackSamplerAudioProcessor::prepareToPlay (double sampleRate,
                                                int maximumExpectedSamplesPerBlock)
{
    humanizeRandom.setSeed (humanizeSeed.load (std::memory_order_acquire));
    appliedHumanizeSeedGeneration
        = humanizeSeedGeneration.load (std::memory_order_acquire);

    for (auto& engine : layerEngines)
        engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
}

void StackSamplerAudioProcessor::releaseResources()
{
    for (auto& engine : layerEngines)
        engine.releaseResources();
}

bool StackSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StackSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& output,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    output.clear();
    applyPendingHumanizeSeed();

    std::array<stacksampler::LayerRenderParameters, stacksampler::kMaxLayers> snapshots;
    bool anyLayerSoloed = false;

    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
    {
        const auto index = static_cast<std::size_t> (bank);
        snapshots[index] = parameterRefs[index]->snapshot();

        layerEngines[index].beginBlock (snapshots[index]);

        if (layerMetadata[index].active.load (std::memory_order_acquire))
            anyLayerSoloed = anyLayerSoloed || parameterRefs[index]->isSoloed();
    }

    auto cursor = 0;

    for (const auto metadata : midiMessages)
    {
        const auto eventPosition = juce::jlimit (cursor,
                                                output.getNumSamples(),
                                                metadata.samplePosition);
        renderSegment (output,
                       cursor,
                       eventPosition - cursor,
                       anyLayerSoloed,
                       snapshots);

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            triggerNote (message.getNoteNumber(),
                         message.getFloatVelocity(),
                         isHumanizeEnabled(),
                         snapshots);
        }
        else if (message.isNoteOff())
        {
            releaseNote (message.getNoteNumber());
        }
        else if (message.isAllSoundOff())
        {
            for (auto& engine : layerEngines)
                engine.stopAllVoices();
        }
        else if (message.isAllNotesOff())
        {
            for (int note = 0; note < 128; ++note)
                releaseNote (note);
        }

        cursor = eventPosition;
    }

    renderSegment (output,
                   cursor,
                   output.getNumSamples() - cursor,
                   anyLayerSoloed,
                   snapshots);
    midiMessages.clear();
}

void StackSamplerAudioProcessor::renderSegment (
    juce::AudioBuffer<float>& output,
    int startSample,
    int numSamples,
    bool anyLayerSoloed,
    const std::array<stacksampler::LayerRenderParameters, stacksampler::kMaxLayers>& snapshots)
{
    if (numSamples <= 0)
        return;

    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
    {
        const auto index = static_cast<std::size_t> (bank);

        if (! layerMetadata[index].active.load (std::memory_order_acquire))
            continue;

        auto renderParameters = snapshots[index];
        renderParameters.mute = parameterRefs[index]->isMuted()
                             || (anyLayerSoloed && ! parameterRefs[index]->isSoloed());
        layerEngines[index].render (output,
                                    startSample,
                                    numSamples,
                                    renderParameters);
    }
}

void StackSamplerAudioProcessor::triggerNote (
    int midiNote,
    float velocity,
    bool humanize,
    const std::array<stacksampler::LayerRenderParameters, stacksampler::kMaxLayers>& snapshots)
{
    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
    {
        const auto index = static_cast<std::size_t> (bank);

        if (layerMetadata[index].active.load (std::memory_order_acquire))
            layerEngines[index].trigger (midiNote,
                                         velocity,
                                         humanize,
                                         humanizeRandom,
                                         snapshots[index]);
    }
}

void StackSamplerAudioProcessor::releaseNote (int midiNote)
{
    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
    {
        const auto index = static_cast<std::size_t> (bank);

        if (layerMetadata[index].active.load (std::memory_order_acquire))
            layerEngines[index].release (midiNote);
    }
}

bool StackSamplerAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StackSamplerAudioProcessor::createEditor()
{
    return new StackSamplerAudioProcessorEditor (*this);
}

std::vector<int> StackSamplerAudioProcessor::getActiveLayers() const
{
    const juce::ScopedLock lock (metadataLock);
    return layerOrder;
}

StackSamplerAudioProcessor::LayerDisplayState
StackSamplerAudioProcessor::getLayerDisplayState (int bank) const
{
    LayerDisplayState result;
    result.bank = bank;

    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        return result;

    {
        const juce::ScopedLock lock (metadataLock);
        const auto& metadata = layerMetadata[static_cast<std::size_t> (bank)];
        result.name = metadata.name;
        result.filePath = metadata.filePath;
        result.status = metadata.status;
        result.quickMode = metadata.quickMode;
    }

    result.hasSample
        = layerEngines[static_cast<std::size_t> (bank)].getSample() != nullptr;
    return result;
}

std::shared_ptr<const stacksampler::SampleData>
StackSamplerAudioProcessor::getSampleData (int bank) const
{
    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        return {};

    return layerEngines[static_cast<std::size_t> (bank)].getSample();
}

bool StackSamplerAudioProcessor::addLayer()
{
    auto freeBank = -1;

    for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
    {
        if (! layerMetadata[static_cast<std::size_t> (bank)].active.load (
                std::memory_order_acquire))
        {
            freeBank = bank;
            break;
        }
    }

    if (freeBank < 0)
        return false;

    const auto index = static_cast<std::size_t> (freeBank);
    layerEngines[index].setSample ({});
    layerEngines[index].reset();
    resetLayerParameters (freeBank);

    {
        const juce::ScopedLock lock (metadataLock);
        auto& metadata = layerMetadata[index];
        metadata.name = "Empty layer";
        metadata.filePath.clear();
        metadata.status = "Drop WAV, AIFF or FLAC";
        metadata.quickMode = stacksampler::QuickMode::none;
        layerOrder.push_back (freeBank);
    }

    layerMetadata[index].active.store (true, std::memory_order_release);
    sendChangeMessage();
    return true;
}

void StackSamplerAudioProcessor::removeLayer (int bank)
{
    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        return;

    const auto index = static_cast<std::size_t> (bank);
    layerMetadata[index].active.store (false, std::memory_order_release);
    layerEngines[index].setSample ({});
    layerEngines[index].reset();
    resetLayerParameters (bank);

    {
        const juce::ScopedLock lock (metadataLock);
        layerOrder.erase (std::remove (layerOrder.begin(), layerOrder.end(), bank),
                          layerOrder.end());
        auto& metadata = layerMetadata[index];
        metadata.name.clear();
        metadata.filePath.clear();
        metadata.status.clear();
        metadata.quickMode = stacksampler::QuickMode::none;
    }

    sendChangeMessage();
}

bool StackSamplerAudioProcessor::loadSample (int bank,
                                             const juce::File& file,
                                             juce::String* errorMessage)
{
    const auto fail = [&] (const juce::String& message)
    {
        if (errorMessage != nullptr)
            *errorMessage = message;

        if (juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        {
            const juce::ScopedLock lock (metadataLock);
            layerMetadata[static_cast<std::size_t> (bank)].status = message;
        }

        sendChangeMessage();
        return false;
    };

    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers)
        || ! layerMetadata[static_cast<std::size_t> (bank)].active.load (
            std::memory_order_acquire))
        return fail ("Layer is not active");

    if (! file.existsAsFile())
        return fail ("Sample file is missing");

    if (! hasSupportedExtension (file))
        return fail ("Use WAV, AIFF or FLAC");

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return fail ("Could not decode this audio file");

    if (reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
        return fail ("Sample is empty or too long");

    auto sample = std::make_shared<stacksampler::SampleData>();
    const auto channelCount = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
    const auto sampleCount = static_cast<int> (reader->lengthInSamples);
    sample->audio.setSize (channelCount, sampleCount);
    sample->sampleRate = reader->sampleRate;

    if (! reader->read (&sample->audio,
                        0,
                        sampleCount,
                        0,
                        true,
                        channelCount > 1))
        return fail ("Could not read this audio file");

    const auto bins = juce::jmin (waveformBinCount, sampleCount);
    sample->waveformMin.resize (static_cast<std::size_t> (bins));
    sample->waveformMax.resize (static_cast<std::size_t> (bins));

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto firstSample = static_cast<int> (
            static_cast<juce::int64> (bin) * sampleCount / bins);
        const auto lastSample = juce::jmax (firstSample + 1,
                                           static_cast<int> (
                                               static_cast<juce::int64> (bin + 1)
                                               * sampleCount / bins));
        auto minimum = 1.0f;
        auto maximum = -1.0f;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto range = sample->audio.findMinMax (channel,
                                                        firstSample,
                                                        lastSample - firstSample);
            minimum = juce::jmin (minimum, range.getStart());
            maximum = juce::jmax (maximum, range.getEnd());
        }

        sample->waveformMin[static_cast<std::size_t> (bin)] = minimum;
        sample->waveformMax[static_cast<std::size_t> (bin)] = maximum;
    }

    layerEngines[static_cast<std::size_t> (bank)].setSample (sample);

    {
        const juce::ScopedLock lock (metadataLock);
        auto& metadata = layerMetadata[static_cast<std::size_t> (bank)];
        metadata.name = file.getFileNameWithoutExtension();
        metadata.filePath = file.getFullPathName();
        metadata.status = juce::String (reader->sampleRate / 1000.0, 1)
                        + " kHz · "
                        + juce::String (channelCount == 1 ? "mono" : "stereo");
    }

    if (errorMessage != nullptr)
        errorMessage->clear();

    sendChangeMessage();
    return true;
}

void StackSamplerAudioProcessor::setParameterValue (const juce::String& parameterId,
                                                     float actualValue)
{
    if (auto* parameter = parameters.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (actualValue));
        parameter->endChangeGesture();
    }
}

void StackSamplerAudioProcessor::resetLayerParameters (int bank)
{
    for (const auto* suffix : layerParameterSuffixes)
    {
        if (auto* parameter
            = parameters.getParameter (stacksampler::parameterID (bank, suffix)))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    }
}

void StackSamplerAudioProcessor::applyPendingHumanizeSeed()
{
    const auto generation
        = humanizeSeedGeneration.load (std::memory_order_acquire);

    if (generation == appliedHumanizeSeedGeneration)
        return;

    humanizeRandom.setSeed (humanizeSeed.load (std::memory_order_relaxed));
    appliedHumanizeSeedGeneration = generation;
}

void StackSamplerAudioProcessor::timerCallback()
{
    for (auto& engine : layerEngines)
        engine.collectRetiredSamples();
}

void StackSamplerAudioProcessor::applyQuickMode (int bank,
                                                 stacksampler::QuickMode mode)
{
    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        return;

    const auto settings = stacksampler::quickModeSettings (mode);
    setParameterValue (stacksampler::parameterID (bank, "attack"), settings.attackMs);
    setParameterValue (stacksampler::parameterID (bank, "decay"), settings.decayMs);
    setParameterValue (stacksampler::parameterID (bank, "release"), settings.releaseMs);
    setParameterValue (stacksampler::parameterID (bank, "lowpass"), settings.lowPassHz);
    setParameterValue (stacksampler::parameterID (bank, "highpass"), settings.highPassHz);
    setParameterValue (stacksampler::parameterID (bank, "drive"), settings.driveDb);
    setParameterValue (stacksampler::parameterID (bank, "saturation"), settings.saturation);
    setParameterValue (stacksampler::parameterID (bank, "width"), settings.width);
    setParameterValue (stacksampler::parameterID (bank, "transient"), settings.transient);
    setParameterValue (stacksampler::parameterID (bank, "tail"), settings.tail);

    {
        const juce::ScopedLock lock (metadataLock);
        layerMetadata[static_cast<std::size_t> (bank)].quickMode = mode;
    }

    sendChangeMessage();
}

void StackSamplerAudioProcessor::randomizeLayer (int bank)
{
    if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers))
        return;

    auto& random = juce::Random::getSystemRandom();
    const auto readActual = [&] (const char* suffix)
    {
        auto* parameter = parameters.getParameter (stacksampler::parameterID (bank, suffix));
        return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : 0.0f;
    };
    const auto bipolar = [&random] (float magnitude)
    {
        return (random.nextFloat() * 2.0f - 1.0f) * magnitude;
    };

    const auto pitch = std::round (readActual ("pitch"))
                     + static_cast<float> (random.nextInt (3) - 1);
    setParameterValue (stacksampler::parameterID (bank, "pitch"),
                       juce::jlimit (-24.0f, 24.0f, pitch));
    setParameterValue (stacksampler::parameterID (bank, "fine"),
                       juce::jlimit (-100.0f,
                                    100.0f,
                                    readActual ("fine") + bipolar (6.0f)));

    auto startDelta = 0.005f;
    if (const auto sample = getSampleData (bank))
    {
        if (sample->audio.getNumSamples() > 0)
            startDelta = juce::jmin (
                0.02f,
                static_cast<float> (0.01 * sample->sampleRate
                                    / sample->audio.getNumSamples()));
    }

    setParameterValue (stacksampler::parameterID (bank, "start"),
                       juce::jlimit (0.0f,
                                    0.99f,
                                    readActual ("start") + bipolar (startDelta)));
    setParameterValue (stacksampler::parameterID (bank, "pan"),
                       juce::jlimit (-1.0f,
                                    1.0f,
                                    readActual ("pan") + bipolar (0.08f)));
    setParameterValue (stacksampler::parameterID (bank, "drive"),
                       juce::jlimit (0.0f,
                                    24.0f,
                                    readActual ("drive") + bipolar (1.5f)));
    setParameterValue (
        stacksampler::parameterID (bank, "lowpass"),
        juce::jlimit (20.0f,
                     20000.0f,
                     readActual ("lowpass") * std::pow (2.0f, bipolar (0.15f))));
    setParameterValue (
        stacksampler::parameterID (bank, "highpass"),
        juce::jlimit (20.0f,
                     20000.0f,
                     readActual ("highpass") * std::pow (2.0f, bipolar (0.15f))));
    setParameterValue (stacksampler::parameterID (bank, "width"),
                       juce::jlimit (0.0f,
                                    2.0f,
                                    readActual ("width") + bipolar (0.08f)));
}

bool StackSamplerAudioProcessor::isHumanizeEnabled() const
{
    if (const auto* value = parameters.getRawParameterValue ("humanize"))
        return value->load() >= 0.5f;

    return false;
}

void StackSamplerAudioProcessor::setHumanizeEnabled (bool enabled)
{
    setParameterValue ("humanize", enabled ? 1.0f : 0.0f);
}

void StackSamplerAudioProcessor::resetToInitialLayers()
{
    {
        const juce::ScopedLock lock (metadataLock);
        layerOrder.clear();

        for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
        {
            const auto index = static_cast<std::size_t> (bank);
            layerMetadata[index].active.store (false, std::memory_order_release);
            layerMetadata[index].name.clear();
            layerMetadata[index].filePath.clear();
            layerMetadata[index].status.clear();
            layerMetadata[index].quickMode = stacksampler::QuickMode::none;
            layerEngines[index].setSample ({});
        }

        for (int bank = 0; bank < stacksampler::kInitialLayers; ++bank)
        {
            const auto index = static_cast<std::size_t> (bank);
            layerMetadata[index].name = "Empty layer";
            layerMetadata[index].status = "Drop WAV, AIFF or FLAC";
            layerMetadata[index].active.store (true, std::memory_order_release);
            layerOrder.push_back (bank);
        }
    }
}

void StackSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    state.setProperty ("humanizeSeed",
                       humanizeSeed.load (std::memory_order_acquire),
                       nullptr);

    auto layers = juce::ValueTree { "LAYERS" };

    {
        const juce::ScopedLock lock (metadataLock);

        for (std::size_t order = 0; order < layerOrder.size(); ++order)
        {
            const auto bank = layerOrder[order];
            const auto& metadata = layerMetadata[static_cast<std::size_t> (bank)];
            juce::ValueTree layer { "LAYER" };
            layer.setProperty ("bank", bank, nullptr);
            layer.setProperty ("order", static_cast<int> (order), nullptr);
            layer.setProperty ("name", metadata.name, nullptr);
            layer.setProperty ("filePath", metadata.filePath, nullptr);
            layer.setProperty ("quickMode",
                               static_cast<int> (metadata.quickMode),
                               nullptr);
            layers.addChild (layer, -1, nullptr);
        }
    }

    state.removeChild (state.getChildWithName ("LAYERS"), nullptr);
    state.addChild (layers, -1, nullptr);

    juce::MemoryOutputStream stream (destinationData, false);
    state.writeToStream (stream);
}

void StackSamplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data,
                                               static_cast<std::size_t> (sizeInBytes));
    if (! state.isValid())
        return;

    parameters.replaceState (state);
    const auto restoredHumanizeSeed = static_cast<juce::int64> (
        state.getProperty ("humanizeSeed",
                           humanizeSeed.load (std::memory_order_relaxed)));
    humanizeSeed.store (restoredHumanizeSeed, std::memory_order_relaxed);
    humanizeSeedGeneration.fetch_add (1, std::memory_order_release);

    struct RestoredLayer
    {
        int bank = 0;
        int order = 0;
        juce::String name;
        juce::String filePath;
        stacksampler::QuickMode mode = stacksampler::QuickMode::none;
    };

    std::vector<RestoredLayer> restoredLayers;
    const auto layers = state.getChildWithName ("LAYERS");

    std::array<bool, stacksampler::kMaxLayers> restoredBanks {};

    for (const auto& child : layers)
    {
        const auto bank = static_cast<int> (child.getProperty ("bank", -1));
        if (! juce::isPositiveAndBelow (bank, stacksampler::kMaxLayers)
            || restoredBanks[static_cast<std::size_t> (bank)])
            continue;

        RestoredLayer restored;
        restoredBanks[static_cast<std::size_t> (bank)] = true;
        restored.bank = bank;
        restored.order = static_cast<int> (child.getProperty ("order",
                                                              static_cast<int> (
                                                                  restoredLayers.size())));
        restored.name = child.getProperty ("name").toString();
        restored.filePath = child.getProperty ("filePath").toString();
        const auto mode = juce::jlimit (
            static_cast<int> (stacksampler::QuickMode::none),
            static_cast<int> (stacksampler::QuickMode::eightOhEight),
            static_cast<int> (child.getProperty ("quickMode", 0)));
        restored.mode = static_cast<stacksampler::QuickMode> (mode);
        restoredLayers.push_back (std::move (restored));
    }

    if (restoredLayers.empty())
    {
        resetToInitialLayers();
        sendChangeMessage();
        return;
    }

    std::sort (restoredLayers.begin(),
               restoredLayers.end(),
               [] (const auto& left, const auto& right)
               {
                   return left.order < right.order;
               });

    {
        const juce::ScopedLock lock (metadataLock);
        layerOrder.clear();

        for (int bank = 0; bank < stacksampler::kMaxLayers; ++bank)
        {
            const auto index = static_cast<std::size_t> (bank);
            layerMetadata[index].active.store (false, std::memory_order_release);
            layerMetadata[index].name.clear();
            layerMetadata[index].filePath.clear();
            layerMetadata[index].status.clear();
            layerMetadata[index].quickMode = stacksampler::QuickMode::none;
            layerEngines[index].setSample ({});
        }

        for (const auto& restored : restoredLayers)
        {
            const auto index = static_cast<std::size_t> (restored.bank);
            auto& metadata = layerMetadata[index];
            metadata.name = restored.name.isNotEmpty() ? restored.name : "Empty layer";
            metadata.filePath = restored.filePath;
            metadata.status = restored.filePath.isEmpty() ? "Drop WAV, AIFF or FLAC"
                                                          : "Loading sample";
            metadata.quickMode = restored.mode;
            metadata.active.store (true, std::memory_order_release);
            layerOrder.push_back (restored.bank);
        }
    }

    for (const auto& restored : restoredLayers)
    {
        if (restored.filePath.isNotEmpty())
        {
            juce::String ignoredError;
            loadSample (restored.bank, juce::File { restored.filePath }, &ignoredError);
        }
    }

    sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StackSamplerAudioProcessor();
}
