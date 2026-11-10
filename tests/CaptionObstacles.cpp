#include <ui/CaptionObstacles.h>

#include <catch2/catch_test_macros.hpp>

#include <random>

TEST_CASE ("Caption clutter from the cells matches a full scan", "[ui][captions]")
{
    std::mt19937 rng { 0xCAB5u };
    auto uniform = [&rng] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (rng); };
    auto grid = [&rng] (int lo, int hi) { return (float) std::uniform_int_distribution<int> (lo, hi) (rng); };

    for (int sheet = 0; sheet < 20; ++sheet)
    {
        struct Part { int id; juce::Rectangle<float> bounds; };
        std::vector<Part> parts;
        std::vector<juce::Line<float>> wires;
        std::vector<juce::Rectangle<float>> captions;

        for (int i = 0; i < 60; ++i)
            parts.push_back ({ i + 1, { grid (-40, 120), grid (-40, 80), grid (1, 8), grid (1, 8) } });

        // Wires run along grid lines, so half are zero-width and half zero-height.
        for (int i = 0; i < 60; ++i)
        {
            const juce::Point<float> a (grid (-40, 120), grid (-40, 80));
            const auto length = grid (-20, 20);
            wires.push_back (i % 2 == 0 ? juce::Line<float> (a, a.translated (length, 0.0f))
                                        : juce::Line<float> (a, a.translated (0.0f, length)));
        }

        // The extent deliberately misses some items: those go into the edge cells.
        Celine::CaptionObstacles cells;
        cells.reset ({ 0.0f, 0.0f, 60.0f, 40.0f });

        for (const auto& part : parts)
            cells.addPart (part.id, part.bounds);
        for (const auto& wire : wires)
            cells.addWire (wire);

        for (int query = 0; query < 400; ++query)
        {
            const juce::Rectangle<float> box (uniform (-60.0f, 140.0f), uniform (-60.0f, 100.0f),
                                              uniform (0.5f, 12.0f), uniform (0.5f, 4.0f));
            const int self = (int) grid (0, 60);

            float expected = 0.0f;
            for (const auto& placed : captions)
                if (placed.intersects (box))
                    expected += 10.0f;
            for (const auto& part : parts)
                if (part.id != self && part.bounds.intersects (box))
                    expected += 10.0f;
            for (const auto& wire : wires)
                if (box.intersects (wire))
                    expected += 1.0f;

            INFO ("sheet " << sheet << ", query " << query);
            REQUIRE (juce::exactlyEqual (cells.clutterAt (self, box), expected));

            // Captions are placed as the paint goes, and count from then on.
            if (query % 4 == 0)
            {
                captions.push_back (box);
                cells.addCaption (box);
            }
        }
    }
}
