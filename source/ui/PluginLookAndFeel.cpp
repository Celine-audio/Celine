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

    // The switches sit on the light panels.
    setColour(pillSwitchTrackColourId, Theme::pill().darker(0.18f));
    setColour(pillSwitchDotColourId, Theme::panel());
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
