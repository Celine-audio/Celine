#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <ui/SchematicSymbols.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using Celine::SymbolPainter;

namespace
{
    /** Hangs a scope across a two-pin part by coincident pins (no wires; see
        ScopeProbe.cpp). */
    int addScopeAcross (Schematic& sheet, const Element& part)
    {
        const auto positive = part.getPinPosition (0);
        const auto reference = part.getPinPosition (1);

        const int id = sheet.addElement (ElementType::Scope, 0, 0);
        auto* scope = sheet.findElement (id);

        // Tries every orientation until both scope pins land on the part's pins.
        for (int orientation = 0; orientation < 4; ++orientation)
        {
            scope->orientation = orientation;

            // Back to the origin each try: getPinPosition includes x and y.
            scope->x = 0;
            scope->y = 0;

            const auto offset = scope->getPinPosition (0);
            scope->x = positive.x - offset.x;
            scope->y = positive.y - offset.y;

            if (scope->getPinPosition (1) == reference)
                return id;
        }

        return 0;
    }

    void pump (PluginProcessor& plugin, int blocks, float level)
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

    /** The first pin of the first element of a type, in grid coordinates. */
    juce::Point<int> pinOfType (const Schematic& sheet, ElementType type)
    {
        for (const auto& e : sheet.getElements())
            if (e.type == type)
                return e.getPinPosition (0);

        return { 0, 0 };
    }

    const Element* firstResistor (const Schematic& sheet)
    {
        for (const auto& e : sheet.getElements())
            if (e.type == ElementType::Resistor && e.value > 0.0)
                return &e;

        return nullptr;
    }
}

// ==============================================================================
// The potentiometer's taper letter.

TEST_CASE ("A potentiometer is written the way one is marked", "[pot][format]")
{
    CHECK (SymbolPainter::formatPotValue (10000.0, Taper::Linear) == "B10K");
    CHECK (SymbolPainter::formatPotValue (1000.0, Taper::Logarithmic) == "A1K");
    CHECK (SymbolPainter::formatPotValue (500000.0, Taper::ReverseLogarithmic) == "C500K");

    // Upper case throughout, as marked on the part: "B10K".
    CHECK (! SymbolPainter::formatPotValue (10000.0, Taper::Linear).contains ("k"));

    // An unset pot is a bare "0", like every unset value.
    CHECK (SymbolPainter::formatPotValue (0.0, Taper::Logarithmic) == "0");
}

TEST_CASE ("A potentiometer's marking round-trips", "[pot][format]")
{
    for (const auto taper : { Taper::Linear, Taper::Logarithmic, Taper::ReverseLogarithmic })
    {
        for (const double ohms : { 1000.0, 10000.0, 250000.0, 1000000.0 })
        {
            const auto text = SymbolPainter::formatPotValue (ohms, taper);

            double parsed = 0.0;
            auto readBack = Taper::Linear;
            bool given = false;

            INFO (text);
            REQUIRE (SymbolPainter::parsePotValue (text, parsed, readBack, given));
            CHECK (given);
            CHECK (readBack == taper);
            CHECK (parsed == Catch::Approx (ohms));
        }
    }
}

TEST_CASE ("Typing a bare value leaves a potentiometer's taper alone", "[pot][format]")
{
    // Correcting the resistance keeps the taper.
    double parsed = 0.0;
    auto taper = Taper::Logarithmic;
    bool given = true;

    REQUIRE (SymbolPainter::parsePotValue ("250k", parsed, taper, given));
    CHECK (! given);
    CHECK (taper == Taper::Logarithmic);
    CHECK (parsed == Catch::Approx (250000.0));

    // Lower case is how it gets typed.
    CHECK (SymbolPainter::parsePotValue ("a1k", parsed, taper, given));
    CHECK (given);
    CHECK (taper == Taper::Logarithmic);

    // A letter that names no taper isn't one: "M" stays the mega prefix.
    CHECK (SymbolPainter::parsePotValue ("M2", parsed, taper, given));
    CHECK (! given);
}

// ==============================================================================
// The current readout.

TEST_CASE ("Every resistor on the sheet can have its current read", "[current]")
{
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    plugin.prepareToPlay (48000.0, 512);
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    int resistors = 0;

    for (const auto& e : sheet.getElements())
        if (e.type == ElementType::Resistor && e.value > 0.0)
            ++resistors;

    REQUIRE (resistors > 0);
    CHECK (static_cast<int> (result.currentProbes.size()) == resistors);

    for (const auto& probe : result.currentProbes)
    {
        const auto* element = sheet.findElement (probe.elementId);
        REQUIRE (element != nullptr);
        CHECK (probe.resistance == Catch::Approx (element->value));
    }
}

TEST_CASE ("A part's current is its voltage over its resistance", "[current]")
{
    // Checked against the scope rather than a computed number: the two readings take
    // different paths, and Ohm's law between them shows both read the same part.
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const auto* resistor = firstResistor (sheet);
    REQUIRE (resistor != nullptr);

    const int elementId = resistor->id;
    const double ohms = resistor->value;
    const int scope = addScopeAcross (sheet, *resistor);
    REQUIRE (scope != 0);

    // Built before preparing, so prepareToPlay swaps in directly and both averaging
    // windows start together.
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());
    plugin.prepareToPlay (48000.0, 512);

    plugin.setInspectedElement (elementId);

    float amps = 0.0f, watts = 0.0f, peak = 0.0f;

    // Nothing is published until a whole window has been accumulated.
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));

    pump (plugin, 120, 0.6f);

    REQUIRE (plugin.readInspectedCurrent (amps, watts, peak));

    const auto* trace = plugin.getScopeTrace (scope);
    REQUIRE (trace != nullptr);

    const double volts = trace->dcAverage.load();

    // Mean current is mean voltage over R (averaging is linear). The tolerance covers the
    // windows' different start times.
    INFO ("across " << volts << " V, " << ohms << " ohm, read " << amps << " A");
    CHECK (static_cast<double> (amps)
           == Catch::Approx (volts / ohms).epsilon (0.05).margin (1.0e-9));

    // Mean power is mean(V*I), which exceeds mean(V)^2/R by the variance for a moving
    // signal.
    CHECK (static_cast<double> (watts)
           >= static_cast<double> (amps) * static_cast<double> (amps) * ohms - 1.0e-12);

    CHECK (peak >= std::abs (amps));
}

TEST_CASE ("Nothing is measured when nothing is selected", "[current]")
{
    // With no readable part inspected, the audio thread does no measuring.
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const auto* resistor = firstResistor (sheet);
    REQUIRE (resistor != nullptr);

    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.rebuild().isValid());

    plugin.setInspectedElement (resistor->id);
    pump (plugin, 120, 0.6f);

    float amps = 0.0f, watts = 0.0f, peak = 0.0f;
    REQUIRE (plugin.readInspectedCurrent (amps, watts, peak));

    // Deselecting stops the reading immediately, not at the end of the window.
    plugin.setInspectedElement (0);
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));

    pump (plugin, 120, 0.6f);
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));
}

// ==============================================================================
// The scope's axes.

TEST_CASE ("A scope's axes survive a save and load", "[scope][axes]")
{
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const int id = sheet.addElement (ElementType::Scope, 0, 0);
    {
        auto* scope = sheet.findElement (id);
        scope->scopeAutoScale = false;
        scope->scopeMin = -250.0;
        scope->scopeMax = 400.0;
        scope->scopeSeconds = 0.005;
    }

    const auto document = plugin.createDocument();
    PluginProcessor reloaded;
    REQUIRE (reloaded.restoreDocument (document));

    const auto* back = reloaded.getSchematic().findElement (id);
    REQUIRE (back != nullptr);
    REQUIRE (back->type == ElementType::Scope);

    CHECK (! back->scopeAutoScale);
    CHECK (back->scopeMin == Catch::Approx (-250.0));
    CHECK (back->scopeMax == Catch::Approx (400.0));
    CHECK (back->scopeSeconds == Catch::Approx (0.005));
}

TEST_CASE ("A pinned scope range is drawn as given", "[scope][axes]")
{
    // A fixed range maps quiet and loud traces to different heights (auto-scale makes
    // them the same).
    Celine::ScopeReading reading;
    reading.live = true;
    reading.autoScale = false;
    reading.rangeMin = -10.0f;
    reading.rangeMax = 10.0f;

    for (int c = 0; c < Celine::ScopeReading::columns; ++c)
    {
        reading.minimum[c] = -1.0f;
        reading.maximum[c] = 1.0f;
    }

    juce::Image image (juce::Image::ARGB, 128, 64, true);
    juce::Graphics g (image);

    const auto scale = Celine::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
                                                    juce::Colours::white, juce::Colours::grey,
                                                    juce::Colours::black);

    REQUIRE (scale.valid);

    // Taken as given, without the auto path's 15% headroom.
    CHECK (scale.lowest == Catch::Approx (-10.0f));
    CHECK (scale.highest == Catch::Approx (10.0f));

    // Auto-scaling on the same data fits it instead.
    reading.autoScale = true;
    const auto fitted = Celine::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
                                                     juce::Colours::white, juce::Colours::grey,
                                                     juce::Colours::black);

    REQUIRE (fitted.valid);
    CHECK (fitted.highest < 2.0f);
    CHECK (fitted.lowest > -2.0f);
}

TEST_CASE ("An unusable scope range falls back to fitting the data", "[scope][axes]")
{
    // An inverted or empty range from a hand-edited file must not reach the renderer's
    // division.
    Celine::ScopeReading reading;
    reading.live = true;
    reading.autoScale = false;
    reading.rangeMin = 5.0f;
    reading.rangeMax = 5.0f;

    for (int c = 0; c < Celine::ScopeReading::columns; ++c)
    {
        reading.minimum[c] = -2.0f;
        reading.maximum[c] = 2.0f;
    }

    juce::Image image (juce::Image::ARGB, 128, 64, true);
    juce::Graphics g (image);

    const auto scale = Celine::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
                                                    juce::Colours::white, juce::Colours::grey,
                                                    juce::Colours::black);

    REQUIRE (scale.valid);
    CHECK (scale.highest > scale.lowest);
    CHECK (scale.highest == Catch::Approx (2.3f).margin (0.2f));
}

TEST_CASE ("Each scope keeps its own timebase", "[scope][axes]")
{
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const auto output = pinOfType (sheet, ElementType::Output);
    const int fast = sheet.addElement (ElementType::Scope, output.x, output.y - 6);
    const int slow = sheet.addElement (ElementType::Scope, output.x, output.y - 12);

    sheet.findElement (fast)->scopeSeconds = 0.002;
    sheet.findElement (slow)->scopeSeconds = 0.500;

    plugin.prepareToPlay (48000.0, 512);
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    // The timebase travels on the probe, which is what the audio thread has.
    REQUIRE (result.probes.size() >= 2);

    bool sawFast = false, sawSlow = false;

    for (const auto& probe : result.probes)
    {
        if (probe.elementId == fast) { sawFast = true; CHECK (probe.windowSeconds == Catch::Approx (0.002)); }
        if (probe.elementId == slow) { sawSlow = true; CHECK (probe.windowSeconds == Catch::Approx (0.500)); }
    }

    CHECK (sawFast);
    CHECK (sawSlow);
}

TEST_CASE ("A time span round-trips through its own text", "[scope][axes][format]")
{
    // parseValue must read back what formatValue writes for seconds ("40ms").
    for (const double seconds : { 0.001, 0.002, 0.005, 0.04, 0.5, 1.0 })
    {
        const auto text = SymbolPainter::formatValue (seconds, "s");

        double parsed = 0.0;
        INFO (seconds << " -> \"" << text << "\"");
        REQUIRE (SymbolPainter::parseValue (text, parsed));
        CHECK (parsed == Catch::Approx (seconds));
    }

    // And forms someone would type.
    double parsed = 0.0;
    CHECK (SymbolPainter::parseValue ("20ms", parsed));
    CHECK (parsed == Catch::Approx (0.02));

    CHECK (SymbolPainter::parseValue ("0.5 s", parsed));
    CHECK (parsed == Catch::Approx (0.5));

    CHECK (SymbolPainter::parseValue ("500us", parsed));
    CHECK (parsed == Catch::Approx (0.0005));

    // The seconds unit doesn't break ohms.
    CHECK (SymbolPainter::parseValue ("10 ohms", parsed));
    CHECK (parsed == Catch::Approx (10.0));

    CHECK (SymbolPainter::parseValue ("4k7 ohms", parsed));
    CHECK (parsed == Catch::Approx (4700.0));
}
