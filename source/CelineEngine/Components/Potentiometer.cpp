#include "Potentiometer.h"
#include "../Engine.h"

#include <algorithm>

namespace
{
    /** The fraction of the track a numbered audio taper has passed at half
        rotation, or 0 for the tapers that are not one of them.

        Zero as the "not one of these" answer rather than an optional: no real
        taper passes none of its track at noon, so the value can carry the
        question as well as the answer. */
    double audioTaperMidpoint(CircuitComponents::Potentiometer::Taper taper) noexcept
    {
        using Taper = CircuitComponents::Potentiometer::Taper;

        switch (taper)
        {
            case Taper::Audio5:  return 0.05;
            case Taper::Audio10: return 0.10;
            case Taper::Audio15: return 0.15;
            case Taper::Audio20: return 0.20;
            case Taper::Audio30: return 0.30;

            case Taper::Linear:
            case Taper::Logarithmic:
            case Taper::ReverseLogarithmic:
                break;
        }

        return 0.0;
    }
} // namespace

void CircuitComponents::Potentiometer::setPosition(Circuit& circuit, float position) const
{
    const double pos = std::clamp(static_cast<double>(position), 0.0, 1.0);

    // Taper normalized position (0.0 to 1.0)
    double normPos = pos;
    if (taper == Taper::Logarithmic)
    {
        normPos = pos * pos;
    }
    else if (taper == Taper::ReverseLogarithmic)
    {
        const double inv = 1.0 - pos;
        normPos = 1.0 - inv * inv;
    }
    else if (const double midpoint = audioTaperMidpoint(taper); midpoint > 0.0)
    {
        // Two straight segments meeting at half rotation, which is how the part
        // is built: an audio pot is two lengths of resistive track of different
        // resistivity, and `midpoint` is the fraction the wiper has passed when
        // it reaches the join. Straight lines rather than a curve is not an
        // approximation of the real thing, it *is* the real thing -- what a
        // power law would approximate is the two-segment shape.
        normPos = pos <= 0.5 ? 2.0 * midpoint * pos
                             : midpoint + 2.0 * (1.0 - midpoint) * (pos - 0.5);
    }

    // 3-terminal potentiometer (voltage divider)
    if (upperResistor >= 0 && lowerResistor >= 0)
    {
        const double topR = std::max(minResistance, maxResistance * (1.0 - normPos));
        const double botR = std::max(minResistance, maxResistance * normPos);

        circuit.setResistance(upperResistor, topR);
        circuit.setResistance(lowerResistor, botR);
    }
    // 2-terminal variable resistor (rheostat) - increasing resistance with knob position
    else if (lowerResistor >= 0)
    {
        const double R = minResistance + (maxResistance - minResistance) * normPos;
        circuit.setResistance(lowerResistor, std::max(minResistance, R));
    }
    // 2-terminal variable resistor (rheostat) - decreasing resistance with knob position
    else if (upperResistor >= 0)
    {
        const double R = minResistance + (maxResistance - minResistance) * (1.0 - normPos);
        circuit.setResistance(upperResistor, std::max(minResistance, R));
    }
}
