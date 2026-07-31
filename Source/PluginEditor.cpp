#include "PluginEditor.h"
#include "PluginUiModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace
{
const auto canvas = juce::Colour::fromRGB (11, 13, 18);
const auto canvasLifted = juce::Colour::fromRGB (17, 20, 27);
const auto surface = juce::Colour::fromRGB (21, 25, 34);
const auto surfaceRaised = juce::Colour::fromRGB (27, 32, 42);
const auto hairline = juce::Colour::fromRGB (42, 48, 59);
const auto primaryText = juce::Colour::fromRGB (242, 244, 247);
const auto secondaryText = juce::Colour::fromRGB (146, 155, 170);
const auto aqua = juce::Colour::fromRGB (114, 220, 207);
const auto coral = juce::Colour::fromRGB (241, 112, 91);
const auto amber = juce::Colour::fromRGB (237, 183, 86);

constexpr int headerHeight = 68;
constexpr int footerHeight = 34;
constexpr int cardHeight = 144;
constexpr int cardGap = 8;

struct ControlDefinition
{
    const char* suffix;
    const char* label;
    const char* tooltip;
};

constexpr std::array<ControlDefinition, 15> controlDefinitions
{{
    { "gain", "INPUT", "Pre-effects gain" },
    { "volume", "LEVEL", "Layer output level" },
    { "pan", "PAN", "Position the layer in the stereo field" },
    { "pitch", "PITCH", "Transpose in semitones" },
    { "fine", "FINE", "Fine tune in cents" },
    { "attack", "ATTACK", "Fade in the start of the layer" },
    { "decay", "DECAY", "Shape the initial decay" },
    { "release", "RELEASE", "Fade out after note release" },
    { "transient", "TRANSIENT", "Soften or emphasize the attack" },
    { "tail", "TAIL", "Shorten or extend the perceived tail" },
    { "highpass", "HIGH-PASS", "Remove low frequencies" },
    { "lowpass", "LOW-PASS", "Remove high frequencies" },
    { "drive", "DRIVE", "Push the layer into soft clipping" },
    { "saturation", "SATURATION", "Add harmonic colour" },
    { "width", "WIDTH", "Narrow or widen the stereo image" }
}};

constexpr std::array<const char*, 3> groupNames
{{
    "LEVEL & TUNE",
    "ENVELOPE & SHAPE",
    "TONE & SPACE"
}};

bool isSupportedAudioFile (const juce::String& path)
{
    const auto extension = juce::File { path }.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif"
        || extension == ".aiff" || extension == ".flac";
}

juce::String cleanNumber (float value, int decimals)
{
    if (std::abs (value) < std::pow (10.0f, -static_cast<float> (decimals)) * 0.5f)
        value = 0.0f;

    return juce::String (value, decimals);
}

juce::String formatParameterValue (const juce::String& suffix, double rawValue)
{
    const auto value = static_cast<float> (rawValue);

    if (suffix == "volume" || suffix == "gain" || suffix == "drive")
        return cleanNumber (value, 1) + " dB";

    if (suffix == "pan")
    {
        if (std::abs (value) < 0.005f)
            return "C";

        return juce::String (juce::roundToInt (std::abs (value) * 100.0f))
             + (value < 0.0f ? " L" : " R");
    }

    if (suffix == "pitch")
        return (value >= 0.5f ? "+" : "") + cleanNumber (value, 0) + " st";

    if (suffix == "fine")
        return (value >= 0.5f ? "+" : "") + cleanNumber (value, 0) + " ct";

    if (suffix == "attack" || suffix == "decay" || suffix == "release")
    {
        if (value >= 1000.0f)
            return cleanNumber (value / 1000.0f, value >= 10000.0f ? 0 : 1) + " s";

        return cleanNumber (value, value < 10.0f ? 1 : 0) + " ms";
    }

    if (suffix == "highpass" || suffix == "lowpass")
    {
        if (value >= 1000.0f)
            return cleanNumber (value / 1000.0f, value >= 10000.0f ? 0 : 1) + " kHz";

        return cleanNumber (value, 0) + " Hz";
    }

    if (suffix == "width")
        return cleanNumber (value * 100.0f, 0) + "%";

    if (suffix == "saturation")
        return cleanNumber (value * 100.0f, 0) + "%";

    if (suffix == "transient" || suffix == "tail")
        return (value > 0.0f ? "+" : "") + cleanNumber (value * 100.0f, 0) + "%";

    return cleanNumber (value, 2);
}

double parseParameterValue (const juce::String& suffix, juce::String input)
{
    input = input.trim().toLowerCase();

    if (suffix == "pan")
    {
        if (input == "c" || input == "centre" || input == "center")
            return 0.0;

        const auto amount = std::abs (input.getFloatValue()) / 100.0f;
        return input.containsChar ('l') ? -amount : amount;
    }

    auto value = static_cast<double> (input.getFloatValue());

    if ((suffix == "attack" || suffix == "decay" || suffix == "release")
        && input.contains ("s") && ! input.contains ("ms"))
        value *= 1000.0;

    if ((suffix == "highpass" || suffix == "lowpass") && input.contains ("k"))
        value *= 1000.0;

    if (suffix == "width" || suffix == "saturation"
        || suffix == "transient" || suffix == "tail")
        value /= 100.0;

    return value;
}

juce::Rectangle<float> roundedPanel (juce::Rectangle<int> bounds)
{
    return bounds.toFloat().reduced (0.5f);
}
}

class StackSamplerAudioProcessorEditor::StackSamplerLookAndFeel final
    : public juce::LookAndFeel_V4
{
public:
    StackSamplerLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, primaryText);
        setColour (juce::Slider::textBoxBackgroundColourId, canvas.withAlpha (0.72f));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, hairline.brighter (0.18f));
        setColour (juce::TextButton::textColourOffId, primaryText);
        setColour (juce::TextButton::textColourOnId, canvas);
        setColour (juce::HyperlinkButton::textColourId, secondaryText);
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
                          .reduced (7.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre (diameter, diameter);

        const auto centre = bounds.getCentre();
        const auto lineWidth = juce::jlimit (2.0f, 4.0f, diameter * 0.065f);
        const auto radius = diameter * 0.5f - lineWidth;
        const auto angle = rotaryStartAngle
                         + sliderPosition * (rotaryEndAngle - rotaryStartAngle);

        graphics.setColour (canvasLifted);
        graphics.fillEllipse (bounds.reduced (lineWidth * 1.8f));

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        graphics.setColour (hairline);
        graphics.strokePath (track,
                             juce::PathStrokeType (lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, angle, true);
        graphics.setColour (aqua);
        graphics.strokePath (value,
                             juce::PathStrokeType (lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const auto pointerLength = radius * 0.48f;
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.3f, -radius + lineWidth * 1.4f,
                                     2.6f, pointerLength, 1.3f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre.x, centre.y));
        graphics.setColour (primaryText);
        graphics.fillPath (pointer);
    }

    void drawButtonBackground (juce::Graphics& graphics,
                               juce::Button& button,
                               const juce::Colour&,
                               bool highlighted,
                               bool down) override
    {
        const auto role = button.getProperties().getWithDefault ("role", "default")
                              .toString();
        auto fill = surfaceRaised;
        auto border = hairline;

        if (role == "primary")
        {
            fill = aqua;
            border = aqua;
        }
        else if (role == "danger")
        {
            fill = coral.withAlpha (highlighted ? 0.22f : 0.10f);
            border = coral.withAlpha (highlighted ? 0.72f : 0.35f);
        }
        else if (role == "chip")
        {
            fill = highlighted ? aqua.withAlpha (0.14f) : canvasLifted;
            border = highlighted ? aqua.withAlpha (0.48f) : hairline.withAlpha (0.76f);
        }
        else if (role == "solo" && button.getToggleState())
        {
            fill = amber;
            border = amber;
        }
        else if (button.getToggleState())
        {
            fill = aqua;
            border = aqua;
        }
        else if (highlighted)
        {
            fill = surfaceRaised.brighter (0.09f);
            border = hairline.brighter (0.22f);
        }

        if (! button.isEnabled())
        {
            fill = surfaceRaised.withAlpha (0.72f);
            border = hairline.withAlpha (0.62f);
        }

        if (down)
            fill = fill.darker (0.14f);

        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (fill);
        graphics.fillRoundedRectangle (bounds, role == "chip" ? 9.0f : 8.0f);
        graphics.setColour (border);
        graphics.drawRoundedRectangle (bounds, role == "chip" ? 9.0f : 8.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& graphics,
                         juce::TextButton& button,
                         bool,
                         bool) override
    {
        const auto role = button.getProperties().getWithDefault ("role", "default")
                              .toString();
        const auto active = button.getToggleState();
        auto colour = primaryText;

        if (role == "primary" || active)
            colour = canvas;
        if (role == "danger" && ! active)
            colour = coral.brighter (0.08f);

        if (! button.isEnabled())
            colour = secondaryText;

        graphics.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.55f));
        graphics.setFont (juce::FontOptions { role == "chip" ? 11.5f : 12.5f,
                                             juce::Font::bold });
        graphics.drawFittedText (button.getButtonText(),
                                 button.getLocalBounds().reduced (7, 2),
                                 juce::Justification::centred,
                                 1);
    }

    void drawToggleButton (juce::Graphics& graphics,
                           juce::ToggleButton& button,
                           bool highlighted,
                           bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = button.getToggleState() ? aqua : surfaceRaised;

        if (highlighted)
            fill = fill.brighter (0.08f);
        if (down)
            fill = fill.darker (0.12f);

        graphics.setColour (fill);
        graphics.fillRoundedRectangle (bounds, 10.0f);
        graphics.setColour (button.getToggleState() ? aqua : hairline);
        graphics.drawRoundedRectangle (bounds, 10.0f, 1.0f);
        graphics.setColour (button.getToggleState() ? canvas : primaryText);
        graphics.setFont (juce::FontOptions { 12.5f, juce::Font::bold });
        graphics.drawFittedText (button.getButtonText(),
                                 button.getLocalBounds().reduced (12, 2),
                                 juce::Justification::centred,
                                 1);
    }

    int getDefaultScrollbarWidth() override
    {
        return 8;
    }

    void drawLabel (juce::Graphics& graphics, juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr)
        {
            juce::LookAndFeel_V4::drawLabel (graphics, label);
            return;
        }

        const auto bounds = label.getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (canvas.withAlpha (0.72f));
        graphics.fillRoundedRectangle (bounds, 5.0f);
        graphics.setColour (hairline.withAlpha (0.74f));
        graphics.drawRoundedRectangle (bounds, 5.0f, 1.0f);
        graphics.setColour (primaryText.withMultipliedAlpha (
            label.isEnabled() ? 1.0f : 0.35f));
        graphics.setFont (juce::FontOptions { 11.5f, juce::Font::bold });
        graphics.drawFittedText (label.getText(), label.getLocalBounds().reduced (5, 1),
                                 juce::Justification::centred, 1);
    }
};

class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& state,
                   int bank,
                   const ControlDefinition& definition)
        : suffix (definition.suffix)
    {
        label.setText (definition.label, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, secondaryText);
        label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
        slider.setMouseDragSensitivity (190);
        slider.setScrollWheelEnabled (false);
        slider.setTextValueSuffix ({ });
        slider.setTooltip (definition.tooltip);
        slider.setTitle (definition.label);
        slider.setDescription (definition.tooltip);

        const auto parameterId = stacksampler::parameterID (bank, definition.suffix);
        if (const auto* parameter = state.getParameter (parameterId))
            slider.setDoubleClickReturnValue (
                true,
                parameter->convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, parameterId, slider);
        slider.textFromValueFunction = [parameterSuffix = suffix] (double value)
        {
            return formatParameterValue (parameterSuffix, value);
        };
        slider.valueFromTextFunction = [parameterSuffix = suffix] (const juce::String& value)
        {
            return parseParameterValue (parameterSuffix, value);
        };
        slider.updateText();
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        label.setBounds (bounds.removeFromTop (16));
        slider.setBounds (bounds);
    }

private:
    juce::String suffix;
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class WaveformDisplay final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              public juce::SettableTooltipClient,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    WaveformDisplay (StackSamplerAudioProcessor& owner,
                     int layerBank,
                     bool allowTrimEditing)
        : processor (owner), bank (layerBank), editableTrim (allowTrimEditing),
          startId (stacksampler::parameterID (bank, "start")),
          endId (stacksampler::parameterID (bank, "end")),
          reverseId (stacksampler::parameterID (bank, "reverse"))
    {
        if (const auto* value = processor.parameters.getRawParameterValue (startId))
            latestStart.store (value->load (std::memory_order_relaxed));
        if (const auto* value = processor.parameters.getRawParameterValue (endId))
            latestEnd.store (value->load (std::memory_order_relaxed));
        if (const auto* value = processor.parameters.getRawParameterValue (reverseId))
            latestReverse.store (value->load (std::memory_order_relaxed));

        displayStart = latestStart.load (std::memory_order_relaxed);
        displayEnd = latestEnd.load (std::memory_order_relaxed);
        displayReverse = latestReverse.load (std::memory_order_relaxed) >= 0.5f;
        startParameter = processor.parameters.getParameter (startId);
        endParameter = processor.parameters.getParameter (endId);

        processor.parameters.addParameterListener (startId, this);
        processor.parameters.addParameterListener (endId, this);
        processor.parameters.addParameterListener (reverseId, this);

        setTooltip (editableTrim
                        ? "Drop a sample, then drag the IN and OUT handles to trim it"
                        : "Drop WAV, AIFF or FLAC");
        setTitle ("Sample waveform");
        setDescription ("Drag and drop a sample here");
    }

    ~WaveformDisplay() override
    {
        processor.parameters.removeParameterListener (startId, this);
        processor.parameters.removeParameterListener (endId, this);
        processor.parameters.removeParameterListener (reverseId, this);

        if (dragTarget != DragTarget::none)
            endGesture();
    }

    std::function<void()> onSelect;

    void pollParameterChanges()
    {
        if (dragTarget != DragTarget::none
            && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
            endGesture();

        if (! parameterDirty.exchange (false, std::memory_order_acquire))
            return;

        displayStart = latestStart.load (std::memory_order_relaxed);
        displayEnd = latestEnd.load (std::memory_order_relaxed);
        if (dragTarget == DragTarget::none)
            displayReverse = latestReverse.load (std::memory_order_relaxed) >= 0.5f;
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return std::any_of (files.begin(), files.end(), isSupportedAudioFile);
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
            if (isSupportedAudioFile (path))
            {
                processor.loadSampleAsync (bank, juce::File { path });
                break;
            }
        }

        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto outer = getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (dragActive ? aqua.withAlpha (0.10f) : canvas.withAlpha (0.78f));
        graphics.fillRoundedRectangle (outer, editableTrim ? 12.0f : 8.0f);
        graphics.setColour (dragActive ? aqua : hairline.withAlpha (0.82f));
        graphics.drawRoundedRectangle (outer, editableTrim ? 12.0f : 8.0f,
                                       dragActive ? 1.5f : 1.0f);

        auto waveformArea = outer.reduced (editableTrim ? 18.0f : 8.0f,
                                           editableTrim ? 20.0f : 7.0f);
        if (editableTrim)
            waveformArea = waveformArea.withTrimmedTop (8.0f);

        const auto sample = processor.getSampleData (bank);
        if (sample == nullptr || sample->waveformMin.empty())
        {
            const auto display = processor.getLayerDisplayState (bank);
            graphics.setColour (dragActive ? aqua : primaryText.withAlpha (0.82f));
            graphics.setFont (juce::FontOptions { editableTrim ? 15.0f : 11.5f,
                                                 juce::Font::bold });
            graphics.drawFittedText (dragActive ? "RELEASE TO LOAD"
                                                : "DROP SAMPLE HERE",
                                     waveformArea.toNearestInt(),
                                     juce::Justification::centred,
                                     1);

            if (editableTrim)
            {
                graphics.setColour (secondaryText);
                graphics.setFont (juce::FontOptions { 11.5f });
                graphics.drawFittedText (display.status.isNotEmpty()
                                             ? display.status
                                             : "WAV  |  AIFF  |  FLAC",
                                         waveformArea.toNearestInt().translated (0, 24),
                                         juce::Justification::centred,
                                         1);
            }
            return;
        }

        const auto reverse = isReversed();
        const auto count = static_cast<int> (sample->waveformMin.size());
        const auto centreY = waveformArea.getCentreY();
        graphics.setColour (hairline.withAlpha (0.5f));
        graphics.drawHorizontalLine (juce::roundToInt (centreY),
                                     waveformArea.getX(), waveformArea.getRight());

        juce::Path waveform;
        for (int displayIndex = 0; displayIndex < count; ++displayIndex)
        {
            const auto sourceIndex = reverse ? count - 1 - displayIndex : displayIndex;
            const auto x = waveformArea.getX()
                         + waveformArea.getWidth()
                               * static_cast<float> (displayIndex)
                               / static_cast<float> (juce::jmax (1, count - 1));
            const auto y = juce::jmap (
                sample->waveformMax[static_cast<std::size_t> (sourceIndex)],
                -1.0f, 1.0f, waveformArea.getBottom(), waveformArea.getY());

            if (displayIndex == 0)
                waveform.startNewSubPath (x, y);
            else
                waveform.lineTo (x, y);
        }

        for (int displayIndex = count - 1; displayIndex >= 0; --displayIndex)
        {
            const auto sourceIndex = reverse ? count - 1 - displayIndex : displayIndex;
            const auto x = waveformArea.getX()
                         + waveformArea.getWidth()
                               * static_cast<float> (displayIndex)
                               / static_cast<float> (juce::jmax (1, count - 1));
            const auto y = juce::jmap (
                sample->waveformMin[static_cast<std::size_t> (sourceIndex)],
                -1.0f, 1.0f, waveformArea.getBottom(), waveformArea.getY());
            waveform.lineTo (x, y);
        }
        waveform.closeSubPath();

        juce::ColourGradient waveGradient (
            aqua.withAlpha (editableTrim ? 0.60f : 0.76f),
            waveformArea.getX(), waveformArea.getY(),
            aqua.darker (0.26f).withAlpha (editableTrim ? 0.24f : 0.38f),
            waveformArea.getRight(), waveformArea.getBottom(), false);
        graphics.setGradientFill (waveGradient);
        graphics.fillPath (waveform);
        graphics.setColour (aqua.withAlpha (0.88f));
        graphics.strokePath (waveform,
                             juce::PathStrokeType (editableTrim ? 1.15f : 0.9f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const auto visual = getVisualSelection();
        const auto selectionLeft = waveformArea.getX() + visual.getStart() * waveformArea.getWidth();
        const auto selectionRight = waveformArea.getX() + visual.getEnd() * waveformArea.getWidth();

        graphics.setColour (canvas.withAlpha (editableTrim ? 0.68f : 0.56f));
        graphics.fillRect (juce::Rectangle<float> (waveformArea.getX(), waveformArea.getY(),
                                                   juce::jmax (0.0f, selectionLeft - waveformArea.getX()),
                                                   waveformArea.getHeight()));
        graphics.fillRect (juce::Rectangle<float> (selectionRight, waveformArea.getY(),
                                                   juce::jmax (0.0f, waveformArea.getRight() - selectionRight),
                                                   waveformArea.getHeight()));

        if (editableTrim)
        {
            drawTrimHandle (graphics, selectionLeft, waveformArea, true);
            drawTrimHandle (graphics, selectionRight, waveformArea, false);

            graphics.setColour (secondaryText);
            graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
            graphics.drawText (reverse ? "<  REVERSE" : "FORWARD  >",
                               outer.toNearestInt().reduced (14, 7).removeFromTop (18),
                               juce::Justification::centredRight);
        }
        else if (reverse)
        {
            graphics.setColour (aqua.withAlpha (0.88f));
            graphics.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
            graphics.drawText ("<", outer.toNearestInt().reduced (6),
                               juce::Justification::topRight);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (onSelect)
            onSelect();

        if (! editableTrim || processor.getSampleData (bank) == nullptr)
            return;

        const auto positions = getHandlePositions();
        const auto distanceToLeft = std::abs (event.position.x - positions.first);
        const auto distanceToRight = std::abs (event.position.x - positions.second);
        const auto reverse = isReversed();

        if (juce::jmin (distanceToLeft, distanceToRight) > 18.0f)
            return;

        const auto choseLeft = distanceToLeft <= distanceToRight;
        dragTarget = choseLeft
                         ? (reverse ? DragTarget::end : DragTarget::start)
                         : (reverse ? DragTarget::start : DragTarget::end);
        beginGesture();
        updateDrag (event.position.x);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragTarget != DragTarget::none)
            updateDrag (event.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        endGesture();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (! editableTrim || processor.getSampleData (bank) == nullptr)
            return;

        const auto positions = getHandlePositions();
        const auto nearHandle = std::abs (event.position.x - positions.first) <= 12.0f
                             || std::abs (event.position.x - positions.second) <= 12.0f;
        setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::NormalCursor);
    }

private:
    enum class DragTarget
    {
        none,
        start,
        end
    };

    void parameterChanged (const juce::String& parameterId, float newValue) override
    {
        if (parameterId == startId)
            latestStart.store (newValue, std::memory_order_relaxed);
        else if (parameterId == endId)
            latestEnd.store (newValue, std::memory_order_relaxed);
        else if (parameterId == reverseId)
            latestReverse.store (newValue, std::memory_order_relaxed);

        parameterDirty.store (true, std::memory_order_release);
    }

    bool isReversed() const noexcept
    {
        return displayReverse;
    }

    juce::Range<float> getVisualSelection() const noexcept
    {
        const auto range = stacksampler::ui::visualTrimRange (
            displayStart, displayEnd, isReversed());
        return { range.start, range.end };
    }

    juce::Rectangle<float> getWaveformArea() const
    {
        auto area = getLocalBounds().toFloat().reduced (18.5f, 20.5f);
        return area.withTrimmedTop (8.0f);
    }

    std::pair<float, float> getHandlePositions() const
    {
        const auto area = getWaveformArea();
        const auto selection = getVisualSelection();
        return { area.getX() + selection.getStart() * area.getWidth(),
                 area.getX() + selection.getEnd() * area.getWidth() };
    }

    void drawTrimHandle (juce::Graphics& graphics,
                         float x,
                         juce::Rectangle<float> area,
                         bool isLeft) const
    {
        graphics.setColour (primaryText.withAlpha (0.95f));
        graphics.fillRoundedRectangle (x - 1.0f, area.getY() - 3.0f,
                                       2.0f, area.getHeight() + 6.0f, 1.0f);
        graphics.setColour (aqua);
        graphics.fillRoundedRectangle (x - 5.0f, area.getY() - 5.0f,
                                       10.0f, 8.0f, 3.0f);

        const auto selection = getVisualSelection();
        const auto percent = juce::roundToInt ((isLeft ? selection.getStart()
                                                       : selection.getEnd())
                                              * 100.0f);
        const auto labelWidth = 62.0f;
        auto labelBounds = juce::Rectangle<float> (0.0f, area.getBottom() + 5.0f,
                                                    labelWidth, 18.0f);
        labelBounds.setX (isLeft
                              ? juce::jlimit (area.getX(), area.getRight() - labelWidth,
                                              x - 1.0f)
                              : juce::jlimit (area.getX(), area.getRight() - labelWidth,
                                              x - labelWidth + 1.0f));
        graphics.setColour (secondaryText);
        graphics.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
        graphics.drawText ((isLeft ? "IN  " : "OUT  ") + juce::String (percent) + "%",
                           labelBounds.toNearestInt(),
                           isLeft ? juce::Justification::centredLeft
                                  : juce::Justification::centredRight);
    }

    void beginGesture()
    {
        if (dragTarget == DragTarget::start && startParameter != nullptr)
            startParameter->beginChangeGesture();
        else if (dragTarget == DragTarget::end && endParameter != nullptr)
            endParameter->beginChangeGesture();

        dragReverse = displayReverse;
    }

    void endGesture()
    {
        const auto wasDragging = dragTarget != DragTarget::none;
        if (dragTarget == DragTarget::start && startParameter != nullptr)
            startParameter->endChangeGesture();
        else if (dragTarget == DragTarget::end && endParameter != nullptr)
            endParameter->endChangeGesture();

        dragTarget = DragTarget::none;
        if (wasDragging)
            parameterDirty.store (true, std::memory_order_release);
    }

    void updateDrag (float mouseX)
    {
        auto* parameter = dragTarget == DragTarget::start ? startParameter : endParameter;
        if (parameter == nullptr)
            return;

        const auto area = getWaveformArea();
        auto visualPosition = juce::jlimit (0.0f, 1.0f,
                                           (mouseX - area.getX()) / area.getWidth());
        auto sourcePosition = stacksampler::ui::sourcePositionFromVisual (
            visualPosition, dragReverse);
        const auto start = latestStart.load (std::memory_order_relaxed);
        const auto end = latestEnd.load (std::memory_order_relaxed);

        if (dragTarget == DragTarget::start)
            sourcePosition = juce::jlimit (0.0f, juce::jmax (0.0f, end - 0.01f),
                                           sourcePosition);
        else
            sourcePosition = juce::jlimit (juce::jmin (1.0f, start + 0.01f), 1.0f,
                                           sourcePosition);

        const auto snapped = parameter->getNormalisableRange().snapToLegalValue (
            sourcePosition);
        const auto previous = dragTarget == DragTarget::start ? start : end;
        if (std::abs (snapped - previous) <= 0.000001f)
            return;

        if (dragTarget == DragTarget::start)
        {
            latestStart.store (snapped, std::memory_order_relaxed);
            displayStart = snapped;
        }
        else
        {
            latestEnd.store (snapped, std::memory_order_relaxed);
            displayEnd = snapped;
        }

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (snapped));
        repaint();
    }

    StackSamplerAudioProcessor& processor;
    int bank = 0;
    bool editableTrim = false;
    bool dragActive = false;
    bool dragReverse = false;
    DragTarget dragTarget = DragTarget::none;
    juce::String startId;
    juce::String endId;
    juce::String reverseId;
    std::atomic<float> latestStart { 0.0f };
    std::atomic<float> latestEnd { 1.0f };
    std::atomic<float> latestReverse { 0.0f };
    std::atomic<bool> parameterDirty { false };
    float displayStart = 0.0f;
    float displayEnd = 1.0f;
    bool displayReverse = false;
    juce::RangedAudioParameter* startParameter = nullptr;
    juce::RangedAudioParameter* endParameter = nullptr;
};

class LayerCard final : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    LayerCard (StackSamplerAudioProcessor& owner,
               int layerBank,
               int displayOrdinal,
               bool initiallySelected,
               std::function<void(int)> selectCallback,
               std::function<void(int)> removeCallback)
        : processor (owner), bank (layerBank), ordinal (displayOrdinal),
          selected (initiallySelected), onSelect (std::move (selectCallback)),
          onRemove (std::move (removeCallback)), waveform (owner, layerBank, false)
    {
        waveform.onSelect = [this]
        {
            if (onSelect)
                onSelect (bank);
        };
        addAndMakeVisible (waveform);

        configureToggle (muteButton, "MUTE", "Mute this layer", "mute");
        configureToggle (soloButton, "SOLO", "Solo this layer", "solo");
        soloButton.getProperties().set ("role", "solo");

        muteAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters, stacksampler::parameterID (bank, "mute"), muteButton);
        soloAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters, stacksampler::parameterID (bank, "solo"), soloButton);

        removeButton.setButtonText ("REMOVE");
        removeButton.getProperties().set ("role", "danger");
        removeButton.setTooltip ("Remove layer");
        removeButton.setTitle ("Remove layer");
        removeButton.onClick = [this]
        {
            if (onRemove)
                onRemove (bank);
        };
        addAndMakeVisible (removeButton);
    }

    int getBank() const noexcept
    {
        return bank;
    }

    void setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;

        selected = shouldBeSelected;
        repaint();
    }

    void refresh()
    {
        repaint();
        waveform.repaint();
    }

    void pollParameterChanges()
    {
        waveform.pollParameterChanges();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return std::any_of (files.begin(), files.end(), isSupportedAudioFile);
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
            if (isSupportedAudioFile (path))
            {
                processor.loadSampleAsync (bank, juce::File { path });
                break;
            }
        }
        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (selected ? surfaceRaised : surface);
        graphics.fillRoundedRectangle (bounds, 11.0f);
        graphics.setColour (dragActive ? aqua
                                       : selected ? aqua.withAlpha (0.58f)
                                                  : hairline.withAlpha (0.78f));
        graphics.drawRoundedRectangle (bounds, 11.0f, 1.0f);

        if (selected)
        {
            graphics.setColour (aqua);
            graphics.fillRoundedRectangle (bounds.getX(), bounds.getY() + 11.0f,
                                           3.0f, bounds.getHeight() - 22.0f, 1.5f);
        }

        graphics.setColour (primaryText.withAlpha (selected ? 0.055f : 0.032f));
        graphics.setFont (juce::FontOptions { 46.0f, juce::Font::bold });
        graphics.drawText (juce::String (ordinal).paddedLeft ('0', 2),
                           getLocalBounds().withTrimmedRight (12),
                           juce::Justification::centredRight);

        const auto display = processor.getLayerDisplayState (bank);
        graphics.setColour (selected ? aqua : secondaryText);
        graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        graphics.drawText (juce::String (ordinal).paddedLeft ('0', 2),
                           juce::Rectangle<int> { 14, 10, 28, 17 },
                           juce::Justification::centredLeft);

        graphics.setColour (primaryText);
        graphics.setFont (juce::FontOptions { 13.5f, juce::Font::bold });
        graphics.drawFittedText (display.name.isNotEmpty() ? display.name : "Empty layer",
                                 { 42, 8, getWidth() - 56, 20 },
                                 juce::Justification::centredLeft, 1);
        graphics.setColour (secondaryText);
        graphics.setFont (juce::FontOptions { 10.5f });
        graphics.drawFittedText (display.status.isNotEmpty()
                                     ? display.status
                                     : "Drop WAV, AIFF or FLAC",
                                 { 42, 26, getWidth() - 56, 16 },
                                 juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (10);
        bounds.removeFromTop (38);
        waveform.setBounds (bounds.removeFromTop (48));
        bounds.removeFromTop (6);

        muteButton.setBounds (bounds.removeFromLeft (62));
        bounds.removeFromLeft (6);
        soloButton.setBounds (bounds.removeFromLeft (62));
        removeButton.setBounds (bounds.removeFromRight (76));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect (bank);
    }

private:
    void configureToggle (juce::TextButton& button,
                          const juce::String& title,
                          const juce::String& tooltip,
                          const juce::String& role)
    {
        button.setButtonText (title);
        button.setClickingTogglesState (true);
        button.getProperties().set ("role", role);
        button.setTooltip (tooltip);
        button.setTitle (tooltip);
        addAndMakeVisible (button);
    }

    StackSamplerAudioProcessor& processor;
    int bank = 0;
    int ordinal = 0;
    bool selected = false;
    bool dragActive = false;
    std::function<void(int)> onSelect;
    std::function<void(int)> onRemove;
    WaveformDisplay waveform;
    juce::TextButton muteButton;
    juce::TextButton soloButton;
    juce::TextButton removeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
};

class StackSamplerAudioProcessorEditor::LayerList final : public juce::Component
{
public:
    LayerList (StackSamplerAudioProcessor& owner,
               std::function<void(int)> selectCallback,
               std::function<void(int)> removeCallback)
        : processor (owner), onSelect (std::move (selectCallback)),
          onRemove (std::move (removeCallback))
    {
    }

    void rebuild (int selectedLayerBank)
    {
        cards.clear();
        const auto layers = processor.getActiveLayers();

        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const auto bank = layers[index];
            auto card = std::make_unique<LayerCard> (
                processor, bank, static_cast<int> (index) + 1,
                bank == selectedLayerBank,
                onSelect, onRemove);
            addAndMakeVisible (*card);
            cards.push_back (std::move (card));
        }

        const auto requiredHeight = cardGap
                                  + static_cast<int> (cards.size()) * (cardHeight + cardGap);
        setSize (getWidth(), juce::jmax (requiredHeight, 1));
        resized();
    }

    void setSelectedBank (int selectedLayerBank)
    {
        for (auto& card : cards)
            card->setSelected (card->getBank() == selectedLayerBank);
    }

    void refresh()
    {
        for (auto& card : cards)
            card->refresh();
    }

    void pollParameterChanges()
    {
        for (auto& card : cards)
            card->pollParameterChanges();
    }

    juce::Rectangle<int> getCardBoundsForBank (int bank) const
    {
        for (const auto& card : cards)
            if (card->getBank() == bank)
                return card->getBounds();

        return {};
    }

    void resized() override
    {
        auto y = cardGap;
        for (auto& card : cards)
        {
            card->setBounds (cardGap, y,
                             juce::jmax (1, getWidth() - cardGap * 2), cardHeight);
            y += cardHeight + cardGap;
        }
    }

private:
    StackSamplerAudioProcessor& processor;
    std::function<void(int)> onSelect;
    std::function<void(int)> onRemove;
    std::vector<std::unique_ptr<LayerCard>> cards;
};

class StackSamplerAudioProcessorEditor::LayerInspector final : public juce::Component
{
public:
    LayerInspector (StackSamplerAudioProcessor& owner,
                    int layerBank,
                    int displayOrdinal,
                    std::function<void(int)> removeCallback)
        : processor (owner), bank (layerBank), ordinal (displayOrdinal),
          onRemove (std::move (removeCallback)), waveform (owner, layerBank, true)
    {
        reverseButton.setButtonText ("Reverse");
        reverseButton.setClickingTogglesState (true);
        reverseButton.setTooltip ("Reverse playback and mirror the waveform");
        reverseButton.setTitle ("Reverse sample");
        addAndMakeVisible (reverseButton);
        reverseAttachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.parameters, stacksampler::parameterID (bank, "reverse"),
                reverseButton);

        randomButton.setButtonText ("Random");
        randomButton.setTooltip ("Apply small, musical variations to this layer");
        randomButton.setTitle ("Randomize layer");
        randomButton.onClick = [this] { processor.randomizeLayer (bank); };
        addAndMakeVisible (randomButton);

        removeButton.setButtonText ("Remove Layer");
        removeButton.getProperties().set ("role", "danger");
        removeButton.setTooltip ("Remove this layer from the stack");
        removeButton.setTitle ("Remove layer");
        removeButton.onClick = [this]
        {
            if (onRemove)
                onRemove (bank);
        };
        addAndMakeVisible (removeButton);

        for (const auto mode : stacksampler::selectableQuickModes())
        {
            auto button = std::make_unique<juce::TextButton> (
                stacksampler::quickModeName (mode));
            button->getProperties().set ("role", "chip");
            button->setTooltip ("Set a useful starting point for "
                                + stacksampler::quickModeName (mode));
            button->onClick = [this, mode] { processor.applyQuickMode (bank, mode); };
            addAndMakeVisible (*button);
            modeButtons.push_back (std::move (button));
        }

        addAndMakeVisible (waveform);

        for (const auto& definition : controlDefinitions)
        {
            auto knob = std::make_unique<ParameterKnob> (
                processor.parameters, bank, definition);
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto outer = roundedPanel (getLocalBounds());
        graphics.setColour (surface);
        graphics.fillRoundedRectangle (outer, 13.0f);
        graphics.setColour (hairline.withAlpha (0.88f));
        graphics.drawRoundedRectangle (outer, 13.0f, 1.0f);

        const auto display = processor.getLayerDisplayState (bank);
        auto identity = identityBounds;
        graphics.setColour (primaryText.withAlpha (0.035f));
        graphics.setFont (juce::FontOptions { 76.0f, juce::Font::bold });
        graphics.drawText (juce::String (ordinal).paddedLeft ('0', 2),
                           getLocalBounds().reduced (16),
                           juce::Justification::topRight);

        graphics.setColour (aqua);
        graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        graphics.drawText ("LAYER " + juce::String (ordinal).paddedLeft ('0', 2),
                           identity.removeFromTop (16),
                           juce::Justification::centredLeft);
        graphics.setColour (primaryText);
        graphics.setFont (juce::FontOptions { 19.0f, juce::Font::bold });
        graphics.drawFittedText (display.name.isNotEmpty() ? display.name : "Empty layer",
                                 identity.withTrimmedBottom (17),
                                 juce::Justification::centredLeft, 1);
        graphics.setColour (secondaryText);
        graphics.setFont (juce::FontOptions { 11.0f });
        graphics.drawFittedText (display.status.isNotEmpty()
                                     ? display.status
                                     : "Drop WAV, AIFF or FLAC",
                                 identity.removeFromBottom (16),
                                 juce::Justification::centredLeft, 1);

        graphics.setColour (secondaryText);
        graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        graphics.drawText ("QUICK START", quickLabelBounds,
                           juce::Justification::centredLeft);

        for (std::size_t index = 0; index < groupBounds.size(); ++index)
        {
            const auto group = groupBounds[index].toFloat();
            graphics.setColour (canvasLifted.withAlpha (0.58f));
            graphics.fillRoundedRectangle (group, 10.0f);
            graphics.setColour (hairline.withAlpha (0.52f));
            graphics.drawRoundedRectangle (group.reduced (0.5f), 10.0f, 1.0f);
            graphics.setColour (secondaryText);
            graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
            auto titleBounds = groupBounds[index].reduced (12, 0);
            graphics.drawText (groupNames[index],
                               titleBounds.removeFromTop (22),
                               juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (16);
        auto identityRow = bounds.removeFromTop (48);
        auto actions = identityRow.removeFromRight (300);
        identityBounds = identityRow.withTrimmedRight (10);

        removeButton.setBounds (actions.removeFromRight (106).reduced (2, 5));
        actions.removeFromRight (6);
        randomButton.setBounds (actions.removeFromRight (82).reduced (2, 5));
        actions.removeFromRight (6);
        reverseButton.setBounds (actions.removeFromRight (94).reduced (2, 5));

        bounds.removeFromTop (6);
        auto quickRow = bounds.removeFromTop (38);
        quickLabelBounds = quickRow.removeFromLeft (82);
        quickRow.removeFromLeft (4);
        const auto modeWidth = quickRow.getWidth()
                             / juce::jmax (1, static_cast<int> (modeButtons.size()));
        for (std::size_t index = 0; index < modeButtons.size(); ++index)
        {
            auto buttonBounds = quickRow.removeFromLeft (
                index + 1 == modeButtons.size() ? quickRow.getWidth() : modeWidth);
            modeButtons[index]->setBounds (buttonBounds.reduced (2, 3));
        }

        bounds.removeFromTop (8);
        const auto waveformHeight = juce::jlimit (122, 164, bounds.getHeight() / 4);
        waveform.setBounds (bounds.removeFromTop (waveformHeight));
        bounds.removeFromTop (10);

        const auto groupHeight = bounds.getHeight() / 3;
        for (int group = 0; group < 3; ++group)
        {
            auto groupArea = bounds.removeFromTop (
                group == 2 ? bounds.getHeight() : groupHeight);
            if (group < 2)
                groupArea.removeFromBottom (7);
            groupBounds[static_cast<std::size_t> (group)] = groupArea;

            auto controls = groupArea.reduced (8, 5);
            controls.removeFromTop (18);
            const auto controlWidth = controls.getWidth() / 5;
            for (int index = 0; index < 5; ++index)
            {
                const auto knobIndex = group * 5 + index;
                auto knobBounds = controls.removeFromLeft (
                    index == 4 ? controls.getWidth() : controlWidth);
                knobs[static_cast<std::size_t> (knobIndex)]->setBounds (
                    knobBounds.reduced (2, 0));
            }
        }
    }

    void refresh()
    {
        repaint();
        waveform.repaint();
    }

    void pollParameterChanges()
    {
        waveform.pollParameterChanges();
    }

private:
    StackSamplerAudioProcessor& processor;
    int bank = 0;
    int ordinal = 0;
    std::function<void(int)> onRemove;
    juce::Rectangle<int> identityBounds;
    juce::Rectangle<int> quickLabelBounds;
    std::array<juce::Rectangle<int>, 3> groupBounds;
    WaveformDisplay waveform;
    juce::TextButton reverseButton;
    juce::TextButton randomButton;
    juce::TextButton removeButton;
    std::vector<std::unique_ptr<juce::TextButton>> modeButtons;
    std::vector<std::unique_ptr<ParameterKnob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        reverseAttachment;
};

StackSamplerAudioProcessorEditor::StackSamplerAudioProcessorEditor (
    StackSamplerAudioProcessor& owner)
    : AudioProcessorEditor (&owner), ownerProcessor (owner)
{
    stackLookAndFeel = std::make_unique<StackSamplerLookAndFeel>();
    setLookAndFeel (stackLookAndFeel.get());
    setOpaque (true);

    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    viewport.setColour (juce::ScrollBar::backgroundColourId,
                        juce::Colours::transparentBlack);
    addAndMakeVisible (viewport);

    layerList = std::make_unique<LayerList> (
        ownerProcessor,
        [this] (int bank) { selectLayer (bank); },
        [this] (int bank) { removeLayer (bank); });
    viewport.setViewedComponent (layerList.get(), false);

    addLayerButton.getProperties().set ("role", "primary");
    addLayerButton.setTooltip ("Create a new empty layer");
    addLayerButton.setTitle ("Add layer");
    addLayerButton.onClick = [this]
    {
        if (! ownerProcessor.addLayer())
            return;

        const auto activeLayers = ownerProcessor.getActiveLayers();
        if (! activeLayers.empty())
        {
            selectedBank = activeLayers.back();
            rebuildLayers();
            selectLayer (selectedBank, true);
        }
    };
    addAndMakeVisible (addLayerButton);

    humanizeButton.setClickingTogglesState (true);
    humanizeButton.setTooltip (
        "Add tiny timing, pitch, level and pan variations to repeated notes");
    humanizeButton.setTitle ("Humanize repeated notes");
    addAndMakeVisible (humanizeButton);
    humanizeAttachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            ownerProcessor.parameters, "humanize", humanizeButton);

    layerCountLabel.setJustificationType (juce::Justification::centredRight);
    layerCountLabel.setColour (juce::Label::textColourId, secondaryText);
    layerCountLabel.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    addAndMakeVisible (layerCountLabel);

    sourceLink.setColour (juce::HyperlinkButton::textColourId, secondaryText);
    sourceLink.setFont (juce::FontOptions { 10.5f }, false,
                        juce::Justification::centredRight);
    addAndMakeVisible (sourceLink);

    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 500);
    ownerProcessor.addChangeListener (this);
    startTimerHz (30);

    setResizable (true, false);
    setResizeLimits (1060, 780, 1680, 1200);
    setSize (1280, 800);

    const auto activeLayers = ownerProcessor.getActiveLayers();
    if (! activeLayers.empty())
        selectedBank = activeLayers.front();
    rebuildLayers();
}

StackSamplerAudioProcessorEditor::~StackSamplerAudioProcessorEditor()
{
    ownerProcessor.removeChangeListener (this);
    stopTimer();
    tooltipWindow.reset();
    layerInspector.reset();
    viewport.setViewedComponent (nullptr, false);
    setLookAndFeel (nullptr);
}

void StackSamplerAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    juce::ColourGradient backgroundGradient (
        canvasLifted, 0.0f, 0.0f,
        canvas, static_cast<float> (getWidth()), static_cast<float> (getHeight()), false);
    graphics.setGradientFill (backgroundGradient);
    graphics.fillRect (getLocalBounds());

    juce::ColourGradient glow (
        aqua.withAlpha (0.055f), static_cast<float> (getWidth()) * 0.78f, 0.0f,
        juce::Colours::transparentBlack, static_cast<float> (getWidth()) * 0.78f,
        static_cast<float> (getHeight()) * 0.58f, true);
    graphics.setGradientFill (glow);
    graphics.fillRect (getLocalBounds());

    auto mark = juce::Rectangle<float> (18.0f, 17.0f, 31.0f, 31.0f);
    graphics.setColour (aqua);
    for (int row = 0; row < 3; ++row)
    {
        const auto inset = static_cast<float> (row) * 3.5f;
        graphics.fillRoundedRectangle (mark.getX() + inset,
                                       mark.getY() + static_cast<float> (row) * 8.5f,
                                       mark.getWidth() - inset * 2.0f,
                                       4.5f, 2.25f);
    }

    graphics.setColour (primaryText);
    graphics.setFont (juce::FontOptions { 21.0f, juce::Font::bold });
    graphics.drawText ("StackSampler", 58, 11, 210, 28,
                       juce::Justification::centredLeft);
    graphics.setColour (secondaryText);
    graphics.setFont (juce::FontOptions { 10.8f });
    graphics.drawText ("layer, shape, play.", 59, 36, 210, 18,
                       juce::Justification::centredLeft);

    graphics.setColour (hairline.withAlpha (0.65f));
    graphics.drawHorizontalLine (headerHeight - 1, 14.0f,
                                 static_cast<float> (getWidth() - 14));

    graphics.setColour (surface.withAlpha (0.86f));
    graphics.fillRoundedRectangle (roundedPanel (rackPanelBounds), 13.0f);
    graphics.setColour (hairline.withAlpha (0.88f));
    graphics.drawRoundedRectangle (roundedPanel (rackPanelBounds), 13.0f, 1.0f);

    graphics.setColour (secondaryText);
    graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
    graphics.drawText ("LAYERS", rackPanelBounds.reduced (13, 0).removeFromTop (44),
                       juce::Justification::centredLeft);

    if (layerInspector == nullptr && ! inspectorPanelBounds.isEmpty())
    {
        graphics.setColour (surface);
        graphics.fillRoundedRectangle (roundedPanel (inspectorPanelBounds), 13.0f);
        graphics.setColour (hairline);
        graphics.drawRoundedRectangle (roundedPanel (inspectorPanelBounds), 13.0f, 1.0f);
        graphics.setColour (primaryText);
        graphics.setFont (juce::FontOptions { 19.0f, juce::Font::bold });
        graphics.drawText ("Your stack is empty", inspectorPanelBounds.translated (0, -10),
                           juce::Justification::centred);
        graphics.setColour (secondaryText);
        graphics.setFont (juce::FontOptions { 12.0f });
        graphics.drawText ("Add a layer to begin", inspectorPanelBounds.translated (0, 18),
                           juce::Justification::centred);
    }

    graphics.setColour (hairline.withAlpha (0.56f));
    graphics.drawHorizontalLine (getHeight() - footerHeight, 14.0f,
                                 static_cast<float> (getWidth() - 14));
}

void StackSamplerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (headerHeight).reduced (16, 12);
    humanizeButton.setBounds (header.removeFromRight (122));

    auto footer = bounds.removeFromBottom (footerHeight).reduced (15, 4);
    sourceLink.setBounds (footer.removeFromRight (430));

    auto body = bounds.reduced (12, 10);
    const auto rackWidth = juce::jlimit (292, 332,
                                        juce::roundToInt (
                                            static_cast<float> (body.getWidth()) * 0.265f));
    rackPanelBounds = body.removeFromLeft (rackWidth);
    body.removeFromLeft (12);
    inspectorPanelBounds = body;

    auto rackContent = rackPanelBounds.reduced (10);
    auto rackHeader = rackContent.removeFromTop (34);
    layerCountLabel.setBounds (rackHeader.removeFromRight (112));
    auto rackFooter = rackContent.removeFromBottom (48);
    addLayerButton.setBounds (rackFooter.reduced (0, 3));
    viewport.setBounds (rackContent);

    layerList->setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()),
                        layerList->getHeight());
    layerList->resized();

    if (layerInspector != nullptr)
        layerInspector->setBounds (inspectorPanelBounds);
}

void StackSamplerAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (ownerProcessor.getActiveLayers() == displayedLayerOrder)
    {
        layerList->refresh();
        if (layerInspector != nullptr)
            layerInspector->refresh();
        return;
    }

    rebuildLayers();
}

void StackSamplerAudioProcessorEditor::timerCallback()
{
    layerList->pollParameterChanges();
    if (layerInspector != nullptr)
        layerInspector->pollParameterChanges();
}

void StackSamplerAudioProcessorEditor::rebuildLayers()
{
    const auto previousScroll = viewport.getViewPositionY();
    const auto activeLayers = ownerProcessor.getActiveLayers();
    displayedLayerOrder = activeLayers;

    if (std::find (activeLayers.begin(), activeLayers.end(), selectedBank)
        == activeLayers.end())
        selectedBank = activeLayers.empty() ? -1 : activeLayers.front();

    layerList->rebuild (selectedBank);
    layerInspector.reset();

    const auto selected = std::find (activeLayers.begin(), activeLayers.end(), selectedBank);
    if (selected != activeLayers.end())
    {
        const auto ordinal = static_cast<int> (std::distance (activeLayers.begin(), selected)) + 1;
        layerInspector = std::make_unique<LayerInspector> (
            ownerProcessor, selectedBank, ordinal,
            [this] (int bank) { removeLayer (bank); });
        addAndMakeVisible (*layerInspector);
    }

    const auto count = static_cast<int> (activeLayers.size());
    layerCountLabel.setText (juce::String (count) + " / "
                                 + juce::String (stacksampler::kMaxLayers),
                             juce::dontSendNotification);
    addLayerButton.setEnabled (count < stacksampler::kMaxLayers);

    resized();
    viewport.setViewPosition (0, previousScroll);
    repaint();
}

void StackSamplerAudioProcessorEditor::selectLayer (int bank, bool scrollIntoView)
{
    const auto activeLayers = ownerProcessor.getActiveLayers();
    const auto selected = std::find (activeLayers.begin(), activeLayers.end(), bank);
    if (selected == activeLayers.end())
        return;

    const auto selectionChanged = selectedBank != bank || layerInspector == nullptr;
    selectedBank = bank;
    layerList->setSelectedBank (selectedBank);

    if (selectionChanged)
    {
        layerInspector.reset();
        const auto ordinal
            = static_cast<int> (std::distance (activeLayers.begin(), selected)) + 1;
        layerInspector = std::make_unique<LayerInspector> (
            ownerProcessor, selectedBank, ordinal,
            [this] (int layerBank) { removeLayer (layerBank); });
        addAndMakeVisible (*layerInspector);
        resized();
    }

    if (scrollIntoView)
    {
        const auto card = layerList->getCardBoundsForBank (bank);
        const auto visibleTop = viewport.getViewPositionY();
        const auto visibleBottom = visibleTop + viewport.getViewHeight();
        auto targetY = visibleTop;

        if (card.getY() < visibleTop)
            targetY = juce::jmax (0, card.getY() - cardGap);
        else if (card.getBottom() > visibleBottom)
            targetY = juce::jmax (0, card.getBottom() - viewport.getViewHeight() + cardGap);

        viewport.setViewPosition (0, targetY);
    }

    repaint();
}

void StackSamplerAudioProcessorEditor::removeLayer (int bank)
{
    const auto activeLayers = ownerProcessor.getActiveLayers();
    const auto removed = std::find (activeLayers.begin(), activeLayers.end(), bank);
    if (removed == activeLayers.end())
        return;

    if (selectedBank == bank)
    {
        const auto index = static_cast<std::size_t> (std::distance (activeLayers.begin(), removed));
        if (activeLayers.size() <= 1)
            selectedBank = -1;
        else if (index + 1 < activeLayers.size())
            selectedBank = activeLayers[index + 1];
        else
            selectedBank = activeLayers[index - 1];
    }

    ownerProcessor.removeLayer (bank);
    rebuildLayers();
}
