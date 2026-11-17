#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <CelineUI/Theme.h>

#include "GlyphButton.h"

namespace Celine
{
    /** The sheet's zoom, in a small card under the palette and as wide as it.

        Open, a zoom-out glyph, the house slider (the same pill every plugin of the house
        draws) and a zoom-in glyph; the glyphs are bare until the pointer is on them, so the
        card stays quiet next to the palette above it. Folded with the palette there is room
        for the two glyphs alone, which sit under its two columns. It follows the palette's
        width as the palette folds, the slider fading as it goes, so the two move as one
        panel. The slider is the zoom on a log scale (each step of it the same proportion
        closer); the glyphs step a quarter closer or farther. Both zoom about the middle of
        the sheet left uncovered. The wheel is not on it, as on every control in this window:
        the wheel zooms the sheet itself. */
    class ZoomStrip : public juce::Component
    {
    public:
        ZoomStrip();

        /** Moves the slider to the sheet's zoom (0..1 along the log scale) without telling
            anyone: the sheet changed it. */
        void showZoom (float proportion);

        /** The slider was moved, to 0..1 along the log scale. */
        std::function<void (float proportion)> onZoomChosen;

        /** A glyph was clicked: true closer, false farther. */
        std::function<void (bool closer)> onStep;

        /** A palette entry's height (Theme::paletteRowHeight less its gap) and a little
            either side: the glyphs are as big as the palette's pills. */
        static constexpr int height = Theme::paletteRowHeight - 4 + 2 * 5;

        /** Below this width the slider gives way to the glyphs alone. */
        static constexpr int sliderWidth = 150;

        void paint (juce::Graphics&) override;
        void resized() override;
        void lookAndFeelChanged() override;

    private:
        GlyphButton outButton { "Zoom out", false }, inButton { "Zoom in", true };
        juce::Slider slider;
    };
} // namespace Celine
