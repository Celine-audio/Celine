#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine::Assets
{
    //==========================================================================
    /**
        Everything embedded in the binary, found by the filename it was embedded
        under.

        The one rule: **ask by filename, never by the C++ identifier JUCE derives
        from it.** That derivation is not the obvious one -- it *strips*
        characters rather than replacing them, so `arrow-pointer-solid-full.svg`
        becomes `arrowpointersolidfull_svg` and `capacitor-polarised.svg` becomes
        `capacitorpolarised_svg`. Getting it wrong is silent: the lookup returns
        null and whatever wanted the artwork draws nothing at all, which reads as
        a rendering bug rather than a typo.

        That has now cost two separate afternoons -- once for the element
        symbols, once for the toolbar icons -- which is why the loop lives here
        instead of being copied into every file that needs an asset.
    */

    /** Whether a missing file is a bug or a choice.

        Nearly always a bug -- artwork the interface draws is either embedded or the
        interface has a hole in it -- so `required` asserts in a debug build rather
        than leaving a blank button to be found by eye later. `optional` is for the
        handful of assets a project may legitimately not have yet, such as a wordmark
        that has not been drawn: those have a fallback and must not fire an assertion
        on every launch. */
    enum class IfMissing { assertInDebug, returnNull };

    /** The bytes for an embedded file, or null. */
    const char* find(const juce::String& filename, int& sizeInBytes,
                     IfMissing = IfMissing::assertInDebug);

    /** An embedded SVG or image, parsed, or null if it isn't there. */
    std::unique_ptr<juce::Drawable> drawable(const juce::String& filename,
                                             IfMissing = IfMissing::assertInDebug);

    /** Forces every painted path in a drawable to one colour.

        Honours what the artwork actually paints: an outline-only icon says
        `fill="none"`, and filling it anyway turns every glyph into a solid blob.
        `Drawable::replaceColour` is not a substitute -- it only swaps an exact
        match, so it works on pure black artwork and silently does nothing on
        anything else. */
    void tint(juce::Drawable& drawable, juce::Colour colour);

    /** Draws a wordmark centred on its letters rather than on its bounding box.

        A lowercase wordmark almost always has a descender -- SPACE's `p` drops a
        seventh of the artwork's height below the baseline -- and every other glyph
        sits on that baseline. Centring the bounding box therefore centres the
        descender too, and hangs the word visibly above where the eye puts it. This
        centres the band the letters actually occupy: the top of the tallest glyph to
        the baseline the majority of them stand on.

        Measured from the artwork rather than written down as a constant, so a
        redrawn logo with a different descender stays centred without anybody
        remembering that this is why. */
    void drawWordmark(juce::Graphics&, juce::Drawable&, juce::Rectangle<float> area);
} // namespace Celine::Assets
