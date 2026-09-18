#pragma once

#include "LookAndFeelBase.h"

/**
    Céline's look: the house one, plus the two controls this plugin draws its own way.

    Everything else -- the menus, the tooltips, the buttons, the fields, the callout
    bubble -- comes from LookAndFeelBase and is identical to the other Céline plugins.
    What is left here is what a circuit designer genuinely disagrees about:

    - **the knob**, which is `knob.svg` rotated by the value rather than an arc and a
      pointer. There is no value track in the design, so there is none here -- the notch
      is the readout.
    - **the switch**. The house draws a pill too, so this one is an override rather than
      an addition: Céline's says ON and OFF inside it, sizes itself to its own content
      rather than stretching across an inspector row, and takes the panel's dark ink
      because it stands on the light half of the design. `pillSwitchProperty` is the
      house's, and marks a toggle as one of ours in either.
*/

/** Marks a Slider as the plugin's own gain rather than a knob on the drawn circuit, so
    the look and feel can give it a shape of its own.

    A property rather than a subclass because the look and feel is the only thing that
    cares, and rather than a bare string at both ends because two spellings of it would
    fail silently -- the knob would simply come out looking like all the others. */
inline constexpr const char* digitalGainProperty = "celineDigitalGain";

class PluginLookAndFeel : public LookAndFeelBase
{
public:
    PluginLookAndFeel();
    ~PluginLookAndFeel() override;

    /** The house palette, and then the knob artwork, which is tinted from it. */
    void applyPalette() override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

private:
    /** The fluted cap a knob drawn on the circuit wears. */
    std::unique_ptr<juce::Drawable> knob;

    /** The smooth cap the plugin's own input and output gains wear, so the two kinds of
        control in the bottom band are told apart by the face and not by which end they
        sit at. */
    std::unique_ptr<juce::Drawable> ioKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLookAndFeel)
};
