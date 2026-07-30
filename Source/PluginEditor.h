#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>

class StackSamplerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::ChangeListener
{
public:
    explicit StackSamplerAudioProcessorEditor (StackSamplerAudioProcessor&);
    ~StackSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class StackSamplerLookAndFeel;
    class LayerList;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildLayers();

    StackSamplerAudioProcessor& ownerProcessor;
    std::unique_ptr<StackSamplerLookAndFeel> stackLookAndFeel;
    std::unique_ptr<LayerList> layerList;

    juce::Viewport viewport;
    juce::TextButton addLayerButton { "+ Add Layer" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::HyperlinkButton sourceLink {
        "© 2026 Gustavo · no warranty · share under AGPLv3 · source/licence",
        juce::URL { "https://github.com/figtracer/stacksampler/blob/main/LICENSE" }
    };
    juce::Label layerCountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        humanizeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackSamplerAudioProcessorEditor)
};
