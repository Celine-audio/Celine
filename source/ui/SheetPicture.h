#pragma once

#include "../Schematic/Schematic.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A sheet as a picture: what the canvas draws (the grid, the boxes, wires and parts, their
    captions), framed whole, with no rulers, selection or anything in flight. Made for the
    Quick Look preview (source/quicklook), which shows a .celsch when Space is pressed on it
    in the Finder, so it is the drawing Céline shows and not a second one to keep in step.

    Builds no circuit and reads nothing but the drawing. Message thread: it draws through a
    SchematicCanvas, a Component. */
namespace Celine::SheetPicture
{
    /** The drawing in a .celsch document, or false for anything that is not one. */
    bool readDrawing (const juce::File& file, SchematicModel::Schematic& into);

    /** The size in points to show a sheet at: the drawing's own shape, `longest` points
        along its long side, the other at least half that so a single row of parts is not a
        sliver. */
    juce::Point<int> sizeFor (const SchematicModel::Schematic& sheet, int longest = 1200);

    /** The sheet drawn `width` x `height` points at `scale` pixels to a point. */
    juce::Image draw (SchematicModel::Schematic& sheet, int width, int height, float scale);

    /** A .celsch as a PNG at twice its size in points (`width` x `height`), for a Retina
        screen. False when the file is not a sheet. */
    bool previewPng (const juce::File& file, juce::MemoryBlock& png, int& width, int& height);
} // namespace Celine::SheetPicture
