#include "SheetPicture.h"

#include "SchematicCanvas.h"
#include "../PluginProcessor.h"

namespace Celine::SheetPicture
{
    bool readDrawing (const juce::File& file, SchematicModel::Schematic& into)
    {
        // The document's own type and its drawing, as restoreDocument() reads them.
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return false;

        const auto document = juce::ValueTree::fromXml (*xml);
        if (! document.hasType (PluginProcessor::documentType))
            return false;

        const auto drawing = document.getChildWithName ("SCHEMATIC");
        if (! drawing.isValid())
            return false;

        into.restoreFromValueTree (drawing);
        into.takeLoadWarnings(); // a preview says nothing about missing models
        return true;
    }

    juce::Point<int> sizeFor (const SchematicModel::Schematic& sheet, int longest)
    {
        // The drawing's extent in grid squares, as the canvas frames it.
        juce::Rectangle<int> bounds;
        bool first = true;

        const auto include = [&] (juce::Rectangle<int> r)
        {
            bounds = first ? r : bounds.getUnion (r);
            first = false;
        };

        for (const auto& element : sheet.getElements())
            include (sheet.getElementBounds (element));

        for (const auto& wire : sheet.getWires())
            include (juce::Rectangle<int> (wire.a, wire.b).expanded (1));

        if (first)
            return { longest, longest * 2 / 3 };

        const double aspect = (double) juce::jmax (1, bounds.getHeight()) / (double) juce::jmax (1, bounds.getWidth());

        if (aspect <= 1.0)
            return { longest, juce::jlimit (longest / 2, longest, juce::roundToInt (longest * aspect)) };

        return { juce::jlimit (longest / 2, longest, juce::roundToInt (longest / aspect)), longest };
    }

    juce::Image draw (SchematicModel::Schematic& sheet, int width, int height, float scale)
    {
        SchematicCanvas canvas (sheet);
        canvas.setSize (width, height);
        canvas.refresh();
        canvas.zoomToFit();

        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale), juce::roundToInt ((float) height * scale), true);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));

            // The canvas's own paint: the rulers are its children, so they are left out.
            canvas.paint (g);
        }

        return image;
    }

    bool previewPng (const juce::File& file, juce::MemoryBlock& png, int& width, int& height)
    {
        SchematicModel::Schematic sheet;
        if (! readDrawing (file, sheet))
            return false;

        const auto size = sizeFor (sheet);
        width = size.x;
        height = size.y;

        const auto image = draw (sheet, width, height, 2.0f);

        juce::MemoryOutputStream out (png, false);
        return juce::PNGImageFormat().writeImageToStream (image, out);
    }
} // namespace Celine::SheetPicture
