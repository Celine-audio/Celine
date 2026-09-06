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
        // Surfaces.

        /** Titlebar, toolbar, inspector, ruler chrome. */
        inline juce::Colour chrome() { return colour (Role::chrome); }

        /** The sheet you draw on. */
        inline juce::Colour background() { return colour (Role::background); }

        /** The light panels. */
        inline juce::Colour panel() { return colour (Role::panel); }

        /** The dark slate that sits on chrome: the popup panel, the tooltip, and
            anything else that is a surface rather than a control. */
        inline juce::Colour surface() { return colour (Role::surface); }

        /** Hover and selection, a step up from surface. */
        inline juce::Colour surfaceBright() { return colour (Role::surfaceBright); }

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

        /** Borders. */
        inline juce::Colour line() { return colour (Role::line); }

        /** The graph's ground, darker than anything else so it reads as a hole. */
        inline juce::Colour consoleBackground() { return colour (Role::consoleBackground); }

        /** A row in a list. */
        inline juce::Colour pill() { return colour (Role::pill); }

        /** Grid lines. Barely there on purpose -- they are a ruler you read against,
            not part of the picture. */
        inline juce::Colour grid() { return colour (Role::grid); }

        //======================================================================
        // Text. Two families, because of the two-tone split above.

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

        /** On the light panels, where the above would be invisible. */
        inline juce::Colour textOnPanel() { return colour (Role::textOnPanel); }

        //======================================================================
        // Accents.

        inline juce::Colour teal() { return colour (Role::teal); }
        inline juce::Colour violet() { return colour (Role::violet); }

        /** The primary accent: whatever the plugin is doing to the signal. Every filled
            control uses this, so changing it here re-skins the plugin. */
        inline juce::Colour accent() { return colour (Role::accent); }

        /** A second accent, for when one curve or channel has to be told apart from
            another. Sits opposite the primary on the wheel. */
        inline juce::Colour accentAlt() { return colour (Role::accentAlt); }

        /** Anything live and committing. */
        inline juce::Colour record() { return colour (Role::record); }

        /** The unfilled part of a knob's ring and of the cut slider's track. */
        inline juce::Colour track() { return colour (Role::track); }

        /** A control that is armed or in force. */
        inline juce::Colour toolActive() { return colour (Role::toolActive); }

        inline juce::Colour danger()  { return colour (Role::danger); }
        inline juce::Colour warning() { return colour (Role::warning); }
        inline juce::Colour error()   { return colour (Role::error); }

        // This plugin's own colours, if it has any, are declared in PluginTheme.h and
        // included at the end of this namespace -- the same extension point
        // PluginThemeRoles.h is for the roles themselves.

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
