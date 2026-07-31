#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>
#include <vector>

class StackSamplerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::ChangeListener,
                                               private juce::Timer
{
public:
    explicit StackSamplerAudioProcessorEditor (StackSamplerAudioProcessor&);
    ~StackSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class StackSamplerLookAndFeel;
    class LayerList;
    class LayerInspector;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildLayers();
    void selectLayer (int bank, bool scrollIntoView = false);
    void removeLayer (int bank);

    StackSamplerAudioProcessor& ownerProcessor;
    std::unique_ptr<StackSamplerLookAndFeel> stackLookAndFeel;
    std::unique_ptr<LayerList> layerList;
    std::unique_ptr<LayerInspector> layerInspector;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    juce::Viewport viewport;
    juce::TextButton addLayerButton { "+ Add Layer" };
    juce::ToggleButton humanizeButton { "Humanize" };
    juce::HyperlinkButton sourceLink {
        "(c) 2026 Gustavo | no warranty | share under AGPLv3 | source/licence",
        juce::URL { "https://github.com/figtracer/stacksampler/blob/main/LICENSE" }
    };
    juce::Label layerCountLabel;
    juce::Rectangle<int> rackPanelBounds;
    juce::Rectangle<int> inspectorPanelBounds;
    std::vector<int> displayedLayerOrder;
    int selectedBank = -1;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        humanizeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackSamplerAudioProcessorEditor)
};
