#pragma once

#include "Junction.h"
#include "Ports.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        Shockley diode parameters: i = Is * (exp(v / (N*Vt)) - 1).

        Is (saturation current) sets how early the diode starts conducting and
        N (emission coefficient) sets how soft the knee is -- between them they
        determine the forward voltage and the shape of the clipping, which is
        the whole reason different diodes sound different.
    */
    struct DiodeModel
    {
        double saturationCurrent = 2.52e-9;  // Is, amps
        double emissionCoefficient = 1.752;  // N, dimensionless
        double thermalVoltage = 0.025852;    // Vt, volts (kT/q at ~300 K)

        /** Reverse breakdown -- what makes a Zener a Zener.

            Every diode breaks down somewhere; an ordinary signal diode does it
            at a hundred volts or so, far outside anything a pedal will do to it,
            which is why the models above leave `breakdownVoltage` at zero and
            skip the maths entirely. A Zener is built to break down at a chosen
            low voltage and to be *used* there, clamping hard once the reverse
            voltage reaches it.

            `breakdownCurrent` is the current at the nominal voltage -- Zeners
            are specified at a test current, usually 5 mA -- and
            `breakdownEmission` sets how abruptly the knee arrives. Real parts
            below about 5 V have a noticeably soft knee (they're really
            avalanching rather than Zener-ing), which is why the low-voltage
            presets below use a larger value. */
        double breakdownVoltage = 0.0;    // Vz, volts; zero means "don't model it"
        double breakdownCurrent = 5.0e-3; // Ibv, the current at Vz
        double breakdownEmission = 1.0;   // knee sharpness

        /** N*Vt -- the exponential's scale factor, which is all the maths needs. */
        double scaleVoltage() const noexcept { return emissionCoefficient * thermalVoltage; }

        //======================================================================
        // Models. Forward voltages quoted at 1 mA.
        //
        // Only two numbers distinguish these: the saturation current, which sets
        // where conduction begins, and the emission coefficient, which sets how
        // gradually it gets there. The second is the one worth watching. Silicon
        // sits near 1.75 and an LED near 4, and that difference -- a knee several
        // times softer -- is most of why LED clipping sounds less abrupt than
        // silicon at the same drive, quite apart from the higher voltage.

        /** 1N4148 silicon signal diode, ~0.58 V. The classic clipper diode:
            Tube Screamer, Boss DS-1, ProCo Rat. */
        static DiodeModel d1n4148() noexcept { return {2.52e-9, 1.752, 0.025852}; }

        /** The generic name for the same part. */
        static DiodeModel silicon() noexcept { return d1n4148(); }

        /** 1N34A germanium, ~0.33 V. Lower forward voltage and a softer knee --
            the "warmer", more compressed clipping of a Fuzz Face or Tube Screamer
            with germanium diodes swapped in. */
        static DiodeModel germanium() noexcept { return {1.0e-7, 1.4, 0.025852}; }

        /** 1N5817 Schottky, ~0.30 V. Low forward voltage with a hard knee. */
        static DiodeModel schottky() noexcept { return {3.0e-8, 1.1, 0.025852}; }

        /** Generic red LED, ~1.56 V at 1 mA. Far more headroom than silicon, so
            a stage clips later and louder -- and with an emission coefficient
            over twice silicon's, the transition into clipping is much more
            gradual (Marshall Blues Breaker, Klon). */
        static DiodeModel redLed() noexcept { return {93.0e-12, 3.73, 0.025852}; }

        /** Generic green LED, ~1.93 V. Higher again, and softer still. */
        static DiodeModel greenLed() noexcept { return {93.0e-12, 4.61, 0.025852}; }

        /** Generic blue LED, ~3.19 V. The most headroom of the three by a wide
            margin, and the softest knee -- a stage clipped by these stays clean
            to levels where silicon would be squared off completely. */
        static DiodeModel blueLed() noexcept { return {93.0e-12, 7.61, 0.025852}; }

        /** Red, as the usual default when a circuit just says "LED".

            Note this changed: it used to be an approximation with an emission
            coefficient of 2.0, giving a knee far harder than a real LED has.
            Anything using led() will clip more gently than it did before, which
            is the correction, not a regression. */
        static DiodeModel led() noexcept { return redLed(); }

        /** A Zener of the given voltage, on a silicon forward characteristic.

            Two uses in a guitar circuit, and they want opposite things. As a
            supply clamp it just has to hold: pick a voltage above the rail and
            forget about it. As a clipping element -- back to back across a
            feedback loop, or a pair to ground -- the knee shape is the sound,
            and it clips one way at Vz and the other at a forward drop, so a
            single Zener is markedly asymmetric.

            Below about 5 V the knee softens considerably on real parts, so the
            emission coefficient is raised there to match. */
        static DiodeModel zener(double breakdownVolts) noexcept
        {
            DiodeModel model = silicon();
            model.breakdownVoltage = breakdownVolts;
            model.breakdownCurrent = 5.0e-3;
            model.breakdownEmission = breakdownVolts < 5.0 ? 4.0 : 1.5;
            return model;
        }
    };

    //==========================================================================
    /**
        A diode between two nodes, conducting from anode to cathode.

        `seriesCount` models several identical diodes stacked in series without
        the cost of extra nodes: n junctions in series share the same current,
        so the stack's I-V curve is the single-diode curve with the exponential's
        scale voltage multiplied by n. That's how clipping stages get their
        asymmetry -- e.g. two diodes one way, three the other.
    */
    struct Diode
    {
        NodeIndex anode, cathode;
        DiodeModel model;
        int seriesCount = 1;

        /** The junction voltage the last Newton iteration linearised around.
            Carried between iterations (and between samples) so the voltage
            limiter has a previous value to damp against. */
        double vLast = 0.0;

        /** criticalVoltage() for this device, cached by Circuit whenever the model
            changes so the Newton loop doesn't repeat the logarithm every iteration. */
        double vCrit = 0.0;

        /** The same, for the reverse breakdown knee of a Zener. */
        double vCritBreakdown = 0.0;

        /** The exponential's scale voltage for the whole series stack. */
        double scaleVoltage() const noexcept { return seriesCount * model.scaleVoltage(); }

        /** Where the stack breaks down, and how sharply -- the reverse
            direction's answer to scaleVoltage() above, and scaled for the same
            reason.

            n Zeners in series carry one current and each drops its own Vz, so
            the stack clamps at n*Vz with a knee n times wider. Left unscaled the
            forward direction was a stack and the reverse was a single diode: two
            Zeners back to back clipped at 0.7*n one way and at a single Vz the
            other.

            Methods rather than three scalings at the call sites, because there
            are three -- the current, the step limiter, and the critical voltage
            Circuit caches -- and a limiter damping against a knee the
            exponential is not using is the failure the comment in
            limitPortVoltages describes: it reports "acted" on every pass and
            burns the whole iteration limit, forever. */
        double breakdownVoltage() const noexcept { return seriesCount * model.breakdownVoltage; }

        double breakdownScaleVoltage() const noexcept
        {
            return seriesCount * model.breakdownEmission * model.thermalVoltage;
        }
    };

    /** Evaluates the diode at junction voltage `v`, returning its current and
        its small-signal conductance di/dv. */
    inline void evaluateDiode(const Diode& diode, double v, double& current, double& conductance) noexcept
    {
        evaluateJunction(v, diode.model.saturationCurrent, diode.scaleVoltage(), current, conductance);

        if (diode.model.breakdownVoltage <= 0.0)
            return;

        // Reverse breakdown, mirrored: the same exponential run backwards from
        // -Vz. At exactly -Vz the exponent is zero, so this contributes
        // breakdownCurrent -- which is what the datasheet's test current means.
        //
        // Both numbers come from the stack rather than the model, so a series
        // pair clamps at twice the voltage; the current does not scale, since
        // series diodes share one.
        const double vte = diode.breakdownScaleVoltage();
        const double e = fastOrExactExp(std::min(-(v + diode.breakdownVoltage()) / vte, maxExponent));

        current -= diode.model.breakdownCurrent * e;
        conductance += diode.model.breakdownCurrent * e / vte;
    }

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const Diode&) noexcept { return 1; }

    inline void fillPorts(const Diode& diode, Port* ports) noexcept
    {
        ports[0] = {diode.anode, diode.cathode};
    }

    /** Damps the Newton step across the junction, and reports whether it had to.
        `vLast` is updated to whatever the device will actually linearise about. */
    inline bool limitPortVoltages(Diode& diode, double* v) noexcept
    {
        double limited = limitJunctionVoltage(v[0], diode.vLast, diode.scaleVoltage(), diode.vCrit);

        // The limiter returns its argument untouched when it doesn't act, so any
        // difference at all means it damped the step. Written as a magnitude
        // rather than != to keep -Wfloat-equal quiet; the intent is exactness.
        bool acted = std::abs(limited - v[0]) > 0.0;

        // A Zener has a second exponential to run away down, on the other side.
        // Reflecting the voltage about -Vz turns it into the same problem and
        // lets the same limiter handle it.
        //
        // The result is only written back if the reverse limiter actually did
        // something. Reflecting and un-reflecting is not bit-exact across the
        // magnitudes involved here, so assigning unconditionally leaves a
        // rounding bit of difference behind -- which reads as "the limiter
        // acted", which blocks convergence, on every sample, forever. The answer
        // still comes out right; it just costs the full iteration limit to get
        // there.
        if (diode.model.breakdownVoltage > 0.0)
        {
            const double vte = diode.breakdownScaleVoltage();
            const double offset = -diode.breakdownVoltage();
            const double reflectedIn = offset - limited;
            const double reflectedOut = limitJunctionVoltage(reflectedIn,
                                                             offset - diode.vLast,
                                                             vte,
                                                             diode.vCritBreakdown);

            if (std::abs(reflectedOut - reflectedIn) > 0.0)
            {
                limited = offset - reflectedOut;
                acted = true;
            }
        }

        diode.vLast = limited;
        v[0] = limited;
        return acted;
    }

    inline void linearise(const Diode& diode, const double* v, DeviceLinearisation& out) noexcept
    {
        evaluateDiode(diode, v[0], out.current[0], out.jacobian[0]);
    }
} // namespace CircuitComponents
