#pragma once

#include "../Schematic/Element.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace Celine
{
    //==========================================================================
    /** A part's symbol, loaded from an SVG in `assets/icons/elements`.

        Artwork conventions (pins are drawn at the positions in `Element.h`):
        - viewBox `-60 -60 120 120`, with (0,0) at the part's centre;
        - 10 SVG units per grid square;
        - stroke-width 1.2 is the normal line weight; other widths are multiples of it;
        - colours are ignored, the canvas tints everything.

        Geometry is parsed once and cached in grid units. */
    struct SymbolArtwork
    {
        /** One path from the file. */
        struct Shape
        {
            juce::Path path;

            /** Filled shapes are painted solid; the rest are stroked. */
            bool filled = false;

            /** Authored stroke width divided by 1.2. */
            float strokeScale = 1.0f;
        };

        std::vector<Shape> shapes;

        /** What the shapes cover, strokes included, in grid units. */
        juce::Rectangle<float> bounds;

        /** The file's viewBox, in grid units, centred on the part. */
        juce::Rectangle<float> viewBox;

        /** The artwork as painted: strokes outlined at their authored width, merged with the
            fills into one filled path per opacity, in grid units about the part's centre. */
        struct InkRun
        {
            juce::Path path;
            float alpha = 1.0f;
        };

        std::vector<InkRun> ink;

        /** True when the file was missing or empty. */
        bool isEmpty() const noexcept { return shapes.empty(); }
    };

    //==========================================================================
    /** The artwork for the part's current state (switch position, diode style,
        transistor polarity). Nullptr for parts drawn in code: Text, Rectangle and Node. */
    const SymbolArtwork* getSymbolArtwork(const SchematicModel::Element& element);

    /** The asset name the part draws with, without extension. */
    juce::String getSymbolAssetName(const SchematicModel::Element& element);

    /** Every asset name the drawing can use. */
    juce::StringArray getAllSymbolAssetNames();
} // namespace Celine
