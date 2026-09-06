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

            if (active)
            {
                g.setColour (activeColour.value_or (Theme::toolActive()));
                g.fillRoundedRectangle (bounds, Theme::cornerRadius);
            }
            else if (drawsFrame)
            {
                g.setColour (down || highlighted ? Theme::surfaceBright() : Theme::button());
                g.fillRoundedRectangle (bounds, Theme::cornerRadius);
            }
            else if (down || highlighted)
            {
                // Frameless, so only the hover shows -- and it stays inside whatever is
                // drawn behind it as its frame.
                g.setColour (Theme::surfaceBright());
                g.fillRoundedRectangle (bounds.reduced (1.0f), Theme::cornerRadius * 0.6f);
            }

            // Fill only, no rule. Every button here is filled with something that
            // already separates it from what it stands on, so an outline drew a second
            // edge where one was doing the job.
            if (icon == nullptr)
                return;

            auto drawn = icon->createCopy();

            const auto idle = iconColour.has_value() ? *iconColour : Theme::textDim();
            const auto lit = iconColour.has_value() ? iconColour->brighter (0.3f) : Theme::text();

            Assets::tint (*drawn, ! usable                        ? Theme::textDisabled()
                                  : active || highlighted || down ? lit
                                                                  : idle);
            drawn->drawWithin (g, bounds.reduced (bounds.getWidth() * iconInset),
                               juce::RectanglePlacement::centred, 1.0f);
        }

    protected:
        /** How far in from the button edge the glyph is drawn, as a fraction of
            the width. Measured off the design, which fills about twenty of a 33px
            button with glyph. */
        static constexpr float iconInset = 0.18f;

    private:
        std::unique_ptr<juce::Drawable> icon;
        std::optional<juce::Colour> activeColour;
        std::optional<juce::Colour> iconColour;
        bool active = false;
        bool drawsFrame = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
    };
} // namespace Celine
