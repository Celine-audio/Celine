#include "PluginLookAndFeel.h"

#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/Fonts.h>

using namespace Celine;

PluginLookAndFeel::PluginLookAndFeel()
{
    applyPalette();
}

PluginLookAndFeel::~PluginLookAndFeel() = default;

void PluginLookAndFeel::applyPalette()
{
    LookAndFeelBase::applyPalette();

    // The knob face, tinted here rather than per frame: it is the same colour every
    // time it is drawn, and a drawable copy per knob per repaint would be allocation on
    // the paint path.
    //
    // Re-read from the binary rather than re-tinted in place, because this runs again on
    // every theme change and tinting is destructive -- the second pass would be
    // colouring whatever the first pass left, not the artwork.
    knob = Assets::drawable("knob.svg");
    ioKnob = Assets::drawable("knob_io.svg");

    for (auto* face : { knob.get(), ioKnob.get() })
        if (face != nullptr)
            Assets::tint(*face, Theme::textOnPanel());
}

//==============================================================================
void PluginLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    // Which cap this is: the plugin's own gain wears a smooth one against
    // the fluted cap a drawn knob gets, so the two kinds of control in the
    // bottom band are told apart by the face and not by which end they sit
    // at.
    const auto* cap = slider.getProperties().contains(digitalGainProperty) ? ioKnob.get()
                                                                          : knob.get();

    if (cap == nullptr)
    {
        // No artwork: JUCE's own rotary beats drawing nothing.
        LookAndFeel_V4::drawRotarySlider(g, x, y, width, height, sliderPosProportional,
                                         rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }

    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Square, because the cap is. A knob in a cell taller than it is wide
    // would otherwise be drawn as an ellipse.
    const auto face = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()),
                                                 juce::jmin(area.getWidth(), area.getHeight()));

    const auto angle = rotaryStartAngle
                     + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Graphics::ScopedSaveState state(g);

    // The pointer is drawn straight up in both files, so rotating the whole
    // cap about its centre *is* the readout. No arc: the design has none,
    // and inventing one would be my design rather than the drawn one.
    g.addTransform(juce::AffineTransform::rotation(angle, face.getCentreX(), face.getCentreY()));
    cap->drawWithin(g, face, juce::RectanglePlacement::centred, 1.0f);
}

juce::Label* PluginLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeelBase::createSliderTextBox(slider);

    if (slider.getProperties().contains(panelReadoutProperty))
    {
        label->setColour(juce::Label::textColourId, Theme::textOnPanel());

        // And while it is being typed into. The house sets the editing ink, the
        // caret and the selection for the chrome, where every other field lives
        // -- light on dark, which on this panel is white on white: you could
        // type a value but not see it. The label hands its own colours to the
        // editor it makes, so they are set here rather than on the editor.
        label->setColour(juce::Label::textWhenEditingColourId, Theme::textOnPanel());
        label->setColour(juce::CaretComponent::caretColourId, Theme::textOnPanel());
        label->setColour(juce::TextEditor::highlightColourId, Theme::accent().withAlpha(0.35f));
        label->setColour(juce::TextEditor::highlightedTextColourId, Theme::textOnPanel());
        label->setFont(Fonts::light(Theme::bandValueFontHeight));

        // Typed where it is read. A label's editor starts left-aligned whatever
        // the label is, so a click made the value jump to the cell's edge.
        label->onEditorShow = [label]
        {
            if (auto* editor = label->getCurrentTextEditor())
                editor->setJustification(juce::Justification::centred);
        };
    }

    return label;
}

void PluginLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    if (! button.getProperties().contains(pillSwitchProperty))
    {
        LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted,
                                         shouldDrawButtonAsDown);
        return;
    }

    // One switch everywhere it appears -- the inspector's rows and the control
    // strip's cells, both on the light half of the design: a pale track that
    // turns accent when on, with a white dot that sits on the side that is
    // made. No ON/OFF lettering: the dot's side and the colour already say it.
    //
    // Sized to itself, not stretched to whatever the layout handed the
    // button: a switch as wide as an inspector row reads as a banner.
    const auto bounds = button.getLocalBounds().toFloat();
    const bool labelled = button.getButtonText().isNotEmpty();
    const float h = juce::jmin(bounds.getHeight(), 20.0f);
    const float w = h * 1.8f;

    // Against the right edge with its name on the left, as a settings row is
    // read; centred when the switch is the whole control -- the strip, where
    // the name is already on the caption above it.
    const auto track = juce::Rectangle<float>(w, h).withPosition(
        labelled ? bounds.getRight() - w - 1.0f : bounds.getCentreX() - w * 0.5f,
        bounds.getCentreY() - h * 0.5f);

    const float alpha = button.isEnabled() ? 1.0f : 0.45f;
    const bool lit = button.getToggleState();

    auto fill = lit ? Theme::accent() : Theme::pill().darker(0.18f);

    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        fill = fill.darker(shouldDrawButtonAsDown ? 0.12f : 0.06f);

    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, h * 0.5f);

    const float inset = 2.0f, dot = h - 2.0f * inset;
    g.setColour(Theme::panel().withMultipliedAlpha(alpha));
    g.fillEllipse(track.getX() + inset + (lit ? w - dot - 2.0f * inset : 0.0f),
                  track.getY() + inset, dot, dot);

    if (labelled)
    {
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha));
        g.setFont(Fonts::light(14.0f));
        g.drawText(button.getButtonText(), bounds.withTrimmedRight(w + 8.0f),
                   juce::Justification::centredLeft, true);
    }
}

void PluginLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    if (! box.getProperties().contains(inspectorFieldProperty))
    {
        LookAndFeelBase::positionComboBoxText(box, label);
        return;
    }

    // Left-aligned at the text fields' indent, so a model name lines up with
    // the label typed in the field above it. The chevron keeps the right end.
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setBounds(10, 0, juce::jmax(0, box.getWidth() - 10 - 30), box.getHeight());
    label.setFont(Fonts::light(14.0f));
    label.setJustificationType(juce::Justification::centredLeft);
}
