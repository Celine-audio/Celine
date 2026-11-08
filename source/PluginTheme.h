// Céline's own colour accessors, included inside namespace Celine::Theme by
// CelineUI/Theme.h. Roles are declared in PluginThemeRoles.h. A fragment: no include
// guard, no includes.

        // ======================================================================
        // The drawing.

        /** A wire. */
        inline juce::Colour wire() { return colour (Role::wire); }

        /** A part that is not selected. */
        inline juce::Colour element() { return colour (Role::element); }

        /** A selected part. */
        inline juce::Colour selected() { return colour (Role::selected); }

        /** The ruler's cursor marker. */
        inline juce::Colour cursorMark() { return colour (Role::cursorMark); }

        /** The circuit has been edited and not rebuilt. */
        inline juce::Colour pending() { return colour (Role::pending); }

        // ======================================================================
        // Part captions.

        /** A part's name or model. */
        inline juce::Colour captionName() { return colour (Role::captionName); }

        /** A value that has been entered. */
        inline juce::Colour captionValue() { return colour (Role::captionValue); }

        /** A value still at zero (the build refuses it). */
        inline juce::Colour captionUnset() { return colour (Role::captionUnset); }

        // ======================================================================
        // Selection box rules.

        /** Left-to-right: takes only what fits entirely inside. */
        inline juce::Colour boxEnclose() { return colour (Role::boxEnclose); }

        /** Right-to-left: takes anything it touches. */
        inline juce::Colour boxCrossing() { return colour (Role::boxCrossing); }

        //======================================================================

        // ======================================================================
        // The light panels.

        /** The near-white ground: the control strip, and the panel beside the sheet. */
        inline juce::Colour panel() { return colour (Role::panel); }

        /** Text on the light panel. */
        inline juce::Colour textOnPanel() { return colour (Role::textOnPanel); }

        /** A row on the light panel: a palette entry, a listed part. */
        inline juce::Colour pill() { return colour (Role::pill); }

        /** Diagnostic severity: notice (below warning and error). */
        inline juce::Colour notice() { return colour (Role::notice); }

        /** Diagnostic severity: warning. */
        inline juce::Colour warning() { return colour (Role::warning); }

        /** Delete button. */
        inline juce::Colour discard() { return colour (Role::discard); }

        // ======================================================================
        // Group box colours, offered by name in the box's colour menu.

        inline juce::Colour boxGrey()   { return colour (Role::boxGrey); }
        inline juce::Colour boxBlue()   { return colour (Role::boxBlue); }
        inline juce::Colour boxGreen()  { return colour (Role::boxGreen); }
        inline juce::Colour boxAmber()  { return colour (Role::boxAmber); }
        inline juce::Colour boxRed()    { return colour (Role::boxRed); }
        inline juce::Colour boxViolet() { return colour (Role::boxViolet); }

        /** Indexed by the Box model's position in Element.h, so a colour added there needs
            one here. Out of range gives grey. */
        inline juce::Colour groupBox (int modelIndex)
        {
            switch (modelIndex)
            {
                case 1:  return boxBlue();
                case 2:  return boxGreen();
                case 3:  return boxAmber();
                case 4:  return boxRed();
                case 5:  return boxViolet();
                default: return boxGrey();
            }
        }


        // ======================================================================
        // Layout constants.

        /** Fixed width of the preset field. */
        inline constexpr int presetWidth = 302;
        inline constexpr int rebuildWidth = 91;


        /** Fixed width of the inspector card. */
        inline constexpr int inspectorWidth = 260;

        /** Palette row pitch; the pill inside is 4 px shorter. */
        inline constexpr int paletteRowHeight = 35;

        /** Largest palette font size; it shrinks if the longest name does not fit
            paletteColumnWidth. */
        inline constexpr float paletteFontHeight = 11.5f;

        /** Width of one of the palette's two columns: symbol, longest name, padding. */
        inline constexpr int paletteColumnWidth = 118;

        /** Font size of the bottom band's captions and knob values. */
        inline constexpr float bandCaptionFontHeight = 12.6f;
        inline constexpr float bandValueFontHeight = 11.55f;

        /** Margin between a floating panel and the ruler, sheet edge, bottom band or another
            panel. */
        inline constexpr int floatingMargin = 16;

        /** Corner radii of the floating panels and palette entries. */
        inline constexpr float paletteCornerRadius = 9.0f;
        inline constexpr float paletteEntryCornerRadius = 4.0f;
        inline constexpr float inspectorCornerRadius = 14.0f;
        inline constexpr float consoleCornerRadius = 13.0f;

        /** Corner radius of the bottom band's three bars. */
        inline constexpr float bandCornerRadius = 13.0f;
