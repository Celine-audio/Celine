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

    /** Hangs a probe on `positive`, with its reference pin on a ground of its
        own, and returns the scope's id.

        Attached by **coincident pins, not by wires**. Pins landing on the same
        square connect with no wire at all, which is the whole point of the rule
        -- and a wire drawn to an arbitrary point has to get there, which means
        it can pass through something else on the way. An earlier version of this
        helper wired both pins and the two L-shaped runs touched, shorting the
        output to ground: the test then reported that a scope changes the circuit
        by 0.3 V, which was true of the drawing and nothing to do with the part.
    */
    int addScopeAt (Schematic& sheet, juce::Point<int> positive)
    {
        const int id = sheet.addElement (ElementType::Scope, 0, 0);
        auto* scope = sheet.findElement (id);

        // Stood upright, which is not cosmetic. Lying flat its two pins are four
        // grid squares apart *horizontally* -- and the input wire in these
        // example sheets runs from (2,0) to (6,0), which is exactly four squares
        // horizontally. So a flat scope hung on the input landed pin 0 on one end
        // of that wire and pin 1 on the other, put its own reference ground on
        // the far end, and shorted the input net to ground. Upright, the pins are
        // four apart vertically and the reference lands clear of the run.
        scope->orientation = 1;

        // Placed at the origin first, so its own pin positions *are* its
        // offsets -- no table of pin geometry restated here to go stale.
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

        // The helper's whole contract is that a probe watches without changing
        // anything. If its reference pin lands on the net it is measuring, it
        // does not measure that net -- it shorts it, and drags whatever it was
        // watching down to its own ground.
        //
        // Checked here rather than left to whichever test happens to notice,
        // because the failure is quiet: the scope still gets a trace, the trace
        // is just flat, and a test asserting two scopes have different traces
        // passes anyway. That is exactly how a shorted input survived here until
        // the builder learned to reject one.
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

    // Across the circuit's own output and its ground, which is the reading you
    // would actually take.
    const int scope = addScopeAt (sheet, pinOf (sheet, ElementType::Output));

    plugin.prepareToPlay (48000.0, 512);
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    const auto* trace = plugin.getScopeTrace (scope);
    REQUIRE (trace != nullptr);
    REQUIRE (trace->live.load());

    // Quiet first: the output of a clipper with no input sits at zero.
    pump (plugin, 20, 0.0f);
    CHECK (trace->peakToPeak.load() == Catch::Approx (0.0f).margin (1.0e-4));

    // Then driven. What is under test is that the probe follows the node at all
    // -- a probe reading a hardcoded zero would pass the line above and fail
    // this one.
    pump (plugin, 80, 0.8f);

    INFO ("peak-to-peak " << trace->peakToPeak.load()
                          << "  dc " << trace->dcAverage.load());
    CHECK (trace->peakToPeak.load() > 0.05f);
}

TEST_CASE ("A scope changes nothing about the circuit it watches", "[scope]")
{
    // The whole contract of the part: it stamps nothing, so hanging one on a
    // node must not change what that node does. A probe that loaded what it
    // measured would be a probe that lied, and it would lie worst exactly where
    // you care most -- a high-impedance grid node, where a megohm matters.
    //
    // Bit-for-bit, which the probe earns by attaching on coincident pins: no new
    // wires, no new nets, so the netlist the builder produces is the same one in
    // the same order. Attach it with wires instead and the answers separate in
    // the last few bits -- not because the probe loads anything, but because the
    // extra net reorders the matrix and partial pivoting then picks differently.
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

    // And an id that isn't a scope has no trace rather than someone else's.
    CHECK (plugin.getScopeTrace (99999) == nullptr);
}
