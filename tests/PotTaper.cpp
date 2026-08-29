#include <CelineEngine/Engine.h>
#include <Schematic/Element.h>
#include <UI/SchematicSymbols.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using SchematicUI::SymbolPainter;
using Catch::Approx;

namespace
{
    /** A pot as a plain divider off a 1 V rail, read at the wiper.

        With an ideal source above it and nothing hanging off the wiper, the
        voltage there *is* the taper function -- so this measures the law
        directly rather than asserting a resistance nobody can read back. */
    double wiperAt (Circuit::Potentiometer::Taper taper, double position)
    {
        Circuit circuit;
        circuit.addVoltageSource ("top", "gnd", 1.0);
        auto pot = circuit.addPotentiometer ("top", "wiper", "gnd", 100000.0, taper);
        circuit.setInputNode ("unused");
        circuit.setOutputNode ("wiper");
        circuit.prepare (48000.0);
        pot.setPosition (circuit, static_cast<float> (position));

        // The pot only re-stamps resistances, so one sample is enough to read
        // the divider it now makes.
        for (int i = 0; i < 4; ++i)
            circuit.process (0.0f);

        return circuit.process (0.0f);
    }
}

TEST_CASE ("A numbered audio taper passes its own percentage at half rotation", "[pot][taper]")
{
    using EngineTaper = Circuit::Potentiometer::Taper;

    // The whole of what "10A" means: one tenth of the track at noon. If these
    // ever stop holding, the number in the part's name is a lie.
    CHECK (wiperAt (EngineTaper::Audio5,  0.5) == Approx (0.05).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio10, 0.5) == Approx (0.10).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio15, 0.5) == Approx (0.15).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio20, 0.5) == Approx (0.20).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio30, 0.5) == Approx (0.30).margin (0.002));

    // And the ends are still the ends, whatever happens between them.
    for (const auto taper : { EngineTaper::Audio5, EngineTaper::Audio30 })
    {
        CHECK (wiperAt (taper, 0.0) == Approx (0.0).margin (0.002));
        CHECK (wiperAt (taper, 1.0) == Approx (1.0).margin (0.002));
    }
}

TEST_CASE ("A numbered taper is usable low down where a power law is not", "[pot][taper]")
{
    // The reason these are two straight segments rather than a curve. A 10A pot
    // one fifth of the way round reads about 4%; the power law through the same
    // midpoint reads 0.48%, so the control would be eight times quieter there
    // -- a volume that does nothing until it suddenly does.
    const auto measured = wiperAt (Circuit::Potentiometer::Taper::Audio10, 0.2);
    const auto powerLaw = std::pow (0.2, std::log (0.1) / std::log (0.5));

    CHECK (measured == Approx (0.04).margin (0.002));
    CHECK (powerLaw == Approx (0.0048).margin (0.0005));
    CHECK (measured > 8.0 * powerLaw);
}

TEST_CASE ("The old tapers are exactly what they always were", "[pot][taper]")
{
    // Every sheet already saved names one of these three by number, so a change
    // here is a change to how somebody's amp sounds at a knob position they set
    // months ago.
    using EngineTaper = Circuit::Potentiometer::Taper;

    CHECK (wiperAt (EngineTaper::Linear, 0.5) == Approx (0.5).margin (0.002));
    CHECK (wiperAt (EngineTaper::Logarithmic, 0.5) == Approx (0.25).margin (0.002));
    CHECK (wiperAt (EngineTaper::ReverseLogarithmic, 0.5) == Approx (0.75).margin (0.002));
}

TEST_CASE ("Every taper round-trips through the pot caption", "[pot][format]")
{
    for (int i = 0; i < numTapers; ++i)
    {
        const auto taper = static_cast<Taper> (i);

        for (const double ohms : { 1000.0, 10000.0, 250000.0, 1000000.0 })
        {
            const auto text = SymbolPainter::formatPotValue (ohms, taper);

            double parsed = 0.0;
            auto readBack = Taper::Linear;
            bool given = false;

            INFO ("caption \"" << text << "\"");
            REQUIRE (SymbolPainter::parsePotValue (text, parsed, readBack, given));
            CHECK (given);
            CHECK (parsed == Approx (ohms));
            CHECK (readBack == taper);
        }
    }
}

TEST_CASE ("A bare resistance is never mistaken for a numbered taper", "[pot][format]")
{
    // "250K" starts with digits, and so does "10A250K". Reading the first as a
    // taper would silently retype every pot somebody corrected the value of.
    double parsed = 0.0;
    auto taper = Taper::Log15A;
    bool given = true;

    REQUIRE (SymbolPainter::parsePotValue ("250K", parsed, taper, given));
    CHECK (! given);
    CHECK (taper == Taper::Log15A);
    CHECK (parsed == Approx (250000.0));

    // And the numbered form still reads, upper or lower case.
    REQUIRE (SymbolPainter::parsePotValue ("10a 250k", parsed, taper, given));
    CHECK (given);
    CHECK (taper == Taper::Log10A);
    CHECK (parsed == Approx (250000.0));
}

TEST_CASE ("A reverse-log pot is not a log pot turned round", "[pot][taper]")
{
    using EngineTaper = Circuit::Potentiometer::Taper;

    // Turning a pot 180 degrees on the sheet swaps its two end pins, which is
    // the same circuit as wiring pin 1 and pin 3 the other way round. It looks
    // like it should make a reverse-log pot out of a log one. It does not, and
    // this is the number that says so.
    Circuit swapped;
    swapped.addVoltageSource ("top", "gnd", 1.0);
    auto turnedRound = swapped.addPotentiometer ("gnd", "wiper", "top", 100000.0,
                                                 EngineTaper::Logarithmic);
    swapped.setInputNode ("unused");
    swapped.setOutputNode ("wiper");
    swapped.prepare (48000.0);
    turnedRound.setPosition (swapped, 0.25f);

    for (int i = 0; i < 4; ++i)
        swapped.process (0.0f);

    // A quarter turn up: the reverse-log pot is a little under halfway, the
    // turned-round log pot is nearly at the top. Same value at noon, nowhere
    // else -- and the turned-round one runs *backwards*, since it now falls as
    // the knob rises.
    CHECK (swapped.process (0.0f) == Approx (0.9375).margin (0.002));
    CHECK (wiperAt (EngineTaper::ReverseLogarithmic, 0.25) == Approx (0.4375).margin (0.002));

    CHECK (wiperAt (EngineTaper::ReverseLogarithmic, 0.25)
             < wiperAt (EngineTaper::ReverseLogarithmic, 0.75));
}
