#pragma once

#include <CelineUI/LookAndFeelBase.h>

/** Céline's look: the shared LookAndFeelBase plus its own knob (`knob.svg`, rotated
    by the value), and the house switch coloured for the light panels it sits on. */

/** Marks a Slider as the plugin's own input/output gain, which gets its own knob. */
inline constexpr const char* digitalGainProperty = "celineDigitalGain";

/** Marks a Slider whose value box sits on the light bottom panel, so it uses the
    panel's text colour. */
inline constexpr const char* panelReadoutProperty = "celinePanelReadout";

/** Marks an inspector field or dropdown; dropdown text is left-aligned. */
inline constexpr const char* inspectorFieldProperty = "celineInspectorField";

class PluginLookAndFeel : public LookAndFeelBase
{
public:
    PluginLookAndFeel();
    ~PluginLookAndFeel() override;

    /** Applies the palette, then re-tints the knob artwork. */
    void applyPalette() override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox(juce::Slider&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

private:
    /** Knob cap for controls drawn on the circuit. */
    std::unique_ptr<juce::Drawable> knob;

    /** Knob cap for the plugin's input and output gains. */
    std::unique_ptr<juce::Drawable> ioKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLookAndFeel)
};
