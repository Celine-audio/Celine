#pragma once

#include <CelineUI/Theme.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    /** A bare minus or plus on the light panels, for stepping something down or up: the
        zoom card's two ends, the trace window's time span. Nothing behind it until the
        pointer is on it, then the palette's pill, so a row of them stays quiet. One class,
        so every stepper in the window looks and answers the same. */
    class GlyphButton : public juce::Button
    {
    public:
        GlyphButton (const juce::String& name, bool isPlus, float glyphSize = 11.0f)
            : juce::Button (name), plus (isPlus), size (glyphSize)
        {
            setTooltip (name);
            setWantsKeyboardFocus (false);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat();
            const bool live = isEnabled();

            if (live && (highlighted || down))
            {
                g.setColour (down ? Theme::pill().darker (0.08f) : Theme::pill());
                g.fillRoundedRectangle (bounds, Theme::paletteEntryCornerRadius + 2.0f);
            }

            // At the end of its range a stepper's glyph fades: there is nowhere to go.
            const auto c = bounds.getCentre();
            const float weight = 1.6f;
            g.setColour (Theme::textOnPanel().withAlpha (! live ? 0.25f : highlighted || down ? 0.95f : 0.7f));
            g.fillRoundedRectangle (juce::Rectangle<float> (size, weight).withCentre (c), weight * 0.5f);
            if (plus)
                g.fillRoundedRectangle (juce::Rectangle<float> (weight, size).withCentre (c), weight * 0.5f);
        }

    private:
        const bool plus;
        const float size;
    };
} // namespace Celine
