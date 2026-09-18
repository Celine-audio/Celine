#pragma once

#include "ThemePalette.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        The palette, in one place, shared across the house plugins.

        Two rules. Nothing outside this header names a hex value; and the accents are
        named for the job they do rather than for the colour they are, so a change of
        palette does not have to be chased through the call sites.

        The design is two-tone, and that is the thing to hold on to when adding
        anything: the chrome is dark aubergine and the canvas darker still, while the
        panels you reach into are near-white. A new widget has to know which side of
        that line it sits on, because the text colour flips with it.

        Only the dark half is here. A plugin that actually puts a light panel in front
        of you declares its ground and its ink itself -- see PluginThemeRoles.h -- for
        the same reason nothing else here is offered to a window that cannot paint with
        it. The two-tone rule is the house's; the second tone is not always present.

        **Every one of these is a lookup, not a constant.** What they answer is whatever
        the theme in force says -- see ThemePalette.h. Two consequences worth knowing
        before writing a control:

        - Read them **at paint time**. A colour taken once in a constructor and handed to
          `setColour` is a snapshot, and a snapshot does not follow a theme change. Where
          a JUCE widget insists on being told its colours, take them in an override of
          `lookAndFeelChanged()`, which the window calls on every child when the theme
          moves.
        - The shipped values, the editor's labels and the keys a `.celthm` file uses all
          live in ThemeRoles.h. Adding a colour means adding it there; this header is
          where it is given a name and a reason.
    */
    namespace Theme
    {
        //======================================================================
        // The window. The frame everything else sits in.

        /** The ground the whole window is painted on, darker than anything else so the
            panels and the graph read as things laid on it rather than holes in it. */
        inline juce::Colour consoleBackground() { return colour (Role::consoleBackground); }

        /** The step up from that ground: the graph's own panel, and the well a control
            sits in. Anything you look *into* rather than at. */
        inline juce::Colour background() { return colour (Role::background); }

        /** Borders, on controls and panels alike. A rule you can take hold of and drag
            is a control rather than a border, and belongs to whichever plugin has
            one. */
        inline juce::Colour line() { return colour (Role::line); }

        //======================================================================
        // The header. Only what the toolbar band owns outright.
        //
        // The icon buttons are deliberately not here. A button is a control wherever it
        // is standing, and GALLERY puts the same class in a side panel; filing its ink
        // under Header would mean changing the header moved something in another corner
        // of a different plugin. They read from the Controls and Text roles below.

        /** The toolbar band itself. */
        inline juce::Colour headerBackground() { return colour (Role::headerBackground); }

        /** Every white mark in the chrome: the logo, the wordmark, and the glyph in an
            icon button. All three are artwork tinted rather than text drawn, all three
            stand on the dark chrome, and they are meant to look like one another -- so
            they are one colour rather than two that have to be set to the same value.

            Its own role rather than text(), which the marks followed until recently:
            these are the brand rather than the interface, and the last thing most themes
            want moved when a label moves. */
        inline juce::Colour headerText() { return colour (Role::headerText); }

        //======================================================================
        // The graph, as far as every plugin in the house has one: a ruler and the
        // captions on it. What is *drawn* on the graph -- curves, bands, whatever this
        // plugin plots -- is the plugin's own, in PluginTheme.h.

        /** Axis captions and the frequency ruler. */
        inline juce::Colour graphText() { return colour (Role::graphText); }

        /** Grid lines. Barely there on purpose -- they are a ruler you read against,
            not part of the picture. */
        inline juce::Colour grid() { return colour (Role::grid); }

        //======================================================================
        // Controls. What you press, drag and hover, wherever it happens to stand.

        /** What a button is filled with -- text buttons, icon buttons, and a dropdown,
            which is a button that opens a menu.

            Its own role rather than surface(), which it ships equal to: a button is a
            thing you press and a surface is a thing you read, and a theme that could
            not tell them apart could not make the controls stand out from the panels
            they sit on. surfaceBright() is still the hover, for both. */
        inline juce::Colour button() { return colour (Role::button); }

        /** What a text field is filled with -- anything you type into.

            Ships equal to background(), the darkest ground, because a field reads as a
            hole you put something in rather than as a raised control. Separate for the
            same reason button() is: a theme has to be able to say where typing happens
            without moving the canvas with it. */
        inline juce::Colour field() { return colour (Role::field); }

        /** The dark slate a control is built from: the popup's body, the tooltip, and
            anything else that is a surface rather than a thing you press. */
        inline juce::Colour surface() { return colour (Role::surface); }

        /** Hover and selection, a step up from surface. */
        inline juce::Colour surfaceBright() { return colour (Role::surfaceBright); }

        /** The unfilled part of a knob's ring and of a slider's track. */
        inline juce::Colour track() { return colour (Role::track); }

        /** The cap of a knob and the grip of a slider: the thing your hand goes to.

            Its own role rather than panel(), which it used to be. The two ship at the
            same near-white, but one is a ground you read dark ink off and the other is
            a small bright object you reach for, and a theme that could not tell them
            apart could not darken its panels without the knobs going with them. */
        inline juce::Colour handle() { return colour (Role::handle); }

        //======================================================================
        // Text. Two families, because of the two-tone split described above: ink on the
        // dark chrome, and ink on the light panels. Kept out of the area groups on
        // purpose -- these are used by controls standing on both halves, so filing them
        // under one area would be wrong the first time somebody changed it.

        /** On chrome. Céline White -- the same value the light panels are, because the
            ink on the dark half of the design and the ground on the light half are one
            colour used two ways. It was Monokai's warm off-white, which put a faintly
            yellow white beside a faintly blue one wherever the two halves met. */
        inline juce::Colour text() { return colour (Role::text); }
        inline juce::Colour textDim() { return colour (Role::textDim); }
        inline juce::Colour comment() { return colour (Role::comment); }

        /** Ink for a control that cannot be used right now. Several steps below
            textDim(), which is the *idle* look of a control that does work: if the two
            were close, "greyed out" and "not hovered" would look the same.

            Its own role rather than an alias of comment(), which is what it used to be:
            a theme has to be able to pull them apart, and the two happening to ship at
            the same value is not the same as their being one colour. */
        inline juce::Colour textDisabled() { return colour (Role::textDisabled); }

        //======================================================================
        // Panels.

        /** The dark ground a popup is built on -- the About sheet, the Theme window.
            Named for the chrome it matches rather than for the header, which has had
            its own colour since the band and the popups stopped having to agree. */
        inline juce::Colour chrome() { return colour (Role::chrome); }

        //======================================================================
        // Accents.

        /** The primary accent: whatever the plugin is doing to the signal. Every filled
            control uses this, so changing it here re-skins the plugin.

            It is also what a control in force is filled with -- a button that is on, a
            switch thrown, an icon lit. That used to be a role of its own, and it was
            reachable in neither plugin: every button that lights up says which colour
            to light up in, so the role sat in the editor doing nothing. */
        inline juce::Colour accent() { return colour (Role::accent); }

        //======================================================================
        // States.

        inline juce::Colour danger() { return colour (Role::danger); }
        inline juce::Colour error()  { return colour (Role::error); }

        // This plugin's own colours, if it has any, are declared in PluginTheme.h and
        // included at the end of this namespace -- the same extension point
        // PluginThemeRoles.h is for the roles themselves.

        //======================================================================
        // What a control does under the pointer, which is a rule rather than a colour.
        //
        // Stated once because it has to be the same everywhere: a hover that is its own
        // themeable colour is a second thing to keep in step with the first, and the
        // two drift the moment somebody changes one. This lifts whatever the control is
        // already filled with towards the ink, so a button, an icon and a dropdown all
        // answer the pointer in the same voice whatever they are painted.

        /** `fill`, lifted for a pointer over it and lifted further for one held down. */
        inline juce::Colour underPointer (juce::Colour fill, bool hovered, bool held)
        {
            if (! (hovered || held))
                return fill;

            return fill.overlaidWith (text().withAlpha (held ? 0.16f : 0.08f));
        }

        //======================================================================
        // Geometry the mockup is consistent about, stated once rather than sprinkled
        // through four files as literals. Not themeable: a layout is not a colour, and
        // a theme that could move these would be a theme that could break the window.

        /** Corner radius on every button, field and pill. */
        inline constexpr float cornerRadius = 8.0f;

        /** Border weight on buttons and fields. */
        inline constexpr float borderWidth = 1.2f;

        /** Toolbar buttons are square, and sit on a pitch of size + gap. */
        inline constexpr int buttonSize = 33;
        inline constexpr int buttonGap = 7;

        /** The toolbar band: 33px buttons with 6px of air above and below. */
        inline constexpr int toolbarHeight = 45;

        /** The band a panel's own title sits in. Stated here because two panels wear
            it side by side -- the graph's tabs and the library's header -- and the two
            being a few pixels apart reads as one of them having slipped. */
        inline constexpr int tabBarHeight = 42;
        //======================================================================
        // Whatever this plugin adds to the house palette. Included here, inside the
        // namespace, so a plugin's accessors read exactly like the shared ones at
        // every call site -- `Theme::irSlot(2)` and `Theme::chrome()` alike.
        #include "../PluginTheme.h"
    } // namespace Theme

} // namespace Celine
