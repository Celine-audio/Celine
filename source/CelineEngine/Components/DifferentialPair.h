#pragma once

#include "Junction.h"
#include "Ports.h"

#include <algorithm>
#include <cmath>

namespace CircuitComponents
{
    //==========================================================================
    /**
        An op-amp's input stage: a long-tailed pair feeding the compensation
        node, as in the Boyle macromodel.

        Small-signal it is the linear transconductance the op-amp always had --
        current into the gain node proportional to the differential input. What
        it adds is the tail: the pair can only ever steer the current its tail
        carries, so the current into the compensation capacitor is bounded,
        and with it how fast the output can move. That bound *is* the slew rate:

            i = Itail * tanh(vd / Vlin),    Vlin = Itail / gm

        tanh is a bipolar pair's own transfer curve. Two consequences fall out
        of choosing Itail = SR * C and gm = 2 pi GBW * C, as a real part's
        designer does, rather than being set separately:

          - the small-signal behaviour is unchanged, so gain-bandwidth stays
            exactly where it was;
          - the input voltage at which the stage runs out of current is
            Vlin = SR / (2 pi GBW) -- 48 mV on an LM308, 0.69 V on a TL072 --
            which is why a slow, wide part like the 308 slews on signals a
            TL072 passes untouched.

        It is a nonlinear element on the differential input, which is the thing
        OpAmp.h warns broke the DK solve when the *whole* op-amp was one. That
        failure was a saturating output fed straight back; this one sits behind
        the gain node's clamps and a linear output buffer, and the step limiter
        below keeps Newton from leaping off the flat of the tanh. The tests
        hold both solver strategies to the same answer.

        Two ports: (in+, in-), through which nothing flows but gmin -- the
        op-amp's input resistance is its own resistor -- and (from, to), the
        current the pair delivers.
    */
    struct DifferentialPair
    {
        NodeIndex inPlus, inMinus;
        NodeIndex from, to;

        double transconductance = 1.0; // gm, A/V
        double tailCurrent = 1.0;      // Itail, A

        /** How much of the pair's small-signal input resistance is emitter
            resistors rather than the transistors' own: RE*Itail / (2Vt +
            RE*Itail), from 0 to just under 1. Zero is the bare pair and its
            tanh. Degeneration keeps the pair linear further out and then
            runs it out of current more abruptly -- which is where slewing
            starts, and so what shapes a slow part's edge as it begins to slew.
            Same small-signal gm and same tail current either way. */
        double degeneration = 0.0;

        /** The input voltage the last Newton iteration linearised around. */
        double vLast = 0.0;

        /** The differential input at which the stage runs out of current. */
        double linearRange() const noexcept { return tailCurrent / transconductance; }
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const DifferentialPair&) noexcept { return 2; }

    inline void fillPorts(const DifferentialPair& d, Port* ports) noexcept
    {
        ports[0] = {d.inPlus, d.inMinus};
        ports[1] = {d.from, d.to};
    }

    inline bool limitPortVoltages(DifferentialPair& d, double* v) noexcept
    {
        // Out on the flat of the tanh the slope is nearly zero, so a Newton step
        // computed there is enormous and means nothing. Inside the linear range
        // steps are held to twice that range; out of saturation one step may
        // come back across the whole of the flat to its edge, so recovering from
        // a slew is a few iterations rather than a march. Saturated on the same
        // side before and after, the current hardly changes and nothing is held.
        const double range = d.linearRange();
        const double edge = 3.0 * range;
        const double old = d.vLast;
        const double wanted = v[0];

        const bool staysSaturated = std::abs(old) > edge && std::abs(wanted) > edge
                                 && (old > 0.0) == (wanted > 0.0);

        double next = wanted;

        if (! staysSaturated)
        {
            const double reach = 2.0 * range + std::max(0.0, std::abs(old) - edge);
            next = std::clamp(wanted, old - reach, old + reach);
        }

        d.vLast = next;
        v[0] = next;
        return std::abs(next - wanted) > 0.0;
    }

    /** The fraction x of the tail steered to one side, and dx/du, for a
        normalised input u = vd / linearRange. A bare pair is x = tanh(u); with
        emitter resistors the input is shared between the junctions and the
        resistors,

            u = (1 - d) atanh(x) + d x,

        which has no closed form, so it is solved here by Newton -- a few steps
        from tanh(u), which is exact at d = 0 and always on the near side of the
        root, since x <= atanh(x). Monotonic and convex for x > 0, so Newton
        from there overshoots once and then closes in from above; the step is
        held inside (-1, 1). */
    inline void steerTail(double u, double degeneration, double& x, double& slope) noexcept
    {
        if (degeneration <= 0.0)
        {
            x = std::tanh(u);
            slope = 1.0 - x * x;
            return;
        }

        const double sign = u < 0.0 ? -1.0 : 1.0;
        const double target = std::abs(u);
        const double bare = 1.0 - degeneration;

        // The largest fraction a double can hold below one. tanh of anything
        // past about 19 *is* 1.0 in double precision, and atanh(1) is infinite,
        // so a guess allowed to reach it turns the next step into a NaN. The
        // whole tail on one side is this, to sixteen figures.
        const double all = std::nextafter(1.0, 0.0);
        double guess = std::min(std::tanh(target), all);

        for (int i = 0; i < 40; ++i)
        {
            const double residual = bare * std::atanh(guess) + degeneration * guess - target;
            const double derivative = bare / (1.0 - guess * guess) + degeneration;
            double next = guess - residual / derivative;

            if (next >= all)
                next = std::min(0.5 * (guess + 1.0), all);

            if (next < 0.0)
                next = 0.5 * guess;

            const bool settled = std::abs(next - guess) <= 1.0e-15 + 1.0e-13 * guess;
            guess = next;

            if (settled)
                break;
        }

        x = sign * guess;
        slope = 1.0 / (bare / (1.0 - guess * guess) + degeneration);
    }

    inline void linearise(const DifferentialPair& d, const double* v, DeviceLinearisation& out) noexcept
    {
        double steered = 0.0, slope = 0.0;
        steerTail(v[0] / d.linearRange(), d.degeneration, steered, slope);

        out.current[0] = gmin * v[0];
        out.current[1] = d.tailCurrent * steered + gmin * v[1];

        out.jacobian[0] = gmin;
        out.jacobian[1] = 0.0;
        out.jacobian[2] = d.transconductance * slope;
        out.jacobian[3] = gmin;
    }
} // namespace CircuitComponents
