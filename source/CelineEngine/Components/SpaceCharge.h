#pragma once

#include "Junction.h" // for gmin
#include "Types.h"

#include <algorithm>
#include <cmath>

//==============================================================================
/**
    The maths shared by every vacuum tube, the way Junction.h is shared by every
    semiconductor.

    A valve conducts by boiling electrons off a hot cathode and letting the
    other electrodes pull them across a vacuum. That gives a completely
    different law from a semiconductor junction: where a diode's current is
    exponential in voltage, a valve's is a 3/2 power of it (Child-Langmuir,
    space-charge-limited flow). The practical consequence is that valves are
    far gentler than diodes -- they compress rather than clip, they have soft
    knees, and a rectifier valve's forward drop rises with current instead of
    pinning near a fixed voltage. That last one is where amp "sag" comes from.

    Numerically the good news is that the 3/2 power is much better behaved than
    an exponential; the bad news is the Koren models built on top of it need a
    softplus, and a naive log(1+exp(x)) overflows long before the physics does.
*/
namespace CircuitComponents
{
    /** log(1 + exp(x)), evaluated so it never overflows.

        For large x it is x to within double precision, and for very negative x
        it is exp(x). Both tails matter: Koren's models multiply x by a KP of
        several hundred, so the raw exponential would overflow at grid voltages
        a valve sees routinely. */
    inline double softplus(double x) noexcept
    {
        if (x > 30.0)
            return x;
        if (x < -30.0)
            return fastOrExactExp(x);

        return fastOrExactLog1p(fastOrExactExp(x));
    }

    /** d/dx softplus(x) -- the logistic sigmoid, with the same tail care. */
    inline double softplusSlope(double x) noexcept
    {
        if (x > 30.0)
            return 1.0;
        if (x < -30.0)
            return fastOrExactExp(x);

        const double e = fastOrExactExp(x);
        return e / (1.0 + e);
    }

    /**
        Both of the above at once, which is how the Koren models actually want
        them: they need the value and its slope at the same point, and computing
        them separately means paying for exp(x) twice.

        The exponential is the single most expensive thing in a valve circuit --
        three triodes re-linearised four times a sample is a lot of them -- so
        sharing it is worth the extra function. Results are bit-identical to
        calling softplus() and softplusSlope() in turn.
    */
    inline void softplusWithSlope(double x, double& value, double& slope) noexcept
    {
        if (x > 30.0)
        {
            value = x;
            slope = 1.0;
            return;
        }

        const double e = fastOrExactExp(x);

        if (x < -30.0)
        {
            value = e;
            slope = e;
            return;
        }

        value = fastOrExactLog1p(e);
        slope = e / (1.0 + e);
    }

    /**
        Child-Langmuir space-charge flow: i = perveance * v^1.5 while the
        electrode is positive, and nothing when it isn't -- a valve conducts one
        way only, which is what makes a rectifier a rectifier.

        Used directly for a rectifier valve, and again for the grid of a triode
        or pentode, which starts drawing current the moment it goes positive
        with respect to the cathode. That grid conduction is not a detail: it's
        what makes an overdriven valve stage block and splutter rather than
        clip cleanly.

        The derivative at zero is zero rather than discontinuous, so Newton
        crosses the turn-on point smoothly. gmin keeps the reverse side from
        being a perfect open circuit.
    */
    inline void evaluateSpaceCharge(double v, double perveance, double& current, double& conductance) noexcept
    {
        if (v <= 0.0)
        {
            current = gmin * v;
            conductance = gmin;
            return;
        }

        const double root = std::sqrt(v);

        current = perveance * v * root + gmin * v; // perveance * v^1.5
        conductance = 1.5 * perveance * root + gmin;
    }

    /**
        Clamps how far a Newton step may move an electrode voltage.

        Valves swing hundreds of volts, and an early Newton iteration that has
        the plate voltage badly wrong can propose a step of thousands. The Koren
        models won't diverge on that the way an exponential diode would -- the
        softplus saturates -- but a step that large lands somewhere the tangent
        says nothing useful about, and the iteration wanders instead of
        converging. Capping it costs an iteration occasionally and prevents that.

        This is the valve equivalent of limitJunctionVoltage() in Junction.h,
        and it can afford to be far cruder, because the underlying nonlinearity
        is far milder.
    */
    inline double limitStep(double vNew, double vOld, double maxStep) noexcept
    {
        return std::clamp(vNew, vOld - maxStep, vOld + maxStep);
    }

    /** Largest step allowed per iteration on a plate or screen, in volts.
        Generous: it is a safety net, not part of the normal path. */
    inline constexpr double maxPlateStep = 100.0;

    /** And on a grid, which operates over a far smaller range. */
    inline constexpr double maxGridStep = 10.0;
} // namespace CircuitComponents
