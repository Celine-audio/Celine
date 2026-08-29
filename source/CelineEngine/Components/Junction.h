#pragma once

#include "FastMath.h"
#include "Types.h"

#include <algorithm>
#include <cmath>

//==============================================================================
/**
    The p-n junction maths shared by every semiconductor in this folder.

    A diode is one junction. A bipolar transistor is two of them sharing a base,
    plus a transport current coupling them. Both need the same three things:
    the exponential I-V law, its derivative, and -- critically -- a way to stop
    Newton-Raphson from diverging on that exponential.

    Anything nonlinear added later (FETs, triodes) will want these too.
*/
namespace CircuitComponents
{
    /** Conductance added in parallel with every nonlinear junction.

        This is SPICE's `gmin`. It's far too small to affect the audio (1e-12 S
        is a 1-teraohm resistor), but it guarantees every node has a DC path to
        somewhere, which keeps the matrix non-singular and stops Newton from
        stalling when a junction is hard off and its true conductance underflows
        to zero. */
    inline constexpr double gmin = 1.0e-12;

    /** Largest exponent we'll ever feed to std::exp.

        Voltage limiting (see limitJunctionVoltage) should keep us well below
        this; it's a backstop so that a pathological netlist produces a large
        number rather than an inf that poisons the whole matrix. */
    inline constexpr double maxExponent = 80.0;

    /**
        Evaluates a p-n junction at voltage `v`, returning its current and its
        small-signal conductance di/dv -- the two numbers Newton needs.

        @param saturationCurrent  Is, the scale of the exponential
        @param scaleVoltage       N*Vt, how many volts an e-fold of current costs
    */
    inline void evaluateJunction(double v,
                                 double saturationCurrent,
                                 double scaleVoltage,
                                 double& current,
                                 double& conductance) noexcept
    {
        const double e = fastOrExactExp(std::min(v / scaleVoltage, maxExponent));

        current = saturationCurrent * (e - 1.0) + gmin * v;
        conductance = saturationCurrent * e / scaleVoltage + gmin;
    }

    /**
        The voltage above which the junction's exponential grows faster than
        Newton can safely follow. Below it, an undamped step is fine.
    */
    inline double criticalVoltage(double saturationCurrent, double scaleVoltage) noexcept
    {
        return scaleVoltage * std::log(scaleVoltage / (std::sqrt(2.0) * saturationCurrent));
    }

    /**
        Damps a Newton step across a p-n junction (SPICE's `pnjlim`).

        Newton on an exponential is treacherous: linearise a little too far up
        the curve and the tangent shoots the next guess wildly past the
        solution, where the exponential is larger still, and the iteration
        diverges instead of converging. The fix is to take the step in log
        space once we're past the critical voltage, which turns the runaway
        into a controlled approach. Without this, a diode clipper blows up the
        moment you feed it a signal much above the forward voltage, and a
        transistor stage never finds its bias point at all.

        @param vNew   the junction voltage the latest solve produced
        @param vOld   the voltage the previous iteration linearised around
        @param vte    the junction's scale voltage
        @param vCrit  the junction's critical voltage
        @returns      a limited voltage safe to linearise around
    */
    inline double limitJunctionVoltage(double vNew, double vOld, double vte, double vCrit) noexcept
    {
        if (vNew > vCrit && std::abs(vNew - vOld) > 2.0 * vte)
        {
            if (vOld > 0.0)
            {
                const double arg = 1.0 + (vNew - vOld) / vte;
                vNew = arg > 0.0 ? vOld + vte * fastOrExactLog(arg) : vCrit;
            }
            else
            {
                // vNew > vCrit > 0 here, so the logarithm is always well defined.
                vNew = vte * fastOrExactLog(vNew / vte);
            }
        }

        return vNew;
    }
} // namespace CircuitComponents
