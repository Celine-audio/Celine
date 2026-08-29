#pragma once

#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace SchematicUI
{
    //==========================================================================
    /**
        The preset control in the toolbar.

        A button that says what is loaded rather than what it does. "Presets"
        told you a menu was behind it and nothing else -- which patch you were
        hearing, whether you had edited it since, whether you had loaded one at
        all, none of that was anywhere on screen.

        So it reads as a dropdown: a small caption, the name of the loaded
        preset, and a chevron. The dot appears once the drawing no longer matches
        the file it came from.
    */
    class PresetButton : public juce::Button
    {
       public:
        PresetButton() : juce::Button ("Presets")
        {
            // The canvas needs the keyboard for its part and tool shortcuts, and
            // a toolbar button that takes focus on click silently kills them.
            setWantsKeyboardFocus (false);
        }

        /** The preset now loaded, or {} for none.

            `factory` picks the caption: a shipped example and a file in the
            user's folder are both presets, but only one of them can be
            overwritten by Save, so the button says which you are looking at. */
        void setPresetName (const juce::String& name, bool factory = false)
        {
            if (presetName == name && isFactory == factory)
                return;

            presetName = name;
            isFactory = factory;
            repaint();
        }

        bool isFactoryPreset() const noexcept { return isFactory; }

        /** Whether the sheet has been edited since that preset was loaded. */
        void setModified (bool nowModified)
        {
            if (modified == nowModified)
                return;

            modified = nowModified;
            repaint();
        }

        const juce::String& getPresetName() const noexcept { return presetName; }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);
            const bool loaded = presetName.isNotEmpty();

            g.setColour (down || highlighted ? Theme::surfaceBright() : Theme::surface());
            g.fillRoundedRectangle (bounds, Theme::cornerRadius);

            g.setColour (Theme::line());
            g.drawRoundedRectangle (bounds, Theme::cornerRadius, Theme::borderWidth);

            auto content = bounds.reduced (6.0f, 3.0f);

            // The dot goes right, where it does not shift the name about as it
            // comes and goes.
            if (loaded && modified)
            {
                auto dot = content.removeFromRight (12.0f);
                g.setColour (Theme::pending());
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot.getCentre()));
            }

            // Two lines, left aligned, both in the label weight -- the design
            // stacks the caption above the name rather than putting them side by
            // side, which is what lets the field be mostly empty and still read.
            const auto caption = content.removeFromTop (content.getHeight() * 0.5f);

            g.setFont (Fonts::light (11.0f));
            g.setColour (isFactory && loaded ? Theme::violet() : Theme::comment());
            g.drawText (isFactory && loaded ? "FACTORY" : "PRESET", caption,
                        juce::Justification::bottomLeft, false);

            g.setColour (loaded ? Theme::textDim() : Theme::comment());
            g.drawText (loaded ? presetName.toUpperCase() : "NONE", content,
                        juce::Justification::topLeft, true);
        }

       private:
        juce::String presetName;
        bool isFactory = false;
        bool modified = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetButton)
    };

    //==========================================================================
    /**
        A toolbar button that is an icon rather than a word.

        The drawable is recoloured to match the rest of the toolbar rather than
        drawn in whatever colours the artwork happens to carry, so one icon set
        can sit next to text buttons without looking pasted on.
    */
    class IconButton : public juce::Button
    {
       public:
        /** Forces every path in a drawable to one colour.

            The implementation lives in EmbeddedAssets, which is where everything
            else that loads artwork already reaches for it -- this was a verbatim
            second copy of it, and two copies of "which parts of an icon get
            recoloured" is one more than can be kept in step. Kept as a name here
            because PowerButton and the editor both call it through the button. */
        static void tint (juce::Drawable& drawable, juce::Colour colour)
        {
            Assets::tint (drawable, colour);
        }

        /** `name` is what the tooltip and the accessibility layer say -- an icon
            with no name is a guess for anyone who can't see it. */
        IconButton (const juce::String& name, std::unique_ptr<juce::Drawable> drawable)
            : juce::Button (name), icon (std::move (drawable))
        {
            setTooltip (name);
            setWantsKeyboardFocus (false);
        }

        /** Whether this button's mode is the one in force. The tool buttons are
            the only mutually exclusive things in the row, and the design answers
            "which one am I holding" with the teal fill rather than with a shade
            you have to compare against its neighbour. */
        void setActive (bool nowActive)
        {
            if (active == nowActive)
                return;

            active = nowActive;
            repaint();
        }

        /** Turns off the button's own fill and border, for buttons that sit
            inside a shared housing -- undo and redo live in one, so two frames
            would draw a line down the middle of it in the wrong place. Hover
            and press still show. */
        void setDrawsFrame (bool shouldDraw)
        {
            drawsFrame = shouldDraw;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);

            // JUCE withholds the mouse from a disabled component, so highlighted
            // and down are already false here -- what was missing is that the
            // resting look was identical either way, which left Rotate and Flip
            // looking clickable with nothing selected.
            const bool usable = isEnabled();

            if (active)
            {
                g.setColour (Theme::toolActive());
                g.fillRoundedRectangle (bounds, Theme::cornerRadius);
            }
            else if (drawsFrame)
            {
                g.setColour (down || highlighted ? Theme::surfaceBright() : Theme::surface());
                g.fillRoundedRectangle (bounds, Theme::cornerRadius);
            }
            else if (down || highlighted)
            {
                // Frameless, so only the hover shows -- and it stays inside the
                // housing that is drawn behind it.
                g.setColour (Theme::surfaceBright());
                g.fillRoundedRectangle (bounds.reduced (1.0f), Theme::cornerRadius * 0.6f);
            }

            if (active || drawsFrame)
            {
                g.setColour (usable ? Theme::line() : Theme::lineDisabled());
                g.drawRoundedRectangle (bounds, Theme::cornerRadius, Theme::borderWidth);
            }

            if (icon == nullptr)
                return;

            auto drawn = icon->createCopy();
            tint (*drawn, ! usable                        ? Theme::textDisabled()
                          : active || highlighted || down ? Theme::text()
                                                          : Theme::textDim());
            drawn->drawWithin (g, bounds.reduced (bounds.getWidth() * iconInset),
                               juce::RectanglePlacement::centred, 1.0f);
        }

       protected:
        juce::Drawable* getIcon() const noexcept { return icon.get(); }

        /** How far in from the button edge the glyph is drawn, as a fraction of
            the width. Shared, so a subclass that paints its own background
            still lands the icon in exactly the same place.

            Measured off the design, which fills about twenty of a 33px button
            with glyph. It was 0.26 -- a guess -- which left fifteen, and a row of
            icons two-thirds the size the mockup draws them reads as a row of
            small print rather than a row of buttons. */
        static constexpr float iconInset = 0.18f;

       private:
        std::unique_ptr<juce::Drawable> icon;
        bool active = false;
        bool drawsFrame = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
    };

    //==========================================================================
    /**
        The bypass control: a power button that only speaks up when it has
        something to say.

        Running is the ordinary state, so it draws exactly like every other
        button in the row -- a toolbar where one button is permanently lit
        teaches you to stop seeing it. Bypassed is the state worth noticing,
        because the plugin is passing audio through untouched and that is
        usually not what you meant, so that is the one that goes red.

        Note the parameter is called *bypass*: getToggleState() is true when the
        plugin is doing nothing. Reading it the other way round is the kind of
        bug you stare straight through, so it is unpacked into a named bool.
    */
    class PowerButton : public IconButton
    {
       public:
        PowerButton (const juce::String& name, std::unique_ptr<juce::Drawable> drawable)
            : IconButton (name, std::move (drawable))
        {
            // The APVTS attachment drives the toggle state, and a click has to
            // move it or the attachment never sees anything.
            setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const bool bypassed = getToggleState();

            if (! bypassed)
            {
                // Nothing to report: the circuit is running, so this is just
                // another button.
                IconButton::paintButton (g, highlighted, down);
                return;
            }

            // Exactly the geometry IconButton uses, and deliberately not its own:
            // only the *colour* is allowed to change with the state. This had its
            // own inset, its own 4px radius and its own 1.4px border, so engaging
            // bypass visibly reshaped the button -- a control that changes shape
            // when you press it reads as a rendering fault, and it broke the row's
            // one rhythm at the one moment you are looking straight at it.
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);

            g.setColour (Theme::danger().withAlpha (down || highlighted ? 0.34f : 0.22f));
            g.fillRoundedRectangle (bounds, Theme::cornerRadius);

            g.setColour (Theme::danger());
            g.drawRoundedRectangle (bounds, Theme::cornerRadius, Theme::borderWidth);

            if (auto* icon = getIcon())
            {
                auto drawn = icon->createCopy();
                tint (*drawn, Theme::danger());

                // The same inset the plain state uses. A different one here
                // would make the glyph jump size every time you toggled it,
                // which reads as a rendering glitch rather than a state change.
                drawn->drawWithin (g, bounds.reduced (bounds.getWidth() * iconInset),
                                   juce::RectanglePlacement::centred, 1.0f);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerButton)
    };
} // namespace SchematicUI
