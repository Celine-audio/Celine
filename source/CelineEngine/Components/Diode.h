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

        //======================================================================
        // The rest is off -- zero -- in every model that predates it, and only
        // the parts that are *defined* by one of these carry a figure. Each is
        // wired by Circuit::addDiode() rather than computed here, except the
        // stored charge, which is the device's own.

        /** Series resistance, ohms -- RS on a SPICE card.

            On most diodes it is a fraction of an ohm and a clipping stage never
            finds it. A small Schottky is the exception: the BAT41's is tens of
            ohms, and at the milliamp a clipper runs at that is a tenth of a
            volt -- the difference between its knee and a plain exponential's.
            addDiode() puts it on an internal node, as SPICE does, since the
            current through it is what the junction is solving for. */
        double seriesResistance = 0.0;

        /** A second junction in parallel, straight across the terminals: the
            p-n guard ring a small-signal Schottky is built with to survive
            static. It sits idle below a few milliamps and then takes over from
            the Schottky, whose series resistance has run out of room -- which
            is what bends a BAT41's curve upward past 5 mA. Zero means none. */
        double guardRingSaturationCurrent = 0.0;
        double guardRingEmission = 2.0;

        /** Depletion capacitance, farads, as a constant across the terminals.

            A real one rises into forward bias and falls with reverse, so this is
            the figure at the bias a clipping diode spends its time near --
            about zero. Wired as an ordinary capacitor, and switched off with the
            other junction capacitances in BuildOptions. */
        double junctionCapacitance = 0.0;

        /** Transit time, seconds -- TT on a SPICE card.

            A conducting junction stores charge in proportion to its current,
            q = TT * i, and has to be emptied before it stops conducting. On a
            signal diode TT is nanoseconds and nothing at audio rates can see
            it. A rectifier is built slow on purpose: a 1N4001 stores
            microseconds of its own current, and at 10 kHz the current that
            charge carries is a third of the junction's own. That -- not its
            forward voltage -- is most of what separates it from a 1N4148 in a
            clipper.

            Integrated backward-Euler inside the device (see linearise()), so it
            costs no state variable in the matrix, and it is taken as zero while
            the bias point is solved, where nothing moves. */
        double transitTime = 0.0;

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

        /** 1N4001, the 1 A general-purpose rectifier -- and the TS808-mod
            clipping diode.

            DC and transit time are Motorola's card (Rectifier Databook,
            1991): Is 14.11 nA, N 1.984, TT 5.7 us. It lands on the Diodes Inc
            typical curve in docs/specs at 10 mA (0.69 V against 0.70) and
            runs 30-60 mV high by 1 A, where no clipper goes. Chosen over the
            two other published cards because it is the only one whose low end
            is physical: a big rectifier junction at microamps is dominated by
            recombination, which is an emission coefficient near 2, and that is
            where a pedal runs it -- 0.46 V at 100 uA and 0.57 V at 1 mA, a
            softer knee than a 1N4148's. Diodes Inc's own card (N 1.45, fitted
            at amps) puts 1 mA at 0.61 V instead; nobody publishes measurements
            down there, so treat this end as the better-grounded guess.

            Its capacitance is the Diodes Inc sheet's, about 30 pF near zero
            bias -- seven times a 1N4148's. The stored charge is what really
            sets it apart, though; see transitTime. */
        static DiodeModel d1n4001() noexcept
        {
            DiodeModel model;
            model.saturationCurrent = 14.11e-9;
            model.emissionCoefficient = 1.984;
            model.junctionCapacitance = 30.0e-12;
            model.transitTime = 5.7e-6;
            return model;
        }

        /** BAT41, small-signal Schottky with a p-n guard ring -- a common
            "between germanium and silicon" clipper.

            Structured as Vishay's model is (docs/specs has ST's sheet, and
            Vishay publish the card): a Schottky junction behind its series
            resistance, with the guard ring across the whole. Refitted to ST's
            typical curve, which Vishay's card runs 20-75 mV under, with the
            emission coefficients held physical. Through the engine: 0.20 V at
            10 uA, 0.28 at 100 uA, 0.39 at 1 mA (sheet 0.4 typ), 0.69 at 10 mA
            and 0.90 at 100 mA, within 4 mV rms of the sheet throughout.

            The 35 ohm resistance is why its knee is softer than a Schottky's
            ought to be, and why it clips at a silicon-like voltage once the
            current gets up. 2 pF of capacitance (ST, typ at 1 V). */
        static DiodeModel bat41() noexcept
        {
            DiodeModel model;
            model.saturationCurrent = 2.74e-8;
            model.emissionCoefficient = 1.294;
            model.seriesResistance = 34.94;
            model.guardRingSaturationCurrent = 1.176e-8;
            model.guardRingEmission = 2.2;
            model.junctionCapacitance = 2.0e-12;
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

        /** Stored charge, for a model with a transit time: TT/dt while audio
            runs and zero while the bias point is solved, set by Circuit. */
        double transitScale = 0.0;

        /** The junction current at the end of the last sample -- the charge it
            left behind, over TT. Updated by Circuit once a sample converges. */
        double chargeCurrentPrevious = 0.0;

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

    /** The current the junction's stored charge is proportional to: the
        forward Shockley current alone, without the breakdown or gmin terms. */
    inline double chargeCurrent(const Diode& diode, double v) noexcept
    {
        const double e = fastOrExactExp(std::min(v / diode.scaleVoltage(), maxExponent));
        return diode.model.saturationCurrent * (e - 1.0);
    }

    inline void linearise(const Diode& diode, const double* v, DeviceLinearisation& out) noexcept
    {
        evaluateDiode(diode, v[0], out.current[0], out.jacobian[0]);

        if (diode.transitScale <= 0.0)
            return;

        // Stored charge q = TT * i, differentiated backward-Euler:
        // dq/dt = (TT/dt) * (i - iPrevious). Backward rather than trapezoidal
        // because TT is shorter than a sample at any audio rate -- the charge
        // relaxes within one step, and the trapezoidal rule rings on a time
        // constant it cannot resolve where backward Euler just settles.
        //
        // The derivative is the junction's own conductance scaled up, so the
        // Jacobian keeps its sign and Newton sees an ordinary, stiffer diode.
        const double e = fastOrExactExp(std::min(v[0] / diode.scaleVoltage(), maxExponent));
        const double current = diode.model.saturationCurrent * (e - 1.0);

        out.current[0] += diode.transitScale * (current - diode.chargeCurrentPrevious);
        out.jacobian[0] += diode.transitScale * diode.model.saturationCurrent * e / diode.scaleVoltage();
    }
} // namespace CircuitComponents
