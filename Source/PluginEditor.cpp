#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <utility>

namespace
{
const auto background = juce::Colour::fromRGB (12, 13, 16);
const auto panel = juce::Colour::fromRGB (23, 25, 31);
const auto panelRaised = juce::Colour::fromRGB (30, 33, 40);
const auto outline = juce::Colour::fromRGB (52, 57, 68);
const auto text = juce::Colour::fromRGB (235, 238, 244);
const auto mutedText = juce::Colour::fromRGB (145, 151, 164);
const auto accent = juce::Colour::fromRGB (252, 111, 76);
const auto accentCool = juce::Colour::fromRGB (91, 203, 186);

constexpr int headerHeight = 78;
constexpr int footerHeight = 62;
constexpr int cardHeight = 196;
constexpr int cardGap = 10;

struct KnobDefinition
{
    const char* suffix;
    const char* label;
};

constexpr std::array<KnobDefinition, 17> knobDefinitions
{{
    { "volume", "VOLUME" },
    { "pan", "PAN" },
    { "pitch", "PITCH" },
    { "fine", "FINE" },
    { "start", "START" },
    { "end", "END" },
    { "gain", "GAIN" },
    { "attack", "ATTACK" },
    { "decay", "DECAY" },
    { "release", "RELEASE" },
    { "lowpass", "LOW PASS" },
    { "highpass", "HIGH PASS" },
    { "drive", "DRIVE" },
    { "saturation", "SATURATION" },
    { "width", "WIDTH" },
    { "transient", "TRANSIENT" },
    { "tail", "TAIL" }
}};
}

class StackSamplerAudioProcessorEditor::StackSamplerLookAndFeel final
    : public juce::LookAndFeel_V4
{
public:
    StackSamplerLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, panelRaised);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, panelRaised);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.25f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, background);
    }

    void drawRotarySlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                              static_cast<float> (y),
                                              static_cast<float> (width),
                                              static_cast<float> (height))
                          .reduced (8.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre (diameter, diameter);

        const auto lineWidth = juce::jmax (2.0f, diameter * 0.075f);
        const auto radius = diameter * 0.5f - lineWidth;
        const auto centre = bounds.getCentre();
        const auto angle = rotaryStartAngle
                         + sliderPosition * (rotaryEndAngle - rotaryStartAngle);

        juce::Path track;
        track.addCentredArc (centre.x,
                             centre.y,
                             radius,
                             radius,
                             0.0f,
                             rotaryStartAngle,
                             rotaryEndAngle,
                             true);
        graphics.setColour (outline);
        graphics.strokePath (track,
                             juce::PathStrokeType (lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x,
                             centre.y,
                             radius,
                             radius,
                             0.0f,
                             rotaryStartAngle,
                             angle,
                             true);
        graphics.setColour (accent);
        graphics.strokePath (value,
                             juce::PathStrokeType (lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const auto pointerLength = radius * 0.62f;
        const auto pointerThickness = juce::jmax (1.5f, lineWidth * 0.42f);
        juce::Path pointer;
        pointer.addRoundedRectangle (-pointerThickness * 0.5f,
                                     -radius + lineWidth,
                                     pointerThickness,
                                     pointerLength,
                                     pointerThickness * 0.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre.x, centre.y));
        graphics.setColour (text);
        graphics.fillPath (pointer);
    }

    void drawButtonBackground (juce::Graphics& graphics,
                               juce::Button& button,
                               const juce::Colour&,
                               bool highlighted,
                               bool down) override
    {
        auto colour = button.getToggleState() ? accent : panelRaised;

        if (down)
            colour = colour.darker (0.2f);
        else if (highlighted)
            colour = colour.brighter (0.1f);

        graphics.setColour (colour);
        graphics.fillRoundedRectangle (button.getLocalBounds().toFloat(), 6.0f);
        graphics.setColour (button.getToggleState() ? accent.brighter (0.15f) : outline);
        graphics.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (0.5f),
                                       6.0f,
                                       1.0f);
    }

    void drawToggleButton (juce::Graphics& graphics,
                           juce::ToggleButton& button,
                           bool highlighted,
                           bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto colour = button.getToggleState() ? accentCool : panelRaised;

        if (down)
            colour = colour.darker (0.2f);
        else if (highlighted)
            colour = colour.brighter (0.08f);

        graphics.setColour (colour);
        graphics.fillRoundedRectangle (bounds, 7.0f);
        graphics.setColour (button.getToggleState() ? background : text);
        graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        graphics.drawFittedText (button.getButtonText(),
                                 button.getLocalBounds().reduced (10, 0),
                                 juce::Justification::centred,
                                 1);
    }
};

class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& state,
                   const juce::String& parameterId,
                   juce::String title)
    {
        label.setText (std::move (title), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, mutedText);
        label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);
        slider.setMouseDragSensitivity (160);
        if (const auto* parameter = state.getParameter (parameterId))
            slider.setDoubleClickReturnValue (
                true,
                parameter->convertFrom0to1 (parameter->getDefaultValue()));
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state,
            parameterId,
            slider);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        label.setBounds (bounds.removeFromTop (14));
        slider.setBounds (bounds);
    }

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LayerCard final : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    LayerCard (StackSamplerAudioProcessor& owner, int layerBank)
        : processor (owner), bank (layerBank)
    {
        const auto display = processor.getLayerDisplayState (bank);

        modeBox.addItem ("Quick Mode", 1);
        auto itemId = 2;
        for (const auto mode : stacksampler::selectableQuickModes())
            modeBox.addItem (stacksampler::quickModeName (mode), itemId++);

        modeBox.setSelectedId (
            display.quickMode == stacksampler::QuickMode::none
                ? 1
                : static_cast<int> (display.quickMode) + 1,
            juce::dontSendNotification);
        modeBox.onChange = [this]
        {
            if (modeBox.getSelectedId() <= 1)
                return;

            processor.applyQuickMode (
                bank,
                static_cast<stacksampler::QuickMode> (modeBox.getSelectedId() - 1));
        };
        addAndMakeVisible (modeBox);

        configureToggle (muteButton, "M");
        configureToggle (soloButton, "S");
        configureToggle (reverseButton, "Reverse");
        muteAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters,
                stacksampler::parameterID (bank, "mute"),
                muteButton);
        soloAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters,
                stacksampler::parameterID (bank, "solo"),
                soloButton);
        reverseAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters,
                stacksampler::parameterID (bank, "reverse"),
                reverseButton);

        randomButton.setButtonText ("Random");
        randomButton.onClick = [this] { processor.randomizeLayer (bank); };
        addAndMakeVisible (randomButton);

        removeButton.setButtonText ("Remove");
        removeButton.onClick = [this] { processor.removeLayer (bank); };
        addAndMakeVisible (removeButton);

        for (const auto& definition : knobDefinitions)
        {
            auto knob = std::make_unique<ParameterKnob> (
                processor.parameters,
                stacksampler::parameterID (bank, definition.suffix),
                definition.label);
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return std::any_of (files.begin(),
                            files.end(),
                            [] (const auto& path)
                            {
                                const auto extension
                                    = juce::File { path }.getFileExtension().toLowerCase();
                                return extension == ".wav" || extension == ".aif"
                                    || extension == ".aiff" || extension == ".flac";
                            });
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        dragActive = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dragActive = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragActive = false;

        for (const auto& path : files)
        {
            const juce::File file { path };
            const auto extension = file.getFileExtension().toLowerCase();

            if (extension == ".wav" || extension == ".aif" || extension == ".aiff"
                || extension == ".flac")
            {
                processor.loadSample (bank, file);
                break;
            }
        }

        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (panel);
        graphics.fillRoundedRectangle (bounds, 10.0f);
        graphics.setColour (dragActive ? accent : outline);
        graphics.drawRoundedRectangle (bounds, 10.0f, dragActive ? 2.0f : 1.0f);

        const auto display = processor.getLayerDisplayState (bank);
        const auto badge = juce::Rectangle<float> (14.0f, 13.0f, 30.0f, 24.0f);
        graphics.setColour (accent.withAlpha (0.16f));
        graphics.fillRoundedRectangle (badge, 6.0f);
        graphics.setColour (accent);
        graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
        graphics.drawText (juce::String (bank + 1),
                           badge.toNearestInt(),
                           juce::Justification::centred);

        graphics.setColour (text);
        graphics.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
        graphics.drawFittedText (display.name.isNotEmpty() ? display.name : "Empty layer",
                                 { 54, 9, 218, 20 },
                                 juce::Justification::centredLeft,
                                 1);
        graphics.setColour (mutedText);
        graphics.setFont (juce::FontOptions { 10.5f });
        graphics.drawFittedText (display.status.isNotEmpty()
                                     ? display.status
                                     : "Drop WAV, AIFF or FLAC",
                                 { 54, 28, 218, 16 },
                                 juce::Justification::centredLeft,
                                 1);

        auto wave = waveformBounds.toFloat();
        graphics.setColour (background.withAlpha (0.72f));
        graphics.fillRoundedRectangle (wave, 7.0f);
        graphics.setColour (outline.withAlpha (0.65f));
        graphics.drawRoundedRectangle (wave, 7.0f, 1.0f);

        const auto centreY = wave.getCentreY();
        graphics.setColour (outline.withAlpha (0.45f));
        graphics.drawHorizontalLine (juce::roundToInt (centreY),
                                     wave.getX() + 8.0f,
                                     wave.getRight() - 8.0f);

        if (const auto sample = processor.getSampleData (bank);
            sample != nullptr && ! sample->waveformMin.empty())
        {
            juce::Path waveform;
            const auto count = static_cast<int> (sample->waveformMin.size());
            const auto usable = wave.reduced (9.0f, 12.0f);

            for (int index = 0; index < count; ++index)
            {
                const auto x = usable.getX()
                             + usable.getWidth() * static_cast<float> (index)
                                   / static_cast<float> (juce::jmax (1, count - 1));
                const auto minimum
                    = sample->waveformMin[static_cast<std::size_t> (index)];
                const auto maximum
                    = sample->waveformMax[static_cast<std::size_t> (index)];
                waveform.startNewSubPath (
                    x,
                    juce::jmap (maximum, -1.0f, 1.0f, usable.getBottom(), usable.getY()));
                waveform.lineTo (
                    x,
                    juce::jmap (minimum, -1.0f, 1.0f, usable.getBottom(), usable.getY()));
            }

            graphics.setColour (accentCool);
            graphics.strokePath (waveform, juce::PathStrokeType (1.2f));
        }
        else
        {
            graphics.setColour (dragActive ? accent : mutedText);
            graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            graphics.drawFittedText (dragActive ? "DROP TO LOAD" : "DROP SAMPLE",
                                     waveformBounds.reduced (8),
                                     juce::Justification::centred,
                                     1);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (12);
        auto top = bounds.removeFromTop (36);
        top.removeFromLeft (270);

        modeBox.setBounds (top.removeFromLeft (126).reduced (2, 3));
        muteButton.setBounds (top.removeFromLeft (34).reduced (2, 3));
        soloButton.setBounds (top.removeFromLeft (34).reduced (2, 3));
        reverseButton.setBounds (top.removeFromLeft (70).reduced (2, 3));
        randomButton.setBounds (top.removeFromLeft (68).reduced (2, 3));
        removeButton.setBounds (top.removeFromRight (70).reduced (2, 3));

        bounds.removeFromTop (4);
        waveformBounds = bounds.removeFromLeft (218);
        bounds.removeFromLeft (12);

        constexpr int columns = 9;
        const auto rowHeight = bounds.getHeight() / 2;
        const auto columnWidth = bounds.getWidth() / columns;

        for (std::size_t index = 0; index < knobs.size(); ++index)
        {
            const auto column = static_cast<int> (index) % columns;
            const auto row = static_cast<int> (index) / columns;
            auto knobBounds = juce::Rectangle<int> (
                bounds.getX() + column * columnWidth,
                bounds.getY() + row * rowHeight,
                columnWidth,
                rowHeight);
            knobs[index]->setBounds (knobBounds.reduced (2, 0));
        }
    }

private:
    void configureToggle (juce::TextButton& button, const juce::String& title)
    {
        button.setButtonText (title);
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
    }

    StackSamplerAudioProcessor& processor;
    int bank = 0;
    bool dragActive = false;
    juce::Rectangle<int> waveformBounds;

    juce::ComboBox modeBox;
    juce::TextButton muteButton;
    juce::TextButton soloButton;
    juce::TextButton reverseButton;
    juce::TextButton randomButton;
    juce::TextButton removeButton;
    std::vector<std::unique_ptr<ParameterKnob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;
};

class StackSamplerAudioProcessorEditor::LayerList final : public juce::Component
{
public:
    explicit LayerList (StackSamplerAudioProcessor& owner)
        : processor (owner)
    {
        rebuild();
    }

    void rebuild()
    {
        cards.clear();

        for (const auto bank : processor.getActiveLayers())
        {
            auto card = std::make_unique<LayerCard> (processor, bank);
            addAndMakeVisible (*card);
            cards.push_back (std::move (card));
        }

        const auto requiredHeight = cardGap
                                  + static_cast<int> (cards.size())
                                        * (cardHeight + cardGap);
        setSize (getWidth(), juce::jmax (requiredHeight, 1));
        resized();
    }

    void resized() override
    {
        auto y = cardGap;
        for (auto& card : cards)
        {
            card->setBounds (cardGap,
                             y,
                             juce::jmax (1, getWidth() - cardGap * 2),
                             cardHeight);
            y += cardHeight + cardGap;
        }
    }

private:
    StackSamplerAudioProcessor& processor;
    std::vector<std::unique_ptr<LayerCard>> cards;
};

StackSamplerAudioProcessorEditor::StackSamplerAudioProcessorEditor (
    StackSamplerAudioProcessor& owner)
    : AudioProcessorEditor (&owner), ownerProcessor (owner)
{
    stackLookAndFeel = std::make_unique<StackSamplerLookAndFeel>();
    setLookAndFeel (stackLookAndFeel.get());

    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);

    layerList = std::make_unique<LayerList> (ownerProcessor);
    viewport.setViewedComponent (layerList.get(), false);

    addLayerButton.onClick = [this]
    {
        ownerProcessor.addLayer();
    };
    addAndMakeVisible (addLayerButton);

    humanizeButton.setClickingTogglesState (true);
    addAndMakeVisible (humanizeButton);
    humanizeAttachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            ownerProcessor.parameters,
            "humanize",
            humanizeButton);

    layerCountLabel.setJustificationType (juce::Justification::centredRight);
    layerCountLabel.setColour (juce::Label::textColourId, mutedText);
    layerCountLabel.setFont (juce::FontOptions { 12.0f });
    addAndMakeVisible (layerCountLabel);

    sourceLink.setColour (juce::HyperlinkButton::textColourId, mutedText);
    sourceLink.setFont (juce::FontOptions { 11.0f },
                        false,
                        juce::Justification::centredRight);
    addAndMakeVisible (sourceLink);

    ownerProcessor.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (900, 620, 1600, 1200);
    setSize (1180, 780);
    rebuildLayers();
}

StackSamplerAudioProcessorEditor::~StackSamplerAudioProcessorEditor()
{
    ownerProcessor.removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false);
    setLookAndFeel (nullptr);
}

void StackSamplerAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 0);
    graphics.setColour (text);
    graphics.setFont (juce::FontOptions { 25.0f, juce::Font::bold });
    graphics.drawText ("StackSampler",
                       header.removeFromTop (47),
                       juce::Justification::centredLeft);
    graphics.setColour (mutedText);
    graphics.setFont (juce::FontOptions { 12.0f });
    graphics.drawText ("drag, drop, tweak, done.",
                       header.removeFromTop (18),
                       juce::Justification::centredLeft);

    graphics.setColour (outline.withAlpha (0.65f));
    graphics.drawHorizontalLine (headerHeight - 1,
                                 16.0f,
                                 static_cast<float> (getWidth() - 16));
    graphics.drawHorizontalLine (getHeight() - footerHeight,
                                 16.0f,
                                 static_cast<float> (getWidth() - 16));
}

void StackSamplerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (headerHeight).reduced (20, 17);
    humanizeButton.setBounds (header.removeFromRight (112));

    auto footer = bounds.removeFromBottom (footerHeight).reduced (18, 11);
    addLayerButton.setBounds (footer.removeFromLeft (150));
    layerCountLabel.setBounds (footer.removeFromRight (130));
    footer.removeFromRight (8);
    sourceLink.setBounds (footer.removeFromRight (360));

    viewport.setBounds (bounds.reduced (10, 0));
    layerList->setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()),
                        layerList->getHeight());
    layerList->resized();
}

void StackSamplerAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildLayers();
}

void StackSamplerAudioProcessorEditor::rebuildLayers()
{
    const auto previousScroll = viewport.getViewPositionY();
    layerList->rebuild();
    resized();
    viewport.setViewPosition (0, previousScroll);

    const auto count = static_cast<int> (ownerProcessor.getActiveLayers().size());
    layerCountLabel.setText (juce::String (count) + " / "
                                 + juce::String (stacksampler::kMaxLayers)
                                 + " layers",
                             juce::dontSendNotification);
    addLayerButton.setEnabled (count < stacksampler::kMaxLayers);
}
