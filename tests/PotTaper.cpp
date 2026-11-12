#include <CelineEngine/Engine.h>
#include <Schematic/Element.h>
#include <ui/SchematicSymbols.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using Celine::SymbolPainter;
using Catch::Approx;

namespace
{
    /** A pot as a plain divider off a 1 V rail, read at the wiper: the voltage there is
        the taper function. */
    double wiperAt (Circuit::Potentiometer::Taper taper, double position)
    {
        Circuit circuit;
        circuit.addVoltageSource ("top", "gnd", 1.0);
        auto pot = circuit.addPotentiometer ("top", "wiper", "gnd", 100000.0, taper);
        circuit.setInputNode ("unused");
        circuit.setOutputNode ("wiper");
        circuit.prepare (48000.0);
        pot.setPosition (circuit, static_cast<float> (position));

        // The pot only re-stamps resistances, so one sample reads the new divider.
        for (int i = 0; i < 4; ++i)
            circuit.process (0.0f);

        return circuit.process (0.0f);
    }
}

TEST_CASE ("A numbered audio taper passes its own percentage at half rotation", "[pot][taper]")
{
    using EngineTaper = Circuit::Potentiometer::Taper;

    // "10A" means one tenth of the track at noon.
    CHECK (wiperAt (EngineTaper::Audio5,  0.5) == Approx (0.05).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio10, 0.5) == Approx (0.10).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio15, 0.5) == Approx (0.15).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio20, 0.5) == Approx (0.20).margin (0.002));
    CHECK (wiperAt (EngineTaper::Audio30, 0.5) == Approx (0.30).margin (0.002));

    // The ends are still the ends.
    for (const auto taper : { EngineTaper::Audio5, EngineTaper::Audio30 })
    {
        CHECK (wiperAt (taper, 0.0) == Approx (0.0).margin (0.002));
        CHECK (wiperAt (taper, 1.0) == Approx (1.0).margin (0.002));
    }
}

TEST_CASE ("A numbered taper is usable low down where a power law is not", "[pot][taper]")
{
    // Two straight segments rather than a curve: a power law through the same midpoint
    // reads 0.48% at one fifth instead of about 4%.
    const auto measured = wiperAt (Circuit::Potentiometer::Taper::Audio10, 0.2);
    const auto powerLaw = std::pow (0.2, std::log (0.1) / std::log (0.5));

    CHECK (measured == Approx (0.04).margin (0.002));
    CHECK (powerLaw == Approx (0.0048).margin (0.0005));
    CHECK (measured > 8.0 * powerLaw);
}

TEST_CASE ("The old tapers are exactly what they always were", "[pot][taper]")
{
    // Saved sheets name these by number, so they must not change.
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
    // "250K" starts with digits too, and must not be read as a taper.
    double parsed = 0.0;
    auto taper = Taper::Log15A;
    bool given = true;

    REQUIRE (SymbolPainter::parsePotValue ("250K", parsed, taper, given));
    CHECK (! given);
    CHECK (taper == Taper::Log15A);
    CHECK (parsed == Approx (250000.0));

    // The numbered form reads in either case.
    REQUIRE (SymbolPainter::parsePotValue ("10a 250k", parsed, taper, given));
    CHECK (given);
    CHECK (taper == Taper::Log10A);
    CHECK (parsed == Approx (250000.0));
}

TEST_CASE ("A reverse-log pot is not a log pot turned round", "[pot][taper]")
{
    using EngineTaper = Circuit::Potentiometer::Taper;

    // Rotating a pot 180 degrees swaps its end pins. That does not make a reverse-log pot
    // out of a log one.
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

    // A quarter turn up: reverse-log is a little under halfway, the turned-round log pot
    // nearly at the top (and it runs backwards).
    CHECK (swapped.process (0.0f) == Approx (0.9375).margin (0.002));
    CHECK (wiperAt (EngineTaper::ReverseLogarithmic, 0.25) == Approx (0.4375).margin (0.002));

    CHECK (wiperAt (EngineTaper::ReverseLogarithmic, 0.25)
             < wiperAt (EngineTaper::ReverseLogarithmic, 0.75));
}

TEST_CASE ("An amplifier's supply is captioned the way a schematic labels its rails", "[symbols]")
{
    CHECK (SymbolPainter::formatSupply (9.0, 0.0, "V") == "9V");
    CHECK (SymbolPainter::formatSupply (15.0, -15.0, "V") == juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + "15V");
    CHECK (SymbolPainter::formatSupply (12.0, -9.0, "V") == "+12V / -9V");
}
