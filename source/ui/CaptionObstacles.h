#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        What a caption being placed would cover, for SchematicCanvas: parts and
        captions already placed count 10 each, wires 1 each. All in grid units.

        Everything is sorted into coarse square cells once per paint, so a query
        only tests what is near it rather than the whole sheet; on a 400-part
        sheet the full scan was over half of every repaint. The score is exactly
        the full scan's: each item is counted at most once per query, and items
        outside the cells' extent go into the edge cells, so nothing is missed.

        Message thread only. Storage is reused from paint to paint.
    */
    class CaptionObstacles
    {
    public:
        /** Starts a new paint covering `extent`. Items may lie outside it. */
        void reset(juce::Rectangle<float> extent)
        {
            origin = extent.getPosition();
            columns = juce::jlimit(1, maxCells, static_cast<int>(std::ceil(extent.getWidth() / cellSize)) + 1);
            rows = juce::jlimit(1, maxCells, static_cast<int>(std::ceil(extent.getHeight() / cellSize)) + 1);

            cells.resize(static_cast<size_t>(columns * rows));
            for (auto& cell : cells)
                cell.clear();

            parts.clear();
            captions.clear();
            wires.clear();
            seen.clear();
            stamp = 0;
        }

        void addPart(int elementId, juce::Rectangle<float> bounds)
        {
            parts.push_back({elementId, bounds});
            insert(Kind::part, static_cast<int>(parts.size()) - 1, bounds);
        }

        void addWire(juce::Line<float> wire)
        {
            wires.push_back(wire);
            insert(Kind::wire, static_cast<int>(wires.size()) - 1,
                   juce::Rectangle<float>(wire.getStart(), wire.getEnd()));
        }

        void addCaption(juce::Rectangle<float> box)
        {
            captions.push_back(box);
            insert(Kind::caption, static_cast<int>(captions.size()) - 1, box);
        }

        /** The clutter a caption at `box` would add, its own part (`elementId`) not
            counted. */
        float clutterAt(int elementId, juce::Rectangle<float> box)
        {
            ++stamp;
            float clutter = 0.0f;

            const auto range = cellRange(box);

            for (int row = range.getY(); row <= range.getBottom(); ++row)
            {
                for (int column = range.getX(); column <= range.getRight(); ++column)
                {
                    for (const auto& entry : cells[static_cast<size_t>(row * columns + column)])
                    {
                        auto& last = seen[static_cast<size_t>(entry.item)];
                        if (last == stamp)
                            continue;
                        last = stamp;

                        switch (entry.kind)
                        {
                            case Kind::part:
                            {
                                const auto& part = parts[static_cast<size_t>(entry.index)];
                                if (part.elementId != elementId && part.bounds.intersects(box))
                                    clutter += 10.0f;
                                break;
                            }
                            case Kind::caption:
                                if (captions[static_cast<size_t>(entry.index)].intersects(box))
                                    clutter += 10.0f;
                                break;
                            case Kind::wire:
                                if (box.intersects(wires[static_cast<size_t>(entry.index)]))
                                    clutter += 1.0f;
                                break;
                        }
                    }
                }
            }

            return clutter;
        }

    private:
        enum class Kind : unsigned char { part, caption, wire };

        struct Entry
        {
            Kind kind;
            int index; // into parts, captions or wires
            int item;  // into seen
        };

        struct Part
        {
            int elementId;
            juce::Rectangle<float> bounds;
        };

        /** Grid squares per cell side. A few parts across, so a query touches a
            handful of cells. */
        static constexpr float cellSize = 8.0f;

        /** Cells per side at most, so a sheet with one part far away cannot make
            the grid huge; anything past it shares the edge cells. */
        static constexpr int maxCells = 256;

        void insert(Kind kind, int index, juce::Rectangle<float> bounds)
        {
            const int item = static_cast<int>(seen.size());
            seen.push_back(0);

            const auto range = cellRange(bounds);

            for (int row = range.getY(); row <= range.getBottom(); ++row)
                for (int column = range.getX(); column <= range.getRight(); ++column)
                    cells[static_cast<size_t>(row * columns + column)].push_back({kind, index, item});
        }

        /** The cells a rectangle touches, inclusive, clamped to the grid. Clamping
            both sides the same way keeps any two intersecting rectangles sharing a
            cell. */
        juce::Rectangle<int> cellRange(juce::Rectangle<float> bounds) const noexcept
        {
            const auto cellOf = [](float v, float start, int count)
            { return juce::jlimit(0, count - 1, static_cast<int>(std::floor((v - start) / cellSize))); };

            const int x0 = cellOf(bounds.getX(), origin.x, columns);
            const int x1 = cellOf(bounds.getRight(), origin.x, columns);
            const int y0 = cellOf(bounds.getY(), origin.y, rows);
            const int y1 = cellOf(bounds.getBottom(), origin.y, rows);
            return {x0, y0, x1 - x0, y1 - y0};
        }

        juce::Point<float> origin;
        int columns = 1, rows = 1;
        std::vector<std::vector<Entry>> cells;
        std::vector<Part> parts;
        std::vector<juce::Rectangle<float>> captions;
        std::vector<juce::Line<float>> wires;
        std::vector<unsigned> seen;
        unsigned stamp = 0;
    };
} // namespace Celine
