// Céline's own colour accessors, included inside namespace Celine::Theme by
// ui/Theme.h. The roles behind them are declared in PluginThemeRoles.h.
//
// No include guard and no includes of its own: this is a fragment, included at one
// point inside a namespace, and anything it needs is already there.

        //======================================================================
        // What each colour *means* on the drawing. Named by job rather than by colour,
        // so the schematic's conventions survive a change of palette -- which is
        // exactly what a theme is.

        /** A wire. */
        inline juce::Colour wire() { return colour (Role::wire); }

        /** A part that is not selected. */
        inline juce::Colour element() { return colour (Role::element); }

        /** A part that is. */
        inline juce::Colour selected() { return colour (Role::selected); }

        /** The ruler's cursor marker. Its own role rather than the selection colour it
            once borrowed: that colour means "this part is selected" everywhere else on
            the sheet, and a mark following the pointer is not a selection. */
        inline juce::Colour cursorMark() { return colour (Role::cursorMark); }

        /** The circuit has been edited and not rebuilt. */
        inline juce::Colour pending() { return colour (Role::pending); }

        //======================================================================
        // Captions, which say three different things and have to be told apart at a
        // glance while reading a sheet full of them.

        /** A part's name or model. */
        inline juce::Colour captionName() { return colour (Role::captionName); }

        /** A value someone actually entered. */
        inline juce::Colour captionValue() { return colour (Role::captionValue); }

        /** A value still sitting at zero. It refuses to build, so it shouts -- being
            the loudest thing in the palette is the point of it, not an accident. */
        inline juce::Colour captionUnset() { return colour (Role::captionUnset); }

        //======================================================================
        // The two selection-box rules, which have to be told apart at a glance. Kept as
        // roles rather than as aliases because *which* two colours they are matters
        // less than their being obviously different from each other -- and a theme that
        // could not keep them apart would break the only thing they say.

        /** Left-to-right: takes only what fits entirely inside. */
        inline juce::Colour boxEnclose() { return colour (Role::boxEnclose); }

        /** Right-to-left: takes anything it touches. */
        inline juce::Colour boxCrossing() { return colour (Role::boxCrossing); }

        //======================================================================

        /** The mildest of the three diagnostic severities, under error() and warning()
            in the shared palette. Named `notice` rather than `info` because `info()` is
            already the palette's own role table -- see ThemePalette.h. */
        inline juce::Colour notice() { return colour (Role::notice); }

        /** The button that throws a part away. A muted red rather than the palette's
            danger(): that one is for something going wrong, and deleting a part you
            selected is not that -- it is simply the one button worth hesitating over. */
        inline juce::Colour discard() { return colour (Role::discard); }

        //======================================================================
        // The six colours a group box can be drawn in. Not decoration: a box is how a
        // big sheet is divided into "this is the tone stack" and "this is the power
        // supply", so its colour is the only thing telling one section from another --
        // which is why they are in the theme rather than fixed. Named for the colour
        // they are, unusually, because that is also what the menu offers.

        inline juce::Colour boxGrey()   { return colour (Role::boxGrey); }
        inline juce::Colour boxBlue()   { return colour (Role::boxBlue); }
        inline juce::Colour boxGreen()  { return colour (Role::boxGreen); }
        inline juce::Colour boxAmber()  { return colour (Role::boxAmber); }
        inline juce::Colour boxRed()    { return colour (Role::boxRed); }
        inline juce::Colour boxViolet() { return colour (Role::boxViolet); }

        /** By the position of the model in Element.h's list for a Box, so a colour
            added there needs one added here. Out of range answers grey rather than
            asserting: a wrong hue is a better failure than a box that does not draw. */
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


        //======================================================================
        // Geometry this plugin is consistent about, stated once rather than sprinkled
        // through four files as literals. Not themeable, for the same reason the shared
        // block above it is not: a layout is not a colour, and a theme that could move
        // these would be a theme that could break the window.

        /** The preset field, which the design draws at one fixed width rather than
            letting it take up the slack. */
        inline constexpr int presetWidth = 302;
        inline constexpr int rebuildWidth = 91;

        /** The two side panels, both fixed: they are lists of fixed-width things, so the
            whole of a resize goes to the sheet. */
        inline constexpr int paletteWidth = 188;
        inline constexpr int inspectorWidth = 263;

        /** One palette row, and the pill inside it. */
        inline constexpr int paletteRowHeight = 35;
