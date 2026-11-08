#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;

namespace
{
    /** The first pin of the first element of a type, in grid coordinates. */
    juce::Point<int> pinOf (const Schematic& sheet, ElementType type, int pin = 0)
    {
        for (const auto& element : sheet.getElements())
            if (element.type == type)
                return element.getPinPosition (pin);

        return { 0, 0 };
    }

    /** Hangs a probe on `positive`, its reference pin on a ground of its own, and returns
        the scope's id. Attached by coincident pins, not wires, so no wire can cross
        anything on its way. */
    int addScopeAt (Schematic& sheet, juce::Point<int> positive)
    {
        const int id = sheet.addElement (ElementType::Scope, 0, 0);
        auto* scope = sheet.findElement (id);

        // Upright: lying flat, its pins are four squares apart horizontally, exactly the
        // length of the example sheets' input wire, which would short the input to ground.
        scope->orientation = 1;

        // Placed at the origin first, so its pin positions are its offsets.
        const auto offset = scope->getPinPosition (0);
        scope->x = positive.x - offset.x;
        scope->y = positive.y - offset.y;

        const auto scopeIndex = sheet.getElements().size() - 1;
        const auto reference = scope->getPinPosition (1);

        const int groundId = sheet.addElement (ElementType::Ground, 0, 0);
        auto* ground = sheet.findElement (groundId);
        const auto groundOffset = ground->getPinPosition (0);
        ground->x = reference.x - groundOffset.x;
        ground->y = reference.y - groundOffset.y;

        // The reference pin must not land on the measured net: that would short it, and the
        // trace would just be flat.
        const auto nets = sheet.extractNets();
        REQUIRE (nets.netOfPin[scopeIndex][0] != nets.netOfPin[scopeIndex][1]);

        return id;
    }

    void pump (PluginProcessor& plugin, int blocks = 80, float level = 0.0f)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        int n = 0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 512; ++s)
                    buffer.setSample (c, s, level * std::sin (2.0f * 3.14159265f * 110.0f
                                                              * (float) (n + s) / 48000.0f));

            n += 512;
            plugin.processBlock (buffer, midi);
        }
    }
}

TEST_CASE ("A scope reads the node it is hung on", "[scope]")
{
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    // Across the circuit's output and ground.
    const int scope = addScopeAt (sheet, pinOf (sheet, ElementType::Output));

    plugin.prepareToPlay (48000.0, 512);
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    const auto* trace = plugin.getScopeTrace (scope);
    REQUIRE (trace != nullptr);
    REQUIRE (trace->live.load());

    // Quiet first: no input, zero output.
    pump (plugin, 20, 0.0f);
    CHECK (trace->peakToPeak.load() == Catch::Approx (0.0f).margin (1.0e-4));

    // Then driven: the probe must follow the node.
    pump (plugin, 80, 0.8f);

    INFO ("peak-to-peak " << trace->peakToPeak.load()
                          << "  dc " << trace->dcAverage.load());
    CHECK (trace->peakToPeak.load() > 0.05f);
}

TEST_CASE ("A scope changes nothing about the circuit it watches", "[scope]")
{
    // A scope stamps nothing, so adding one must not change the output, bit for bit.
    // That holds because it attaches by coincident pins: wires would add a net, reorder
    // the matrix and change the last few bits.
    auto render = [] (bool withScope)
    {
        PluginProcessor plugin;
        auto& sheet = plugin.getSchematic();
        Examples::load (sheet, 0);

        if (withScope)
            addScopeAt (sheet, pinOf (sheet, ElementType::Output));

        plugin.prepareToPlay (48000.0, 512);
        REQUIRE (plugin.rebuild().isValid());

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        juce::Random random (99);

        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 256; ++s)
                buffer.setSample (c, s, random.nextFloat() * 2.0f - 1.0f);

        plugin.processBlock (buffer, midi);

        std::vector<float> out ((size_t) 256);

        for (int s = 0; s < 256; ++s)
            out[(size_t) s] = buffer.getSample (0, s);

        return out;
    };

    const auto without = render (false);
    const auto with = render (true);

    REQUIRE (without.size() == with.size());

    for (size_t i = 0; i < without.size(); ++i)
        REQUIRE (juce::exactlyEqual (without[i], with[i]));
}

TEST_CASE ("Every scope on the sheet gets its own trace", "[scope]")
{
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const int a = addScopeAt (sheet, pinOf (sheet, ElementType::Output));
    const int b = addScopeAt (sheet, pinOf (sheet, ElementType::Input));

    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.rebuild().isValid());

    const auto* traceA = plugin.getScopeTrace (a);
    const auto* traceB = plugin.getScopeTrace (b);

    REQUIRE (traceA != nullptr);
    REQUIRE (traceB != nullptr);
    CHECK (traceA != traceB);

    // An id that isn't a scope has no trace.
    CHECK (plugin.getScopeTrace (99999) == nullptr);
}
