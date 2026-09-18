#pragma once

#include "EmbeddedAssets.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace Celine
{
    //==========================================================================
    /**
        A toolbar button that is an icon rather than a word. Celine's, verbatim in
        behaviour: the drawable is recoloured to match the rest of the toolbar
        rather than drawn in whatever colours the artwork happens to carry, so one
        icon set can sit next to text buttons without looking pasted on.
    */
    class IconButton : public juce::Button
    {
    public:
        /** `name` is what the tooltip and the accessibility layer say — an icon
            with no name is a guess for anyone who can't see it. */
        IconButton (const juce::String& name, std::unique_ptr<juce::Drawable> drawable)
            : juce::Button (name), icon (std::move (drawable))
        {
            setTooltip (name);
            setWantsKeyboardFocus (false);
        }

        /** Loads the artwork by filename, which is the only way to ask — see
            EmbeddedAssets for why the derived identifier is a trap. */
        IconButton (const juce::String& name, const juce::String& assetFilename)
            : IconButton (name, Assets::drawable (assetFilename))
        {
        }

        /** Whether this button's mode is the one in force. The design answers
            "is this on" with the accent fill rather than with a shade you have to
            compare against its neighbour. */
        void setActive (bool nowActive)
        {
            if (active == nowActive)
                return;

            active = nowActive;
            repaint();
        }

        bool isActive() const noexcept { return active; }

        /** The fill an active button wears. Bypass wants to shout in red where
            everything else wears the accent. */
        /** Whether this draws a background of its own.

            Off for a button that sits inside something already drawn as its frame --
            Céline's undo and redo share one housing painted behind the pair. Such a
            button shows only its hover, inset so it stays within that housing. */
        /** Which role fills the button when it is framed. Button unless told otherwise
            -- and told otherwise where a button stands on a surface of its own, so that
            re-slating one group of controls does not reach every button in the window.

            A role rather than a colour, for the reason every override in this house is
            one: paintButton runs again on every theme change and reads this afresh, so
            a colour handed in from outside would be painted over the first time the
            palette moved. */
        void setFillRole (Theme::Role role)
        {
            fillRole = role;
            repaint();
        }

        void setDrawsFrame (bool shouldDraw)
        {
            drawsFrame = shouldDraw;
            repaint();
        }

        void setActiveColour (juce::Colour colour)
        {
            activeColour = colour;
            repaint();
        }

        /** Overrides the ink the glyph is drawn in.

            The toolbar's icons are monochrome and want to stay that way -- they are
            chrome, and chrome that competes with the controls is chrome in the way.
            The library's two are the opposite case: they sit in a panel of their own
            carrying the accent, and drawn in the toolbar's grey they read as disabled. */
        void setIconColour (juce::Colour colour)
        {
            iconColour = colour;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);
            const bool usable = isEnabled();

            // The same lift every other button in the window answers a pointer with --
            // Theme::underPointer, not a colour of its own. These used to swap to a
            // themeable "hovered" colour instead, which meant the icon buttons were the
            // one control whose hover had to be kept in step with everything else's by
            // hand.
            if (active || drawsFrame)
            {
                const auto base = active ? activeColour.value_or (Theme::accent())
                                         : Theme::colour (fillRole);

                g.setColour (Theme::underPointer (base, highlighted, down));
                g.fillRoundedRectangle (bounds, Theme::cornerRadius);
            }
            else if (down || highlighted)
            {
                // Frameless, so there is no fill to lift -- the lift itself is the whole
                // of what is drawn, and it stays inside whatever is behind it as its
                // frame.
                g.setColour (Theme::text().withAlpha (down ? 0.16f : 0.08f));
                g.fillRoundedRectangle (bounds.reduced (1.0f), Theme::cornerRadius * 0.6f);
            }

            // Fill only, no rule. Every button here is filled with something that
            // already separates it from what it stands on, so an outline drew a second
            // edge where one was doing the job.
            if (icon == nullptr)
                return;

            auto drawn = icon->createCopy();

            // The mark's own colour, unless the button was given one. An icon in the
            // toolbar is the same kind of thing as the logo beside it -- a white shape
            // on the chrome -- and giving the two separate pickers meant setting one
            // colour twice to keep them looking like they belonged together.
            const auto idle = iconColour.value_or (Theme::headerText());

            Assets::tint (*drawn, ! usable                        ? Theme::textDisabled()
                                  : active || highlighted || down ? idle.brighter (0.3f)
                                                                  : idle);
            drawn->drawWithin (g, bounds.reduced (bounds.getWidth() * iconInset),
                               juce::RectanglePlacement::centred, 1.0f);
        }

        /** How much air to leave around the glyph, as a fraction of the button's
            width. The default is measured off the design's toolbar, which fills about
            twenty of a 33px button with glyph.

            Worth setting where a button stands among things that are not buttons. The
            glyph is scaled to fill whatever this leaves, so a shape that reaches the
            corners of its own box -- an X does, a gear does not -- comes out heavier
            than a letter of nominally the same size sitting beside it. */
        void setIconInset (float fractionOfWidth)
        {
            iconInset = fractionOfWidth;
            repaint();
        }

    protected:
        float iconInset = 0.18f;

    private:
        std::unique_ptr<juce::Drawable> icon;
        std::optional<juce::Colour> activeColour;
        std::optional<juce::Colour> iconColour;
        bool active = false;
        Theme::Role fillRole = Theme::Role::button;
        bool drawsFrame = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
    };
} // namespace Celine
