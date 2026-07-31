#include "StackSamplerCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace stacksampler
{
static_assert (std::atomic<const SampleData*>::is_always_lock_free);
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

namespace
{
constexpr double rootMidiNote = 60.0;
constexpr float smoothingTimeSeconds = 0.015f;
constexpr float audibilitySmoothingTimeSeconds = 0.005f;
constexpr int antiClickFadeSamples = 32;
constexpr float highPassBypassBoundaryHz = 20.01f;
constexpr float lowPassBypassBoundaryHz = 19999.0f;

int millisecondsToSamples (float milliseconds, double sampleRate) noexcept
{
    const auto samples = std::round (juce::jmax (0.0f, milliseconds)
                                     * 0.001 * sampleRate);
    return static_cast<int> (juce::jlimit (
        0.0,
        static_cast<double> (std::numeric_limits<int>::max()),
        samples));
}

float bipolarRandom (juce::Random& random) noexcept
{
    return random.nextFloat() * 2.0f - 1.0f;
}

float onePoleCoefficient (float cutoff, double sampleRate) noexcept
{
    const auto maximumCutoff = static_cast<float> (sampleRate * 0.49);
    const auto limitedCutoff = juce::jlimit (1.0f, maximumCutoff, cutoff);
    return 1.0f
         - std::exp (static_cast<float> (-juce::MathConstants<double>::twoPi
                                         * limitedCutoff / sampleRate));
}
}

void LayerEngine::prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    outputSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);
    scratch.setSize (2, preparedBlockSize, false, false, true);
    scratch.clear();

    appliedSampleGeneration = sampleGeneration.load (std::memory_order_acquire);
    blockSample = publishedSample.load (std::memory_order_acquire);
    appliedResetGeneration = resetGeneration.load (std::memory_order_acquire);
    clearVoices();
    clearFilterState();
    consumedSampleGeneration.store (appliedSampleGeneration,
                                    std::memory_order_release);
    nextVoiceAge = 1;

    const auto initialise = [this] (auto& value, float initial)
    {
        value.reset (outputSampleRate, smoothingTimeSeconds);
        value.setCurrentAndTargetValue (initial);
    };

    initialise (gain, 1.0f);
    initialise (volume, 1.0f);
    initialise (pan, 0.0f);
    initialise (width, 1.0f);
    initialise (drive, 0.0f);
    initialise (saturation, 0.0f);
    initialise (highPassCoefficient, 1.0f);
    initialise (lowPassCoefficient, 1.0f);
    initialise (highPassMix, 0.0f);
    initialise (lowPassMix, 0.0f);
    audibility.reset (outputSampleRate, audibilitySmoothingTimeSeconds);
    audibility.setCurrentAndTargetValue (1.0f);
    parameterTargetsInitialised = false;
    audibilityTargetInitialised = false;
}

void LayerEngine::reset()
{
    resetGeneration.fetch_add (1, std::memory_order_release);
}

void LayerEngine::releaseResources() noexcept
{
    appliedSampleGeneration = sampleGeneration.load (std::memory_order_acquire);
    blockSample = publishedSample.load (std::memory_order_acquire);
    appliedResetGeneration = resetGeneration.load (std::memory_order_acquire);
    clearVoices();
    clearFilterState();
    consumedSampleGeneration.store (appliedSampleGeneration,
                                    std::memory_order_release);
    parameterTargetsInitialised = false;
    audibilityTargetInitialised = false;
}

void LayerEngine::setSample (std::shared_ptr<const SampleData> newSample)
{
    const juce::ScopedLock lock (sampleOwnershipLock);

    if (ownedSample == newSample)
        return;

    const auto nextGeneration
        = sampleGeneration.load (std::memory_order_relaxed) + 1;

    if (ownedSample != nullptr)
        retiredSamples.push_back ({ nextGeneration, std::move (ownedSample) });

    ownedSample = std::move (newSample);
    publishedSample.store (ownedSample.get(), std::memory_order_release);
    sampleGeneration.store (nextGeneration, std::memory_order_release);
}

std::shared_ptr<const SampleData> LayerEngine::getSample() const
{
    const juce::ScopedLock lock (sampleOwnershipLock);
    return ownedSample;
}

void LayerEngine::collectRetiredSamples()
{
    const auto consumed
        = consumedSampleGeneration.load (std::memory_order_acquire);
    const juce::ScopedLock lock (sampleOwnershipLock);
    retiredSamples.erase (
        std::remove_if (retiredSamples.begin(),
                        retiredSamples.end(),
                        [consumed] (const auto& retired)
                        {
                            return retired.safeAfterGeneration <= consumed;
                        }),
        retiredSamples.end());
}

void LayerEngine::consumeControlRequests()
{
    const auto nextSampleGeneration
        = sampleGeneration.load (std::memory_order_acquire);
    const auto nextResetGeneration
        = resetGeneration.load (std::memory_order_acquire);

    if (nextSampleGeneration == appliedSampleGeneration
        && nextResetGeneration == appliedResetGeneration)
        return;

    if (nextSampleGeneration != appliedSampleGeneration)
    {
        blockSample = publishedSample.load (std::memory_order_acquire);
        appliedSampleGeneration = nextSampleGeneration;
    }

    appliedResetGeneration = nextResetGeneration;
    clearVoices();
    clearFilterState();
    audibilityTargetInitialised = false;

    if (nextSampleGeneration != consumedSampleGeneration.load (
            std::memory_order_relaxed))
        consumedSampleGeneration.store (nextSampleGeneration,
                                        std::memory_order_release);
}

void LayerEngine::clearVoices() noexcept
{
    for (auto& voice : voices)
        voice.stop();
}

void LayerEngine::clearFilterState() noexcept
{
    highPassState.fill (0.0f);
    lowPassState.fill (0.0f);
}

void LayerEngine::updateTargets (const LayerRenderParameters& parameters) noexcept
{
    gain.setTargetValue (juce::Decibels::decibelsToGain (parameters.gainDb));
    volume.setTargetValue (juce::Decibels::decibelsToGain (parameters.volumeDb));
    pan.setTargetValue (juce::jlimit (-1.0f, 1.0f, parameters.pan));
    width.setTargetValue (juce::jlimit (0.0f, 2.0f, parameters.stereoWidth));
    drive.setTargetValue (juce::jlimit (0.0f, 24.0f, parameters.driveDb));
    saturation.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.saturation));
}

void LayerEngine::updateFilterTargets (
    const LayerRenderParameters& parameters) noexcept
{
    highPassCoefficient.setTargetValue (
        onePoleCoefficient (parameters.highPassHz, outputSampleRate));
    lowPassCoefficient.setTargetValue (
        onePoleCoefficient (parameters.lowPassHz, outputSampleRate));
    highPassMix.setTargetValue (
        parameters.highPassHz > highPassBypassBoundaryHz ? 1.0f : 0.0f);
    lowPassMix.setTargetValue (
        parameters.lowPassHz < lowPassBypassBoundaryHz ? 1.0f : 0.0f);
}

void LayerEngine::beginBlock (const LayerRenderParameters& parameters)
{
    consumeControlRequests();

    if (! parameterTargetsInitialised)
    {
        updateTargets (parameters);
        updateFilterTargets (parameters);
        const auto snapToTarget = [] (auto& value)
        {
            value.setCurrentAndTargetValue (value.getTargetValue());
        };

        snapToTarget (gain);
        snapToTarget (volume);
        snapToTarget (pan);
        snapToTarget (width);
        snapToTarget (drive);
        snapToTarget (saturation);
        snapToTarget (highPassCoefficient);
        snapToTarget (lowPassCoefficient);
        snapToTarget (highPassMix);
        snapToTarget (lowPassMix);
        parameterTargetsInitialised = true;
    }
    else
    {
        updateTargets (parameters);
        updateFilterTargets (parameters);
    }
}

void LayerEngine::trigger (int midiNote,
                           float velocityValue,
                           bool humanize,
                           juce::Random& random,
                           const LayerRenderParameters& parameters)
{
    if (blockSample == nullptr
        || blockSample->audio.getNumSamples() <= 0
        || blockSample->audio.getNumChannels() <= 0
        || blockSample->sampleRate <= 0.0)
        return;

    auto* selectedVoice = static_cast<Voice*> (nullptr);

    for (auto& voice : voices)
    {
        if (! voice.isActive())
        {
            selectedVoice = &voice;
            break;
        }
    }

    if (selectedVoice == nullptr)
    {
        selectedVoice = &*std::min_element (
            voices.begin(),
            voices.end(),
            [] (const auto& left, const auto& right)
            {
                return left.age < right.age;
            });
    }

    selectedVoice->stop();
    selectedVoice->sample = blockSample;

    const auto sampleCount = blockSample->audio.getNumSamples();
    const auto normalisedStart = juce::jlimit (0.0f, 0.99f, parameters.start);
    const auto normalisedEnd = juce::jlimit (0.01f, 1.0f, parameters.end);
    auto regionStart = juce::jlimit (
        0,
        sampleCount - 1,
        static_cast<int> (std::floor (static_cast<double> (normalisedStart)
                                      * sampleCount)));
    auto regionEnd = juce::jlimit (
        regionStart + 1,
        sampleCount,
        static_cast<int> (std::ceil (static_cast<double> (normalisedEnd)
                                     * sampleCount)));

    if (regionEnd <= regionStart)
    {
        regionStart = juce::jlimit (0, sampleCount - 1, regionStart);
        regionEnd = juce::jmin (sampleCount, regionStart + 1);
    }

    auto humanFineCents = 0.0f;
    auto sourceJitter = 0;
    selectedVoice->delaySamples = 0;
    selectedVoice->humanGain = 1.0f;
    selectedVoice->humanPan = 0.0f;

    if (humanize)
    {
        humanFineCents = bipolarRandom (random) * 4.0f;
        sourceJitter = static_cast<int> (
            std::round (bipolarRandom (random) * blockSample->sampleRate * 0.003));
        selectedVoice->delaySamples = static_cast<int> (
            std::round (random.nextFloat() * outputSampleRate * 0.004));
        selectedVoice->humanGain = juce::Decibels::decibelsToGain (
            bipolarRandom (random) * 0.75f);
        selectedVoice->humanPan = bipolarRandom (random) * 0.02f;
    }

    selectedVoice->regionStart = regionStart;
    selectedVoice->regionEnd = regionEnd;
    selectedVoice->reverse = parameters.reverse;
    const auto initialPosition = parameters.reverse ? regionEnd - 1 : regionStart;
    selectedVoice->sourcePosition = static_cast<double> (juce::jlimit (
        regionStart,
        regionEnd - 1,
        initialPosition + sourceJitter));

    const auto semitones = static_cast<double> (midiNote) - rootMidiNote
                         + parameters.pitchSemitones
                         + (parameters.fineTuneCents + humanFineCents) / 100.0;
    selectedVoice->sourceStep
        = blockSample->sampleRate / outputSampleRate
        * std::exp2 (semitones / 12.0);
    selectedVoice->midiNote = juce::jlimit (0, 127, midiNote);
    selectedVoice->velocity = juce::jlimit (0.0f, 1.0f, velocityValue);
    selectedVoice->attackSamples
        = millisecondsToSamples (parameters.attackMs, outputSampleRate);
    selectedVoice->decaySamples = juce::jmax (
        1,
        millisecondsToSamples (parameters.decayMs, outputSampleRate));
    selectedVoice->releaseSamples = juce::jmax (
        1,
        millisecondsToSamples (parameters.releaseMs, outputSampleRate));
    selectedVoice->stageSample = 0;
    selectedVoice->envelopeLevel = selectedVoice->attackSamples > 0 ? 0.0f : 1.0f;
    selectedVoice->releaseStartLevel = 0.0f;
    selectedVoice->stage = selectedVoice->attackSamples > 0
                               ? EnvelopeStage::attack
                               : EnvelopeStage::decay;
    selectedVoice->transient = juce::jlimit (-1.0f, 1.0f, parameters.transient);
    selectedVoice->tail = juce::jlimit (-1.0f, 1.0f, parameters.tail);
    selectedVoice->age = nextVoiceAge++;
}

void LayerEngine::release (int midiNote)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.midiNote != midiNote
            || voice.stage == EnvelopeStage::release)
            continue;

        voice.releaseStartLevel = voice.envelopeLevel;
        voice.stageSample = 0;
        voice.stage = EnvelopeStage::release;
    }
}

void LayerEngine::stopAllVoices() noexcept
{
    clearVoices();
    clearFilterState();
}

float LayerEngine::nextEnvelopeSample (Voice& voice) noexcept
{
    switch (voice.stage)
    {
        case EnvelopeStage::attack:
        {
            if (voice.attackSamples <= 0)
            {
                voice.stage = EnvelopeStage::decay;
                voice.stageSample = 0;
                voice.envelopeLevel = 1.0f;
                return voice.envelopeLevel;
            }

            voice.envelopeLevel = juce::jlimit (
                0.0f,
                1.0f,
                static_cast<float> (voice.stageSample + 1)
                    / static_cast<float> (voice.attackSamples));

            if (++voice.stageSample >= voice.attackSamples)
            {
                voice.stage = EnvelopeStage::decay;
                voice.stageSample = 0;
                voice.envelopeLevel = 1.0f;
            }

            return voice.envelopeLevel;
        }

        case EnvelopeStage::decay:
        {
            voice.envelopeLevel = juce::jmax (
                0.0f,
                1.0f - static_cast<float> (voice.stageSample)
                         / static_cast<float> (voice.decaySamples));

            if (++voice.stageSample >= voice.decaySamples)
                voice.stage = EnvelopeStage::stopped;

            return voice.envelopeLevel;
        }

        case EnvelopeStage::release:
        {
            voice.envelopeLevel = juce::jmax (
                0.0f,
                voice.releaseStartLevel
                    * (1.0f - static_cast<float> (voice.stageSample)
                                  / static_cast<float> (voice.releaseSamples)));

            if (++voice.stageSample >= voice.releaseSamples)
                voice.stage = EnvelopeStage::stopped;

            return voice.envelopeLevel;
        }

        case EnvelopeStage::stopped:
        default:
            return 0.0f;
    }
}

float LayerEngine::readSample (const Voice& voice, int channel) const noexcept
{
    const auto& audio = voice.sample->audio;
    const auto sourceChannel = juce::jlimit (0, audio.getNumChannels() - 1, channel);
    const auto position = juce::jlimit (
        static_cast<double> (voice.regionStart),
        static_cast<double> (voice.regionEnd - 1),
        voice.sourcePosition);
    const auto first = juce::jlimit (
        voice.regionStart,
        voice.regionEnd - 1,
        static_cast<int> (std::floor (position)));
    const auto second = juce::jmin (voice.regionEnd - 1, first + 1);
    const auto fraction = static_cast<float> (position - first);
    const auto* samples = audio.getReadPointer (sourceChannel);
    return samples[first] + (samples[second] - samples[first]) * fraction;
}

void LayerEngine::panGains (float panValue, float& left, float& right) noexcept
{
    const auto clamped = juce::jlimit (-1.0f, 1.0f, panValue);
    left = clamped <= 0.0f
               ? 1.0f
               : std::cos (clamped * juce::MathConstants<float>::halfPi);
    right = clamped >= 0.0f
                ? 1.0f
                : std::cos (-clamped * juce::MathConstants<float>::halfPi);
}

void LayerEngine::renderVoice (Voice& voice, int numSamples)
{
    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        if (! voice.isActive())
            break;

        if (voice.delaySamples > 0)
        {
            --voice.delaySamples;
            continue;
        }

        const auto envelope = nextEnvelopeSample (voice);
        if (envelope <= 0.0f)
        {
            voice.stop();
            break;
        }

        const auto playedFrames = voice.reverse
                                      ? static_cast<double> (voice.regionEnd - 1)
                                            - voice.sourcePosition
                                      : voice.sourcePosition
                                            - static_cast<double> (voice.regionStart);
        const auto regionLength = juce::jmax (1, voice.regionEnd - voice.regionStart);
        const auto progress = juce::jlimit (
            0.0f,
            1.0f,
            static_cast<float> (playedFrames / regionLength));
        const auto transientLength = voice.sample->sampleRate * 0.03;
        auto transientDb = 0.0f;

        if (playedFrames < transientLength && transientLength > 0.0)
            transientDb = 12.0f * voice.transient
                        * static_cast<float> (1.0 - playedFrames / transientLength);

        const auto tailDb = 12.0f * voice.tail * progress * progress;
        const auto remainingFrames = voice.reverse
                                         ? voice.sourcePosition - voice.regionStart
                                         : voice.regionEnd - voice.sourcePosition;
        const auto fadeLengthInSourceFrames
            = juce::jmax (voice.sourceStep,
                          voice.sourceStep * antiClickFadeSamples);
        const auto endFade = juce::jlimit (
            0.0f,
            1.0f,
            static_cast<float> (remainingFrames / fadeLengthInSourceFrames));
        const auto shapedGain = voice.velocity * voice.humanGain * envelope
                              * endFade
                              * juce::Decibels::decibelsToGain (transientDb + tailDb);
        auto left = readSample (voice, 0) * shapedGain;
        auto right = readSample (
            voice,
            voice.sample->audio.getNumChannels() > 1 ? 1 : 0) * shapedGain;
        float humanLeft = 1.0f;
        float humanRight = 1.0f;
        panGains (voice.humanPan, humanLeft, humanRight);
        left *= humanLeft;
        right *= humanRight;
        scratch.addSample (0, sampleIndex, left);
        scratch.addSample (1, sampleIndex, right);

        voice.sourcePosition += voice.reverse ? -voice.sourceStep : voice.sourceStep;
        const auto outsideRegion = voice.reverse
                                       ? voice.sourcePosition < voice.regionStart
                                       : voice.sourcePosition >= voice.regionEnd;

        if (outsideRegion)
            voice.stop();
    }
}

void LayerEngine::renderChunk (juce::AudioBuffer<float>& destination,
                               int startSample,
                               int numSamples,
                               const LayerRenderParameters& parameters)
{
    const auto audibilityTarget = parameters.mute ? 0.0f : 1.0f;
    if (! audibilityTargetInitialised)
    {
        audibility.setCurrentAndTargetValue (audibilityTarget);
        audibilityTargetInitialised = true;
    }
    else
    {
        audibility.setTargetValue (audibilityTarget);
    }

    scratch.clear (0, numSamples);

    for (auto& voice : voices)
        renderVoice (voice, numSamples);

    updateTargets (parameters);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        auto left = scratch.getSample (0, sampleIndex);
        auto right = scratch.getSample (1, sampleIndex);
        const auto inputGain = gain.getNextValue();
        left *= inputGain;
        right *= inputGain;

        const auto highPassAmount = highPassMix.getNextValue();
        const auto highPassCurrentCoefficient
            = highPassCoefficient.getNextValue();
        highPassState[0] += highPassCurrentCoefficient
                          * (left - highPassState[0]);
        highPassState[1] += highPassCurrentCoefficient
                          * (right - highPassState[1]);
        left -= highPassState[0] * highPassAmount;
        right -= highPassState[1] * highPassAmount;

        const auto lowPassAmount = lowPassMix.getNextValue();
        const auto lowPassCurrentCoefficient = lowPassCoefficient.getNextValue();
        lowPassState[0] += lowPassCurrentCoefficient * (left - lowPassState[0]);
        lowPassState[1] += lowPassCurrentCoefficient * (right - lowPassState[1]);
        left += (lowPassState[0] - left) * lowPassAmount;
        right += (lowPassState[1] - right) * lowPassAmount;

        const auto driveDb = drive.getNextValue();
        const auto saturationAmount = saturation.getNextValue();

        if (driveDb > 0.0001f || saturationAmount > 0.0001f)
        {
            const auto driveGain = juce::Decibels::decibelsToGain (driveDb);
            const auto shape = 1.0f + 4.0f * saturationAmount;
            const auto normalisation = std::tanh (shape);
            const auto mix = juce::jlimit (
                0.0f,
                1.0f,
                juce::jmax (saturationAmount, driveDb / 24.0f));
            const auto saturate = [=] (float input)
            {
                return std::tanh (input * driveGain * shape) / normalisation;
            };
            left += (saturate (left) - left) * mix;
            right += (saturate (right) - right) * mix;
        }

        const auto stereoWidth = width.getNextValue();
        const auto mid = (left + right) * 0.5f;
        const auto side = (left - right) * 0.5f * stereoWidth;
        left = mid + side;
        right = mid - side;

        float panLeft = 1.0f;
        float panRight = 1.0f;
        panGains (pan.getNextValue(), panLeft, panRight);
        const auto outputGain = volume.getNextValue();
        const auto audibilityGain = audibility.getNextValue();
        left *= panLeft * outputGain * audibilityGain;
        right *= panRight * outputGain * audibilityGain;

        if (destination.getNumChannels() == 1)
        {
            destination.addSample (0,
                                   startSample + sampleIndex,
                                   (left + right) * 0.5f);
        }
        else
        {
            destination.addSample (0, startSample + sampleIndex, left);
            destination.addSample (1, startSample + sampleIndex, right);
        }
    }
}

void LayerEngine::render (juce::AudioBuffer<float>& destination,
                          int startSample,
                          int numSamples,
                          const LayerRenderParameters& parameters)
{
    if (numSamples <= 0 || destination.getNumChannels() <= 0
        || startSample >= destination.getNumSamples())
        return;

    auto destinationOffset = juce::jmax (0, startSample);
    auto remaining = juce::jmin (
        numSamples - juce::jmax (0, -startSample),
        destination.getNumSamples() - destinationOffset);

    while (remaining > 0)
    {
        const auto chunkSize = juce::jmin (remaining, preparedBlockSize);
        renderChunk (destination,
                     destinationOffset,
                     chunkSize,
                     parameters);
        destinationOffset += chunkSize;
        remaining -= chunkSize;
    }
}
}
