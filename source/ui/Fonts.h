#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        Jura, the typeface the design is drawn in, loaded out of the binary.

        Bundled rather than asked for by name. A plugin cannot assume anything
        about the machine it lands on, and `juce::Font("Jura", ...)` on a machine
        without it silently falls back to the platform sans -- which looks like a
        layout bug rather than a missing font, because every label is suddenly a
        different width.

        Two weights are used: Light for labels and readouts, Bold for the one
        button that shouts, plus JetBrains Mono for anything that must align. The other weights in `assets/Jura/` are embedded by
        the assets glob but unused; delete the files if the binary size matters
        more than having them to hand.

        If a weight is missing from the binary the accessor returns null and JUCE
        falls back on its own. That is deliberately not an assertion: a missing
        font should not stop the plugin from opening.
    */
    namespace Fonts
    {
        /** The weights the design uses, by the filename that carries them.

            Mono is JetBrains Mono, a separate family rather than a weight of
            Jura, used only where text has to line up in columns. */
        enum class Weight
        {
            Light,
            Bold,
            Mono,

            /** Nico Moji, and only for the wordmark beside the logo. A display face
                with a look of its own, which is the point — and the reason it is not
                on this list for anything else to reach for. */
            Logo,
        };

        /** The typeface, parsed once. Null if the file is not embedded. */
        juce::Typeface::Ptr typeface(Weight weight);

        /** A font of the given weight and height in pixels.

            Height rather than "points": every size in the design is a pixel
            measurement taken off the mockup, and JUCE's `withHeight` is the one
            that means the same thing. */
        juce::Font font(Weight weight, float heightInPixels);

        /** Shorthand for the ones the design actually uses. */
        inline juce::Font light(float height) { return font(Weight::Light, height); }
        inline juce::Font bold(float height) { return font(Weight::Bold, height); }
        inline juce::Font mono(float height) { return font(Weight::Mono, height); }

        /** The wordmark's face. Falls back to Jura Bold rather than to the platform
            sans if Nico Moji is not embedded: a missing display face should leave the
            name looking like the rest of the design, not like a different program. */
        juce::Font logo(float height);
    } // namespace Fonts
} // namespace Celine
