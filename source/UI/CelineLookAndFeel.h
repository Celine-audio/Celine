#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace SchematicUI
{
    //==========================================================================
    /**
        Applies the palette and the typeface to everything JUCE draws for us.

        Two jobs. The first is colour: sliders, combo boxes, popup menus and the
        file dialogs are drawn by the LookAndFeel and not by us, so without this
        half the window is the design and the other half is JUCE's default grey.

        The second is the two controls the design draws its own way:

        - **the knob**, which is `knob.svg` rotated by the value rather than an
          arc and a pointer. There is no value track in the design, so there is
          none here -- the notch is the readout.
        - **the switch**, a pill with a travelling dot rather than a tick box.

        Lives in a .cpp, unlike the palette it reads, because it needs the
        artwork out of BinaryData -- and `Theme.h` is included nearly everywhere,
        so it has no business pulling the binary data header in with it.
    */
    /** Marks a Slider as the plugin's own gain rather than a knob on the drawn
        circuit, so the look and feel can give it a shape of its own.

        A property rather than a subclass because the look and feel is the only
        thing that cares, and rather than a bare string at both ends because two
        spellings of it would fail silently -- the knob would simply come out
        looking like all the others. */
    inline constexpr const char* digitalGainProperty = "celineDigitalGain";

    /** Marks a ToggleButton as one of ours, so it draws as the design's pill.
        Untagged ones keep JUCE's tick box -- in the standalone this is the
        default look and feel, so the Audio/MIDI dialog's boxes resolve here. */
    inline constexpr const char* pillSwitchProperty = "celinePillSwitch";

    class CelineLookAndFeel : public juce::LookAndFeel_V4
    {
       public:
        CelineLookAndFeel();
        ~CelineLookAndFeel() override;

        void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

        /** Rounded like every other field, and in the component's own colours --
            the channel picker sits on a light panel and wears the accent, the
            inspector's boxes sit on chrome and wear the dark slate. */
        void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonW, int buttonH,
                          juce::ComboBox&) override;

        void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

        /** Centred optically rather than geometrically -- see opticalRise in the
            .cpp for why the two are not the same in Jura. */
        void drawButtonText(juce::Graphics&, juce::TextButton&,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

        //======================================================================
        /** Jura, for everything JUCE picks a font for itself.

            These matter more than they look like they should. A font built from
            `FontOptions(height)` carries no typeface, so it is resolved at render
            time against the **default** LookAndFeel -- not the one the component
            is using. Setting the default sans-serif face on *this* instance
            therefore does nothing for any of them, which is why half the window
            was Jura and half was the platform sans. */
        juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;

        /** A menu's section headers.

            Overridden because JUCE draws them as `getPopupMenuFont().boldened()`
            -- and boldening is the one thing this font cannot be asked for.
            Fonts::light is built from an explicitly loaded Jura-Light typeface,
            which has no bold face to find, so the request falls back off Jura
            altogether and the header comes out in the platform sans at a weight
            nothing else in the window uses. Jura-Bold is a real file here, so
            this asks for it by name instead of asking for a variant. */
        void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area,
                                        const juce::String& sectionName) override;
        juce::Font getLabelFont(juce::Label&) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;

       private:
        /** The knob face, parsed once. Null if the asset is missing, in which
            case the rotary falls back to JUCE's own drawing rather than
            vanishing. */
        std::unique_ptr<juce::Drawable> knob;

        /** The same, for the plugin's own gain: `knob_io.svg`. A second file
            rather than a flag on the first, on the same principle the element
            artwork follows -- "redraw the input/output knob" is redrawing one
            drawing. */
        std::unique_ptr<juce::Drawable> ioKnob;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CelineLookAndFeel)
    };
} // namespace SchematicUI
