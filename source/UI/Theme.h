#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace SchematicUI
{
    //==========================================================================
    /**
        The palette, in one place.

        Monokai, near enough: the canonical background, foreground and six
        accents, plus two greys the original uses for panels and selection. The
        colours were scattered as literals across four files before this, which
        meant "make it darker" was a hunt rather than an edit -- and the same
        yellow existed twice with two slightly different values.

        Two rules worth keeping. Nothing outside this header should name a hex
        value; and the accents are picked for *meaning* rather than for looks, so
        `unset` being the loudest colour in the set is the point, not an
        accident.
    */
    namespace Theme
    {
        //======================================================================
        // Surfaces. Every value here was sampled out of the Figma file rather
        // than eyeballed, which is why they are odd numbers.
        //
        // The design is deliberately two-tone, and that is the thing to hold on
        // to when adding anything: the *chrome* is dark aubergine and the
        // *canvas* is darker still, but the two panels you reach into -- the
        // parts palette and the control strip -- are near-white. A new widget
        // has to know which side of that line it sits on, because the text
        // colour flips with it.

        /** Titlebar, toolbar, inspector, ruler chrome. */
        inline juce::Colour chrome() { return juce::Colour (0xff3b334b); }

        /** The sheet you draw on. */
        inline juce::Colour background() { return juce::Colour (0xff28262e); }

        /** The light panels: parts palette and control strip. */
        inline juce::Colour panel() { return juce::Colour (0xfff9fbff); }

        /** Buttons, fields, dropdowns -- the dark slate that sits on chrome. */
        inline juce::Colour surface() { return juce::Colour (0xff37364a); }

        /** Hover and selection, a step up from surface. */
        inline juce::Colour surfaceBright() { return juce::Colour (0xff4f485d); }

        /** Borders. 1.2px of it around every button in the mockup. */
        inline juce::Colour line() { return juce::Colour (0xffd9d9d9); }

        /** The console, darker than anything else so it reads as a hole. */
        inline juce::Colour consoleBackground() { return juce::Colour (0xff17151a); }

        /** A row in the parts palette. */
        inline juce::Colour pill() { return juce::Colour (0xffdcdee4); }

        /** The dots on the sheet. Barely there on purpose -- they are a ruler you
            aim with, not part of the drawing, and they were `line()` until a
            measurement of the mockup showed that to be five times too bright. */
        inline juce::Colour grid() { return juce::Colour (0xff5c5c5c); }

        //======================================================================
        // Text. Two families, because of the two-tone split above.

        /** On chrome. */
        inline juce::Colour text() { return juce::Colour (0xfff8f8f3); }
        inline juce::Colour textDim() { return juce::Colour (0xffd9d9d9); }
        inline juce::Colour comment() { return juce::Colour (0xff888791); }

        /** Ink for a control that cannot be used right now -- Rotate with
            nothing selected, Undo with nothing to undo.

            Deliberately several steps below textDim(), which is the *idle* look
            of a control that does work: if the two were close, "greyed out" and
            "not hovered" would look the same and the toolbar would stop saying
            anything. */
        inline juce::Colour textDisabled() { return comment(); }

        /** On the light panels, where the above would be invisible. */
        inline juce::Colour textOnPanel() { return juce::Colour (0xff28262e); }

        //======================================================================
        // Accents.

        /** The one accent: an armed tool, a toggle that is on, the channel
            picker. Anything the user has *chosen* wears it. */
        inline juce::Colour teal() { return juce::Colour (0xff8F63D5); }

        /** Wires, and the drawing's own violet. */
        inline juce::Colour violet() { return juce::Colour (0xff9761dc); }

        //======================================================================
        // The two selection-box rules, which have to be told apart at a glance.
        // Kept as roles rather than raw colours because *which* two colours
        // matters less than their being obviously different from each other.

        /** Left-to-right: takes only what fits entirely inside. */
        inline juce::Colour boxEnclose() { return teal(); }

        /** Right-to-left: takes anything it touches. */
        inline juce::Colour boxCrossing() { return juce::Colour (0xffa6e22e); }

        //======================================================================
        // What each colour *means* on the drawing. Named by job rather than by
        // colour, so the schematic's conventions survive a change of palette --
        // which is exactly what just happened to them.

        /** A part's name or model. */
        inline juce::Colour captionName() { return juce::Colour (0xffe6db74); }

        /** A value someone actually entered. */
        inline juce::Colour captionValue() { return juce::Colour (0xffa6e22e); }

        /** A value still sitting at zero -- refuses to build, so it shouts. */
        inline juce::Colour captionUnset() { return juce::Colour (0xfff92672); }

        /** A toolbar mode button that is currently in force. The teal is the whole
            of "which tool am I holding"; a button that is *not* in force simply
            keeps `surface()` like every other button in the row, which is why
            there is no `toolIdle()` to go with this. */
        inline juce::Colour toolActive() { return teal(); }

        inline juce::Colour wire() { return violet(); }
        inline juce::Colour element() { return textDim(); }
        inline juce::Colour selected() { return juce::Colour (0xfffd971f); }

        /** The ruler's cursor marker. Violet rather than the selection orange it
            borrowed: orange means "this part is selected" everywhere else on the
            sheet, and a mark that follows the pointer is not a selection. */
        inline juce::Colour cursorMark() { return violet(); }

        /** The circuit has been edited and not rebuilt. */
        inline juce::Colour pending() { return juce::Colour (0xfffd971f); }

        inline juce::Colour error() { return juce::Colour (0xfff92672); }

        /** Bypass engaged: the plugin is passing audio through untouched, which
            is worth noticing. Its own role rather than captionUnset's red --
            "nobody typed a value" and "the effect is off" are unrelated facts
            that happen to share a hue today. */
        inline juce::Colour danger() { return juce::Colour (0xfff92672); }
        inline juce::Colour warning() { return juce::Colour (0xffe6db74); }
        inline juce::Colour info() { return comment(); }

        //======================================================================
        // Geometry the mockup is consistent about, so that it is stated once
        // rather than sprinkled through four files as literals.

        /** Corner radius on every button, field and pill. */
        /** The border of a control that cannot be used.

            Shared rather than written out at each site: undo and redo draw no
            frame of their own -- the housing behind them is their frame, and it
            is painted by the editor -- so two literals here would be two places
            for the same greying to drift apart, which is exactly how the
            housing came to stay bright while its icons dimmed. */
        inline juce::Colour lineDisabled() { return line().withAlpha (0.25f); }

        inline constexpr float cornerRadius = 8.0f;

        /** Border weight on buttons and fields. */
        inline constexpr float borderWidth = 1.2f;

        /** Toolbar buttons are square, and sit on a pitch of size + gap. */
        inline constexpr int buttonSize = 33;
        inline constexpr int buttonGap = 7;

        /** The toolbar band: 33px buttons with 6px of air above and below. */
        inline constexpr int toolbarHeight = 45;

        /** The preset field, which the design draws at one fixed width rather
            than letting it take up the slack. */
        inline constexpr int presetWidth = 302;
        inline constexpr int rebuildWidth = 91;

        /** The two side panels, both fixed: they are lists of fixed-width things,
            so the whole of a resize goes to the sheet. */
        inline constexpr int paletteWidth = 188;
        inline constexpr int inspectorWidth = 263;

        /** One palette row, and the pill inside it. */
        inline constexpr int paletteRowHeight = 35;
    } // namespace Theme

} // namespace SchematicUI
