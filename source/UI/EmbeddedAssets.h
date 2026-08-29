#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace SchematicUI::Assets
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

    /** The bytes for an embedded file, or null. */
    const char* find(const juce::String& filename, int& sizeInBytes);

    /** An embedded SVG or image, parsed, or null if it isn't there. */
    std::unique_ptr<juce::Drawable> drawable(const juce::String& filename);

    /** Forces every painted path in a drawable to one colour.

        Honours what the artwork actually paints: an outline-only icon says
        `fill="none"`, and filling it anyway turns every glyph into a solid blob.
        `Drawable::replaceColour` is not a substitute -- it only swaps an exact
        match, so it works on pure black artwork and silently does nothing on
        anything else. */
    void tint(juce::Drawable& drawable, juce::Colour colour);
} // namespace SchematicUI::Assets
