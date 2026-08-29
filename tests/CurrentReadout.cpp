#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <UI/SchematicSymbols.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using SchematicUI::SymbolPainter;

namespace
{
    /** Hangs a scope across a two-pin part, by coincident pins.

        Wires are avoided for the reason ScopeProbe.cpp records at length: an
        L-routed wire has to get where it is going, and it can pass through
        something else on the way. Pins landing on the same square connect with
        no wire at all. */
    int addScopeAcross (Schematic& sheet, const Element& part)
    {
        const auto positive = part.getPinPosition (0);
        const auto reference = part.getPinPosition (1);

        const int id = sheet.addElement (ElementType::Scope, 0, 0);
        auto* scope = sheet.findElement (id);

        // Every orientation, because the two parts need not be drawn the same
        // way round: a scope's pins are four squares apart horizontally and a
        // resistor's are four apart vertically, so the un-rotated probe lands
        // one pin on the part and the other on empty sheet -- where it reads
        // against whatever else happens to be there rather than failing.
        for (int orientation = 0; orientation < 4; ++orientation)
        {
            scope->orientation = orientation;

            // Back to the origin before each try, because getPinPosition returns
            // a *position*, not an offset -- it has x and y already added in. A
            // previous iteration's placement would otherwise be counted twice
            // and every attempt after the first would land somewhere further
            // away than the last.
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

//==============================================================================
// The potentiometer's taper letter.

TEST_CASE ("A potentiometer is written the way one is marked", "[pot][format]")
{
    CHECK (SymbolPainter::formatPotValue (10000.0, Taper::Linear) == "B10K");
    CHECK (SymbolPainter::formatPotValue (1000.0, Taper::Logarithmic) == "A1K");
    CHECK (SymbolPainter::formatPotValue (500000.0, Taper::ReverseLogarithmic) == "C500K");

    // Upper case throughout, unlike the rest of the sheet's notation: "B10K" is
    // the marking on the part, and "B10k" is a half-translation of it.
    CHECK (! SymbolPainter::formatPotValue (10000.0, Taper::Linear).contains ("k"));

    // An unset pot stays the bare "0" every other unset value is, so the
    // red-for-unset colouring keeps meaning one thing.
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
    // Someone correcting a resistance did not ask to change the taper, and
    // silently resetting it to linear would be a nasty way to find that out.
    double parsed = 0.0;
    auto taper = Taper::Logarithmic;
    bool given = true;

    REQUIRE (SymbolPainter::parsePotValue ("250k", parsed, taper, given));
    CHECK (! given);
    CHECK (taper == Taper::Logarithmic);
    CHECK (parsed == Catch::Approx (250000.0));

    // Lower case is how it will actually get typed.
    CHECK (SymbolPainter::parsePotValue ("a1k", parsed, taper, given));
    CHECK (given);
    CHECK (taper == Taper::Logarithmic);

    // A letter that names no taper is not a taper. "M" here would otherwise eat
    // the megohm prefix off a value written without a digit before it.
    CHECK (SymbolPainter::parsePotValue ("M2", parsed, taper, given));
    CHECK (! given);
}

//==============================================================================
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
    // Checked against the scope rather than against a number worked out here:
    // the two readings come down different paths -- one through getNodeVoltage
    // in sampleScopes, the other through sampleInspectedCurrent -- and Ohm's law
    // between them is a claim that both are reading the same part. A hardcoded
    // expectation would only say that one of them is self-consistent.
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    Examples::load (sheet, 0);

    const auto* resistor = firstResistor (sheet);
    REQUIRE (resistor != nullptr);

    const int elementId = resistor->id;
    const double ohms = resistor->value;
    const int scope = addScopeAcross (sheet, *resistor);
    REQUIRE (scope != 0);

    plugin.prepareToPlay (48000.0, 512);
    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    plugin.setInspectedElement (elementId);

    float amps = 0.0f, watts = 0.0f, peak = 0.0f;

    // Nothing is published until a whole window has been accumulated, so a
    // reading before that is correctly refused rather than half-averaged.
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));

    pump (plugin, 120, 0.6f);

    REQUIRE (plugin.readInspectedCurrent (amps, watts, peak));

    const auto* trace = plugin.getScopeTrace (scope);
    REQUIRE (trace != nullptr);

    const double volts = trace->dcAverage.load();

    // Mean current is mean voltage over R even when the signal is moving, since
    // averaging is linear and I = V/R holds sample by sample. Loose tolerance
    // because the two averages are over windows that start at different times,
    // not because the relationship is approximate.
    INFO ("across " << volts << " V, " << ohms << " ohm, read " << amps << " A");
    CHECK (static_cast<double> (amps)
           == Catch::Approx (volts / ohms).epsilon (0.05).margin (1.0e-9));

    // Mean *power* is not mean(V)^2/R -- it is mean(V*I), which for anything
    // that moves is larger, by exactly the variance. That inequality is the
    // real invariant and it is worth pinning: publishing I_mean^2*R as the
    // power would understate what is heating the part, which is the one
    // question this readout exists to answer.
    CHECK (static_cast<double> (watts)
           >= static_cast<double> (amps) * static_cast<double> (amps) * ohms - 1.0e-12);

    CHECK (peak >= std::abs (amps));
}

TEST_CASE ("Nothing is measured when nothing is selected", "[current]")
{
    // The audio thread does the measuring, so an inspector that is closed --
    // or open on a part with no readable current -- has to cost it nothing.
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

    // Deselecting has to stop it reading immediately, not at the end of the
    // window: a readout that spends 40 ms still showing the part you just
    // clicked away from is long enough to read and believe.
    plugin.setInspectedElement (0);
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));

    pump (plugin, 120, 0.6f);
    CHECK (! plugin.readInspectedCurrent (amps, watts, peak));
}

//==============================================================================
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
    // The whole reason to pin a range is that the picture stops resizing itself
    // to its contents -- so what is under test is that a *quiet* trace and a
    // loud one map to different heights, where auto-scaling makes them the same.
    SchematicUI::ScopeReading reading;
    reading.live = true;
    reading.autoScale = false;
    reading.rangeMin = -10.0f;
    reading.rangeMax = 10.0f;

    for (int c = 0; c < SchematicUI::ScopeReading::columns; ++c)
    {
        reading.minimum[c] = -1.0f;
        reading.maximum[c] = 1.0f;
    }

    juce::Image image (juce::Image::ARGB, 128, 64, true);
    juce::Graphics g (image);

    const auto scale = SchematicUI::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
                                                    juce::Colours::white, juce::Colours::grey,
                                                    juce::Colours::black);

    REQUIRE (scale.valid);

    // Taken as given, with none of the 15% headroom the auto path adds: the
    // point of typing a range is that the number you asked for lands on the
    // edge of the picture.
    CHECK (scale.lowest == Catch::Approx (-10.0f));
    CHECK (scale.highest == Catch::Approx (10.0f));

    // And auto-scaling on the same data fits it instead, which is the contrast
    // the setting exists for.
    reading.autoScale = true;
    const auto fitted = SchematicUI::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
                                                     juce::Colours::white, juce::Colours::grey,
                                                     juce::Colours::black);

    REQUIRE (fitted.valid);
    CHECK (fitted.highest < 2.0f);
    CHECK (fitted.lowest > -2.0f);
}

TEST_CASE ("An unusable scope range falls back to fitting the data", "[scope][axes]")
{
    // An inverted or empty range divides by zero in the renderer. The inspector
    // refuses one, but a hand-edited file can carry one in, and a line of
    // infinities is a poor way to find that out.
    SchematicUI::ScopeReading reading;
    reading.live = true;
    reading.autoScale = false;
    reading.rangeMin = 5.0f;
    reading.rangeMax = 5.0f;

    for (int c = 0; c < SchematicUI::ScopeReading::columns; ++c)
    {
        reading.minimum[c] = -2.0f;
        reading.maximum[c] = 2.0f;
    }

    juce::Image image (juce::Image::ARGB, 128, 64, true);
    juce::Graphics g (image);

    const auto scale = SchematicUI::drawScopeTrace (g, { 0.0f, 0.0f, 128.0f, 64.0f }, reading,
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

    // Carried through the build rather than looked up again by the processor:
    // the probe is what the audio thread has, so the timebase has to be on it.
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
    // formatValue writes seconds with an engineering prefix -- 0.04 comes out
    // as "40ms" -- and that is the text the inspector puts in the box. Without
    // a matching unit in parseValue the field displayed a value it then refused
    // to read back, so typing anything into it fell through to the old number.
    for (const double seconds : { 0.001, 0.002, 0.005, 0.04, 0.5, 1.0 })
    {
        const auto text = SymbolPainter::formatValue (seconds, "s");

        double parsed = 0.0;
        INFO (seconds << " -> \"" << text << "\"");
        REQUIRE (SymbolPainter::parseValue (text, parsed));
        CHECK (parsed == Catch::Approx (seconds));
    }

    // And the forms someone would actually type.
    double parsed = 0.0;
    CHECK (SymbolPainter::parseValue ("20ms", parsed));
    CHECK (parsed == Catch::Approx (0.02));

    CHECK (SymbolPainter::parseValue ("0.5 s", parsed));
    CHECK (parsed == Catch::Approx (0.5));

    CHECK (SymbolPainter::parseValue ("500us", parsed));
    CHECK (parsed == Catch::Approx (0.0005));

    // The seconds unit must not have eaten the ohms one on the way in.
    CHECK (SymbolPainter::parseValue ("10 ohms", parsed));
    CHECK (parsed == Catch::Approx (10.0));

    CHECK (SymbolPainter::parseValue ("4k7 ohms", parsed));
    CHECK (parsed == Catch::Approx (4700.0));
}
