#pragma once

#include "Junction.h"
#include "Ports.h"

#include <limits>

namespace CircuitComponents
{
    //==========================================================================
    /**
        Enhancement-mode MOSFET parameters: the discrete vertical parts a pedal
        uses -- 2N7000, BS170, BS250 -- with the source tied to the body.

        Three things set one apart from a JFET, and all three are audible.

        It is *off* at zero gate voltage and turns on past a threshold of a
        couple of volts, so a stage is biased by lifting the gate rather than
        by a source resistor pulling it below.

        The gate is insulated. It never conducts, whatever the drive -- so a
        MOSFET stage clips by cutoff and by its drain running out of room, but
        never by the gate-conduction blocking a JFET or a valve does.

        And a pedal runs it at a milliamp or so, which on these parts is barely
        above threshold: moderate inversion, where the current is neither the
        square law everyone quotes nor the exponential below it. So the channel
        is written as one smooth curve through both,

            Veff(x) = 2 n Vt ln(1 + exp(x / (2 n Vt)))
            Id      = K/2 (1 + lambda Vds) (Veff(Vgs - Vto)^2 - Veff(Vgs - Vto - Vds)^2)

        which is the level-1 square law exactly once Vgs - Vto is a few tenths
        of a volt (triode below Vds = Vgs - Vto, saturation above), falls away
        exponentially with slope n Vt below threshold, and is smooth everywhere
        -- Newton never meets the corner a level-1 model has at Vto, and the
        stage's gain does not jump as the signal swings the bias through it.
        The two-term form is also symmetric in drain and source, as the channel
        itself is.

        The body is tied to the source, which leaves a diode from source to
        drain -- the body diode -- that conducts when the drain is pulled the
        wrong way. It is part of the device and modelled as one.

        The source resistance is modelled, inside the device rather than as a
        resistor on a node of its own -- see sourceResistance. Not modelled: the
        drain's own series resistance, mobility degradation (both matter at
        amps, and the source resistance already carries most of what a
        datasheet's curves show of them), the voltage dependence of the
        capacitances, and the gate resistance.
    */
    struct MosfetModel
    {
        enum class Channel
        {
            N,
            P
        };

        Channel channel = Channel::N;

        /** Vto, volts, as a magnitude: positive for an N-channel part and for a
            P-channel one alike, the polarity sign handles which way. Note this
            is where the square law extrapolates to zero, a little below the
            datasheet's Vgs(th) -- which is the gate voltage for 1 mA. */
        double threshold = 2.0;

        /** K = KP * W / L, A/V^2: the square law's scale. */
        double transconductance = 0.1;

        /** n: how many times Vt a factor of e of current costs below threshold. */
        double subthresholdSlope = 1.5;

        double lambda = 0.0; // channel-length modulation, 1/V

        /** Rs, ohms, between the channel and the source terminal. Zero means
            none, and then nothing below runs: the answer is bit-identical to a
            card without one.

            An ohm or two, which sounds like nothing against a pedal's kilohms,
            and at a milliamp it is -- but it is what stops a square law growing
            for ever. Without it a card can match the output curves or the
            on-resistance and not both: fitted to the curves, the 2N7000 read
            0.79 ohm where its sheet says 2.2; fitted to the on-resistance, the
            BS170 read 40% low on every curve. The drain current flowing back
            through Rs lowers the gate drive it sees, Id = f(Vgs - Id Rs,
            Vds - Id Rs), so the transfer curve bends from square to straight
            as the current rises, which is what the real parts do.

            Solved inside linearise() rather than wired as a resistor to an
            internal node: no extra unknown, no change to the ports, and a model
            swap that adds or removes it changes no topology. The cost is a few
            Newton steps on one scalar per evaluation, and only for cards that
            carry one. */
        double sourceResistance = 0.0;

        double thermalVoltage = 0.025852;

        /** The body diode, source to drain for an N-channel part. */
        double bodyDiodeSaturationCurrent = 1.0e-13;
        double bodyDiodeEmission = 1.0;

        /** Gate-source and gate-drain capacitances, farads. Wired by
            addMosfet() as ordinary capacitors, the way a BJT's junction
            capacitances are, so the Miller multiplication of the gate-drain one
            comes out of the solve. Constant, at the bias a pedal sits at: both
            fall as their voltage rises, the gate-drain one steeply, so a
            datasheet's Crss -- quoted at 25 V -- reads several times low for a
            stage whose drain is a few volts above its gate. Zero means none. */
        double capGateSource = 0.0;
        double capGateDrain = 0.0;

        double polaritySign() const noexcept { return channel == Channel::N ? 1.0 : -1.0; }
        double smoothingVoltage() const noexcept { return 2.0 * subthresholdSlope * thermalVoltage; }
        double bodyDiodeScaleVoltage() const noexcept { return bodyDiodeEmission * thermalVoltage; }

        //======================================================================
        // Models.
        //
        // All three fitted to Vishay Siliconix sheets, which draw their curves
        // as vectors and so can be read exactly: document 70226 for the 2N7000
        // and BS170, 70209 for the BS250 (docs/specs/mosfets). Fitted through
        // these equations -- source resistance and all -- so the figures quoted
        // are what the engine produces.
        //
        // Threshold is still the parameter with the widest spread: 0.8 to 3 V
        // on the same part number for the N-channel pair, 1 to 3.5 V for the
        // BS250, which is why MOSFET boosts carry a bias trimmer. Each card is
        // the sheet's typical part, so it biases where a typical one does.
        //
        // Capacitances are read off the sheets' curves at the voltage a pedal
        // puts across them, not at the 25 V their tables quote. The curves are
        // taken with the gate at the source, so Crss there is the gate-drain
        // capacitance at Vdg = Vds; a stage biased with its gate just over
        // threshold and its drain near half a 9 V rail has Vdg of about 2.4 V
        // (N-channel) or 1.5 V (the BS250, whose threshold is higher). Cgs is
        // Ciss less Crss at the same point. The gate-drain one is the one that
        // matters, since a stage's gain multiplies it.

        /** 2N7000, Vishay Siliconix (document 70226), and the BS170 with it:
            Vishay print one set of typical curves for the two and the same
            typicals in both tables (Vgs(th) 2.1 V, rDS(on) 2.3 ohm at 10 V), so
            on their sheet the BS170 is this die in another lead-out -- its pins
            run drain, gate, source where the 2N7000's run source, gate, drain,
            which matters on a board and not on a sheet. onsemi's BS170 is a
            different, weaker die, about a third of the transconductance, and is
            not modelled.

                                              sheet      model
                Vgs(th), Vds = Vgs, 1 mA      2.1 V      2.102
                                  0.25 mA     2.0 V      2.000
                output curves, Vgs 2.5-7 V,   --         7.6% rms
                  Vds 0.25-4.5 V
                rDS(on)  Vgs 10 V, 0.1 A      2.21       2.01
                         Vgs 10 V, 0.5 A      2.37       2.07
                         Vgs 5 V, 50 mA       3.20       3.21
                gos, Vds 5 V, 50 mA           0.5 mS     0.48
                body diode, 1 / 10 / 100 mA   0.597 / 0.666 / 0.754 V
                                              model 0.593 / 0.670 / 0.747

            n is fitted rather than assumed: the sheet's two threshold points
            are 100 mV apart for a factor of four in current, which n = 1.5
            makes 86 mV and n = 2.0 makes 102.

            This card used to be onsemi's, fitted to their curves, which carry
            about 1.6 times this current above Vgs 3 V. Against Vishay's it read
            15% high at Vgs 3 V and 93% high at 7 V, and 0.79 ohm on where the
            sheet says 2.2. The pedal end barely moves: the threshold is where
            it was to within 5 mV.

            Capacitances at Vdg 2.4 V: Ciss 26.5 pF, Crss 6.3 -- half the 12 pF
            the old card carried, so the Miller pole of a stage sits an octave
            higher than it did. */
        static MosfetModel n2N7000() noexcept
        {
            MosfetModel m;
            m.channel = Channel::N;
            m.threshold = 2.0459;
            m.transconductance = 0.18571;
            m.subthresholdSlope = 2.0;
            m.lambda = 0.01206;
            m.sourceResistance = 1.320;
            m.bodyDiodeSaturationCurrent = 2.02e-11;
            m.bodyDiodeEmission = 1.295;
            m.capGateSource = 20.2e-12;
            m.capGateDrain = 6.3e-12;
            return m;
        }

        /** BS250, P-channel, Vishay Siliconix (document 70209, where it shares
            a sheet with the TP0610 and VP0610) -- the complement to the two
            above. Wire it as you would the N-channel parts -- the model flips
            the signs -- with the source at the positive rail.

                                              sheet      model
                output curves, Vgs 4-10 V     --         14% rms
                transfer, Vgs 4/4.5/5/6/7 V   64/156/276/573/879 mA
                                              model 63/140/234/458/712
                rDS(on), Vgs 10 V, 0.2 A      2.88       2.76

            The sheet's curves start at about 60 mA, and its transfer chart
            cannot be read below about 30 mA: at 1.2 A full scale the line
            itself is 15 mA wide. So the pedal region is extrapolated, with n
            taken as the 2N7000's 2.0, and lands 1 mA at 3.22 V -- inside the
            sheet's 1 to 3.5 V for the BS250, and 0.7 V above the card it
            replaces, which was Philips's 2.53. Nothing here confirms the exact
            figure; a trimmer absorbs it, as it absorbs the spread.

            The sheet's typical column is the TP0610L's, not the BS250's -- it
            says 8 ohm on and 15 pF in where the curves show 2.9 and 26 -- so the
            curves are what is fitted and the table only bounds.

            Body diode off its curve, 1 to 100 mA: 0.644 / 0.717 / 0.831 V
            against 0.635 / 0.728 / 0.822. The old card, an ideal junction, sat
            130 mV low at 100 mA. Capacitances at Vdg 1.5 V: Ciss 26.0 pF,
            Crss 14.2. A smaller die than other makers' BS250 (General
            Semiconductor's has three times the input capacitance and half the
            gate-drain), with more Miller capacitance than the N parts. */
        static MosfetModel pBS250() noexcept
        {
            MosfetModel m;
            m.channel = Channel::P;
            m.threshold = 3.1875;
            m.transconductance = 0.24238;
            m.subthresholdSlope = 2.0;
            m.lambda = 0.012; // not on the sheet; Zetex's ZVP2106A model
            m.sourceResistance = 2.111;
            m.bodyDiodeSaturationCurrent = 1.70e-10;
            m.bodyDiodeEmission = 1.575;
            m.capGateSource = 11.8e-12;
            m.capGateDrain = 14.2e-12;
            return m;
        }
    };

    //==========================================================================
    /**
        A MOSFET. Ports are (gate, source) and (drain, source), as the JFET's
        are, so the port voltages are Vgs and Vds.
    */
    struct Mosfet
    {
        NodeIndex drain, gate, source;
        MosfetModel model;

        double vGateLast = 0.0;
        double vDrainLast = 0.0;
        double vCritBody = 0.0; // the body diode's, cached by Circuit

        /** The channel current the source resistance was last solved for, which
            with the four below starts the next solve. Only a starting point:
            the answer does not depend on it beyond the solve's tolerance.
            Mutable because linearise() is a read of the device, and this does
            not change what it reads.

            Measured on a clipping 2N7000 stage: solved cold every time, the
            source resistance made the stage 3.1 times as expensive; started
            from the tangent at the last answer, 1.45 times. */
        mutable double channelCurrentLast = 0.0;

        /** Where that answer was found, and its slopes there, so the next
            solve can start from the tangent rather than from the answer
            itself: Newton's own steps shrink as it converges, and a first-order
            prediction across a small step is already inside the tolerance. */
        mutable double vgsLast = 0.0, vdsLast = 0.0, slopeGateLast = 0.0, slopeDrainLast = 0.0;
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const Mosfet&) noexcept { return 2; }

    inline void fillPorts(const Mosfet& m, Port* ports) noexcept
    {
        ports[0] = {m.gate, m.source};
        ports[1] = {m.drain, m.source};
    }

    /** Damps the Newton step, and reports whether it had to.

        Nothing on the gate side can run away -- the channel grows as a square,
        not an exponential, and the gate draws nothing -- but below threshold
        its slope is almost zero, and a tangent that flat throws the next guess
        tens of volts away. A plain clamp stops that without slowing anything
        that is converging. The body diode is a junction like any other and
        gets the junction limiter, reflected: its forward voltage is -Vds. */
    inline bool limitPortVoltages(Mosfet& m, double* v) noexcept
    {
        const double p = m.model.polaritySign();

        const double gate = std::clamp(p * v[0], m.vGateLast - 5.0, m.vGateLast + 5.0);

        const double bodyIn = -p * v[1];
        const double bodyOut = limitJunctionVoltage(bodyIn, -m.vDrainLast,
                                                    m.model.bodyDiodeScaleVoltage(), m.vCritBody);

        // Written back only if the limiter acted: reflecting a voltage twice is
        // not bit-exact, and a rounding bit reads as "acted" on every pass --
        // see the Zener's limiter in Diode.h.
        double drain = p * v[1];

        if (std::abs(bodyOut - bodyIn) > 0.0)
            drain = -bodyOut;

        drain = std::clamp(drain, m.vDrainLast - 20.0, m.vDrainLast + 20.0);

        const bool acted = std::abs(gate - p * v[0]) > 0.0 || std::abs(drain - p * v[1]) > 0.0;

        m.vGateLast = gate;
        m.vDrainLast = drain;
        v[0] = p * gate;
        v[1] = p * drain;
        return acted;
    }

    /** softplus(x) = ln(1 + e^x), and its derivative, written so neither
        overflows nor loses the small end. */
    inline void mosfetSoftplus(double x, double& value, double& slope) noexcept
    {
        if (x > 0.0)
        {
            const double e = fastOrExactExp(-x);
            value = x + std::log1p(e);
            slope = 1.0 / (1.0 + e);
        }
        else
        {
            const double e = fastOrExactExp(std::max(x, -maxExponent));
            value = std::log1p(e);
            slope = e / (1.0 + e);
        }
    }

    /** The channel's forward characteristic -- the drain current for a
        non-negative Vds, with its two partial derivatives. The reverse case is
        this with the channel's ends exchanged; see linearise(). */
    inline void evaluateMosfetChannel(const MosfetModel& model, double vgs, double vds,
                                      double& current, double& dIdVgs, double& dIdVds) noexcept
    {
        const double s = model.smoothingVoltage();

        double softA = 0.0, slopeA = 0.0, softB = 0.0, slopeB = 0.0;
        mosfetSoftplus((vgs - model.threshold) / s, softA, slopeA);
        mosfetSoftplus((vgs - model.threshold - vds) / s, softB, slopeB);

        // The effective overdrive at each end of the channel. In strong
        // inversion a is Vgs - Vto and b is what is left of it at the drain,
        // clamped smoothly at zero once the channel pinches off there.
        const double a = s * softA;
        const double b = s * softB;

        const double k = model.transconductance;
        const double modulation = 1.0 + model.lambda * vds;
        const double core = a * a - b * b;

        current = 0.5 * k * modulation * core;
        dIdVgs = k * modulation * (a * slopeA - b * slopeB);
        dIdVds = k * modulation * b * slopeB + 0.5 * k * model.lambda * core;
    }

    /** The channel at any Vds, with its two partial derivatives, at the
        voltages the channel itself sees.

        Symmetric in its two ends: for Vds < 0 the drain is the source, which is
        the forward law with the ends exchanged and the answer negated -- exactly
        the JFET's rule, and smooth across zero for the same reason (see
        Jfet.h). */
    inline void evaluateMosfetChannelEitherWay(const MosfetModel& model, double vgs, double vds,
                                               double& current, double& dIdVgs, double& dIdVds) noexcept
    {
        if (vds >= 0.0)
        {
            evaluateMosfetChannel(model, vgs, vds, current, dIdVgs, dIdVds);
            return;
        }

        double forward = 0.0, dFdA = 0.0, dFdB = 0.0;
        evaluateMosfetChannel(model, vgs - vds, -vds, forward, dFdA, dFdB);

        current = -forward;
        dIdVgs = -dFdA;
        dIdVds = dFdA + dFdB;
    }

    /** The channel current at the *terminals*, through the source resistance.

        The current I satisfies I = f(Vgs - I Rs, Vds - I Rs). The residual
        r(I) = I - f(...) rises with I at a slope of at least one (the channel
        only ever gains current with gate and drain drive), so there is exactly
        one root, and it lies between zero and the current without Rs. It is
        also concave there -- the channel is convex in its drive, and the
        triode term's far end does not move, since Rs shifts the gate and the
        drain alike -- so Newton approaches from below without overshooting,
        and from above overshoots once and then approaches from below.

        Warm, from `guess` -- linearise() passes the tangent prediction off the
        last answer -- it simply runs Newton, keeping whatever bracket the
        residuals' signs have shown so far; consecutive evaluations are close,
        so that is a step or two. Cold, or if a warm
        start has not settled within a few steps, it evaluates the channel
        without Rs to bracket the root between zero and that current, starts
        from the Newton step off zero, and bisects any step that would leave
        the bracket. Newton converges quadratically, so a correction of one part
        in a million leaves an error near one part in 10^12, and the loop
        stops there rather than evaluating again to confirm it.

        The slopes follow from differentiating the same equation:
        dI/dVgs = fg / (1 + Rs (fg + fd)), and likewise for Vds. */
    inline void evaluateMosfetTerminals(const MosfetModel& model, double vgs, double vds,
                                        double& current, double& dIdVgs, double& dIdVds,
                                        double guess = 0.0) noexcept
    {
        const double rs = model.sourceResistance;

        if (rs <= 0.0)
        {
            evaluateMosfetChannelEitherWay(model, vgs, vds, current, dIdVgs, dIdVds);
            return;
        }

        constexpr double infinity = std::numeric_limits<double>::infinity();
        double lo = -infinity, hi = infinity;
        double i = guess;
        double f = 0.0, fg = 0.0, fd = 0.0, slope = 1.0;

        const auto newton = [&](int maximumSteps)
        {
            for (int iteration = 0; iteration < maximumSteps; ++iteration)
            {
                evaluateMosfetChannelEitherWay(model, vgs - i * rs, vds - i * rs, f, fg, fd);

                const double residual = i - f;
                (residual < 0.0 ? lo : hi) = i;

                slope = 1.0 + rs * (fg + fd);
                const double step = residual / slope;
                const double next = i - step;

                if (std::abs(step) <= 1.0e-6 * std::abs(i) + 1.0e-18)
                {
                    i = next;
                    return true;
                }

                i = next > lo && next < hi ? next : 0.5 * (lo + hi);
            }

            return false;
        };

        bool settled = std::abs(guess) > 0.0 && newton(4);

        if (! settled)
        {
            double unloaded = 0.0, g0 = 0.0, d0 = 0.0;
            evaluateMosfetChannelEitherWay(model, vgs, vds, unloaded, g0, d0);

            if (! (std::abs(unloaded) > 0.0))
            {
                current = unloaded;
                dIdVgs = g0;
                dIdVds = d0;
                return;
            }

            lo = std::min(0.0, unloaded);
            hi = std::max(0.0, unloaded);
            i = unloaded / (1.0 + rs * (g0 + d0)); // Newton's first step off zero
            newton(60);
        }

        // The slopes are the last evaluation's; the current has moved by a
        // part in a million since, which they do not notice.
        current = i;
        dIdVgs = fg / slope;
        dIdVds = fd / slope;
    }

    inline void linearise(const Mosfet& m, const double* v, DeviceLinearisation& out) noexcept
    {
        const MosfetModel& model = m.model;
        const double p = model.polaritySign();
        const double vgs = p * v[0];
        const double vds = p * v[1];

        // The gate: an insulator. gmin is all that flows.
        out.current[0] = p * gmin * vgs;
        out.jacobian[0] = gmin;
        out.jacobian[1] = 0.0;

        double channel = 0.0, dIdVgs = 0.0, dIdVds = 0.0;

        const double predicted = m.channelCurrentLast
                               + m.slopeGateLast * (vgs - m.vgsLast)
                               + m.slopeDrainLast * (vds - m.vdsLast);

        evaluateMosfetTerminals(model, vgs, vds, channel, dIdVgs, dIdVds, predicted);

        m.channelCurrentLast = channel;
        m.vgsLast = vgs;
        m.vdsLast = vds;
        m.slopeGateLast = dIdVgs;
        m.slopeDrainLast = dIdVds;

        // The body diode, source to drain: forward-biased by a negative Vds.
        // Across the terminals, not the channel: it is its own junction, and
        // its own curve on the datasheet is taken between the pins.
        double body = 0.0, bodyConductance = 0.0;
        evaluateJunction(-vds, model.bodyDiodeSaturationCurrent, model.bodyDiodeScaleVoltage(),
                         body, bodyConductance);

        out.current[1] = p * (channel - body + gmin * vds);
        out.jacobian[2] = dIdVgs;                           // d(drain current) / d(Vgs)
        out.jacobian[3] = dIdVds + bodyConductance + gmin;  // d(drain current) / d(Vds)
    }
} // namespace CircuitComponents
