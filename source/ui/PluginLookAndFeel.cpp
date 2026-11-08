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

    // Knob artwork, tinted once per theme change. Re-read from the binary each time
    // because tinting modifies the drawable.
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
    // The plugin's own gains get the smooth cap, circuit knobs the fluted one.
    const auto* cap = slider.getProperties().contains(digitalGainProperty) ? ioKnob.get()
                                                                          : knob.get();

    if (cap == nullptr)
    {
        // No artwork: fall back to JUCE's rotary.
        LookAndFeel_V4::drawRotarySlider(g, x, y, width, height, sliderPosProportional,
                                         rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }

    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Square, like the cap.
    const auto face = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()),
                                                 juce::jmin(area.getWidth(), area.getHeight()));

    const auto angle = rotaryStartAngle
                     + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Graphics::ScopedSaveState state(g);

    // The pointer is drawn pointing up, so rotating the cap shows the value.
    g.addTransform(juce::AffineTransform::rotation(angle, face.getCentreX(), face.getCentreY()));
    cap->drawWithin(g, face, juce::RectanglePlacement::centred, 1.0f);
}

juce::Label* PluginLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeelBase::createSliderTextBox(slider);

    if (slider.getProperties().contains(panelReadoutProperty))
    {
        label->setColour(juce::Label::textColourId, Theme::textOnPanel());

        // Editing colours for the light panel; the label passes them to its editor.
        label->setColour(juce::Label::textWhenEditingColourId, Theme::textOnPanel());
        label->setColour(juce::CaretComponent::caretColourId, Theme::textOnPanel());
        label->setColour(juce::TextEditor::highlightColourId, Theme::accent().withAlpha(0.35f));
        label->setColour(juce::TextEditor::highlightedTextColourId, Theme::textOnPanel());
        label->setFont(Fonts::light(Theme::bandValueFontHeight));

        // Keep the text centred while editing.
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

    // A pale track that turns accent when on, with a white dot on the "on" side.
    // Sized to itself, not to the button.
    const auto bounds = button.getLocalBounds().toFloat();
    const bool labelled = button.getButtonText().isNotEmpty();
    const float h = juce::jmin(bounds.getHeight(), 20.0f);
    const float w = h * 1.8f;

    // Right-aligned with the name on the left when labelled, centred otherwise.
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

    // Left-aligned at the text fields' indent; the chevron keeps the right end.
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setBounds(10, 0, juce::jmax(0, box.getWidth() - 10 - 30), box.getHeight());
    label.setFont(Fonts::light(14.0f));
    label.setJustificationType(juce::Justification::centredLeft);
}
