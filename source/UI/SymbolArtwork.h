#pragma once

#include "../Schematic/Element.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace SchematicUI
{
    //==========================================================================
    /**
        The drawn shape of every part, loaded from SVG rather than written in
        code.

        `assets/icons/elements`, embedded at build time and replaceable by
        dropping a different file in with the same name -- so redrawing a
        resistor is not a C++ edit.

        **The convention artwork must follow**, because the canvas draws pins on
        top of it at the positions in `Element.h` and they have to land on the
        terminals:

        - viewBox is `-60 -60 120 120`, and **(0,0) is the part's centre** --
          the same origin `Element::getPinPosition` measures from.
        - **10 SVG units = 1 grid square.** A pin at grid (0,-2) is at (0,-20).
        - stroke-width `1.2` is the schematic's normal line weight. Heavier
          strokes are multiples of it: 1.56 is the 1.3x used for capacitor
          plates and valve electrodes, 1.8 the 1.5x used for a transistor's base
          bar.
        - colour is ignored. Everything is recoloured by the canvas, so a
          selected part can go orange and a ghost can go translucent.

        Geometry is extracted once and kept in grid space, which lets the canvas
        stroke at `max(1, gridSize * 0.12)` pixels rather than letting the
        transform scale strokes down with everything else -- without that floor a
        sheet zoomed out to see all of it fades to nothing. It also means
        painting allocates nothing: the paths are cached and only an
        AffineTransform changes per element.
    */
    struct SymbolArtwork
    {
        /** One path from the file, and how it is meant to be painted. */
        struct Shape
        {
            juce::Path path;

            /** Filled shapes are the solid ones -- a diode's triangle, an
                arrowhead, a switch contact. Everything else is stroked. */
            bool filled = false;

            /** Authored stroke width over the 1.2 baseline, so 1.3 comes back
                out of a stroke-width of 1.56. Multiplied into the canvas's own
                line weight rather than scaled by the view transform. */
            float strokeScale = 1.0f;
        };

        std::vector<Shape> shapes;

        /** What the shapes actually cover, in grid units, including the width of
            the strokes. Measured when the file is parsed, because that is the
            only moment it can go stale: redraw an SVG and this follows, which a
            box written out by hand in the element table would not. */
        juce::Rectangle<float> bounds;

        /** True when the file was missing or empty -- which is the one thing
            that must not be silent, since a part with no artwork is invisible
            and looks like a bug in the drawing rather than a missing asset. */
        bool isEmpty() const noexcept { return shapes.empty(); }
    };

    //==========================================================================
    /** The artwork for a part *as it currently is* -- which variant of a switch,
        which style of diode, which way round a transistor faces.

        Returns nullptr for the two types that have no artwork: Text is its own
        text, and Rectangle is a frame whose size the user drags. */
    const SymbolArtwork* getSymbolArtwork(const SchematicModel::Element& element);

    /** The asset name a part currently draws with, without the extension. Public
        so a test can check every part resolves to a file that exists. */
    juce::String getSymbolAssetName(const SchematicModel::Element& element);

    /** Every asset name the drawing can ask for. The test that walks these is
        what stops a renamed file from silently blanking a part. */
    juce::StringArray getAllSymbolAssetNames();
} // namespace SchematicUI
