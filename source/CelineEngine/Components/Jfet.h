#pragma once

#include "Junction.h"
#include "Ports.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        Junction FET parameters (Shichman-Hodges, the level-1 SPICE model).

        A JFET is a channel whose width is squeezed by a reverse-biased gate.
        Two things follow, and both are why pedal builders like them.

        The transfer curve is a square law rather than an exponential, so it
        distorts far more gently than a bipolar and produces mostly second
        harmonic -- which is why JFET stages get called "tube-like". They really
        do behave more like a triode than a transistor does.

        And the gate is a reverse-biased junction, so it draws essentially no
        current -- until you drive it positive, at which point it conducts like
        a diode and the stage starts blocking exactly the way a valve grid does.

            cutoff        Vgs < Vto            Id = 0
            triode        0 < Vds < Vgs - Vto  Id = Beta*(1+L*Vds)*Vds*(2*(Vgs-Vto) - Vds)
            saturation    Vds >= Vgs - Vto     Id = Beta*(1+L*Vds)*(Vgs-Vto)^2

        Vto is negative for an N-channel part: the device is *on* at zero gate
        voltage and you pinch it off by going negative. That's depletion mode,
        and it's the main practical difference from a MOSFET.
    */
    struct JfetModel
    {
        enum class Channel
        {
            N,
            P
        };

        Channel channel = Channel::N;
        double threshold = -0.7;   // Vto, volts -- negative for N-channel
        double beta = 1.2e-3;      // transconductance parameter, A/V^2
        double lambda = 0.02;      // channel-length modulation, 1/V
        double gatePerveance = 0.0; // filled in by the presets; see below

        /** Saturation current of the gate junction, for when the gate is driven
            positive and starts conducting. Same Shockley form as a diode. */
        double gateSaturationCurrent = 1.0e-14;
        double gateEmission = 1.0;
        double thermalVoltage = 0.025852;

        /** +1 for N-channel, -1 for P. As with the BJT, a P-channel part is the
            same device with every voltage and current negated. */
        double polaritySign() const noexcept { return channel == Channel::N ? 1.0 : -1.0; }

        double gateScaleVoltage() const noexcept { return gateEmission * thermalVoltage; }

        //======================================================================
        // Models.
        //
        // JFET parameters scatter worse than almost any other part -- Idss on a
        // J201 is specified as 0.2 to 1 mA, a five to one spread on the same
        // part number, which is why pedal builders measure and sort them. Treat
        // these as one plausible specimen each, and expect to adjust `threshold`
        // and `beta` if you're chasing a particular build.

        /** J201. Very low pinch-off, which lets it run from a 9 V rail with a
            small drain resistor -- the reason it turns up in so many pedal
            preamps and "tube emulation" stages. */
        static JfetModel j201() noexcept
        {
            JfetModel m;
            m.channel = Channel::N;
            m.threshold = -0.7;
            m.beta = 1.2e-3;
            m.lambda = 0.02;
            return m;
        }

        /** 2N5457. The general-purpose small-signal N-channel JFET. */
        static JfetModel n2n5457() noexcept
        {
            JfetModel m;
            m.channel = Channel::N;
            m.threshold = -1.5;
            m.beta = 1.3e-3;
            m.lambda = 0.02;
            return m;
        }

        /** 2N5952. The Phase 90's JFET -- four of them, one per all-pass stage,
            run in the triode region as voltage-controlled resistors with the
            LFO on their gates. Also a Small Stone and a Ross compressor part.

            Fitted to the Fairchild sheet (Rev A1, Nov 2002), which gives no
            typicals -- only limits -- so this is the specimen at the middle of
            each range, checked for self-consistency against the third:

                VGS(off)  -1.3 .. -3.5 V    taken as -2.4
                IDSS       4.0 .. 8.0 mA    taken as  6.0, at VDS 15 V
                gfs        2.0 .. 6.5 mA/V  falls out as 5.0, inside the range
                gos             75 umho     the only figure given, so lambda
                                            comes from it: gos/IDSS

            That gfs lands inside its own published band without being fitted to
            is the check that the other two were not picked at odds with each
            other -- the square law ties all three together as gfs = 2*IDSS/|Vto|,
            so a midpoint pair that produced an out-of-range gfs would mean no
            real device sits where this one is being placed.

            As with every JFET here: the spread on the real part is most of a
            factor of three on IDSS, which is why phaser builders match them. */
        static JfetModel n2n5952() noexcept
        {
            JfetModel m;
            m.channel = Channel::N;
            m.threshold = -2.4;
            m.beta = 8.772e-4;
            m.lambda = 0.0125;
            return m;
        }

        /** 2N5485. Higher pinch-off again, used where more headroom is wanted. */
        static JfetModel n2n5485() noexcept
        {
            JfetModel m;
            m.channel = Channel::N;
            m.threshold = -2.0;
            m.beta = 1.5e-3;
            m.lambda = 0.02;
            return m;
        }

        /** 2N5460, P-channel. Wire it as you would the N-channel part -- the
            model flips the signs -- but remember the supply goes the other way. */
        static JfetModel p2n5460() noexcept
        {
            JfetModel m;
            m.channel = Channel::P;
            m.threshold = -1.5; // magnitude; the polarity sign handles direction
            m.beta = 1.0e-3;
            m.lambda = 0.02;
            return m;
        }
    };

    //==========================================================================
    /**
        A JFET. Ports are (gate, source) and (drain, source), so the port
        voltages are Vgs and Vds -- exactly what the model is written in.
    */
    struct Jfet
    {
        NodeIndex drain, gate, source;
        JfetModel model;

        double vGateLast = 0.0;
        double vDrainLast = 0.0;
        double vCritGate = 0.0; // cached by Circuit
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const Jfet&) noexcept { return 2; }

    inline void fillPorts(const Jfet& j, Port* ports) noexcept
    {
        ports[0] = {j.gate, j.source};
        ports[1] = {j.drain, j.source};
    }

    inline bool limitPortVoltages(Jfet& j, double* v) noexcept
    {
        const double p = j.model.polaritySign();

        // The gate is a p-n junction, so it gets the junction limiter. The drain
        // is a square law, which is mild enough for a plain step clamp.
        const double gate = limitJunctionVoltage(p * v[0], j.vGateLast, j.model.gateScaleVoltage(), j.vCritGate);
        const double drain = std::clamp(p * v[1], j.vDrainLast - 20.0, j.vDrainLast + 20.0);

        const bool acted = std::abs(gate - p * v[0]) > 0.0 || std::abs(drain - p * v[1]) > 0.0;

        j.vGateLast = gate;
        j.vDrainLast = drain;
        v[0] = p * gate;
        v[1] = p * drain;
        return acted;
    }

    /** The channel's *forward* characteristic -- the drain current for a
        non-negative Vds, with its two partial derivatives.

        Split out because the reverse case is this same function with the two
        ends of the channel exchanged; see linearise() below. */
    inline void evaluateJfetChannel(const JfetModel& model, double vgs, double vds,
                                    double& current, double& dIdVgs, double& dIdVds) noexcept
    {
        const double overdrive = vgs - model.threshold;

        if (overdrive <= 0.0)
        {
            // Pinched off: the gate has squeezed the channel shut.
            current = 0.0;
            dIdVgs = 0.0;
            dIdVds = 0.0;
            return;
        }

        const double modulation = 1.0 + model.lambda * vds;

        if (vds < overdrive)
        {
            // Triode region: the channel behaves as a voltage-controlled resistor,
            // which is the other reason JFETs turn up in pedals -- as the
            // variable element in a compressor or a phaser.
            const double core = vds * (2.0 * overdrive - vds);

            current = model.beta * modulation * core;
            dIdVgs = model.beta * modulation * 2.0 * vds;
            dIdVds = model.beta * (modulation * 2.0 * (overdrive - vds) + model.lambda * core);
        }
        else
        {
            // Saturation: the square law everyone quotes.
            const double core = overdrive * overdrive;

            current = model.beta * modulation * core;
            dIdVgs = model.beta * modulation * 2.0 * overdrive;
            dIdVds = model.beta * model.lambda * core;
        }
    }

    inline void linearise(const Jfet& j, const double* v, DeviceLinearisation& out) noexcept
    {
        const JfetModel& model = j.model;
        const double p = model.polaritySign();
        const double vgs = p * v[0];
        const double vds = p * v[1];

        // Gate junction. Off in normal operation, and the thing that makes the
        // stage block when it isn't.
        double gateCurrent = 0.0;
        double gateConductance = 0.0;
        evaluateJunction(vgs, model.gateSaturationCurrent, model.gateScaleVoltage(),
                         gateCurrent, gateConductance);

        out.current[0] = p * gateCurrent;
        out.jacobian[0] = gateConductance;
        out.jacobian[1] = 0.0;

        // The channel, which is *symmetric*. A JFET has no built-in drain and
        // source: it is one channel with a gate over it, and whichever end sits
        // lower is the source. So negative Vds is not an off state, it is the
        // same device with its two ends exchanged.
        //
        // Treating Vds < 0 as "carries nothing" -- which is what this did -- made
        // the part a half-wave rectifier, and did it worst in exactly the circuit
        // the triode-region comment above advertises: a phaser or a compressor
        // sits the channel at Vds near zero and swings the signal through it,
        // so half of every cycle was thrown away.
        //
        // SPICE's rule, and the one used here: for Vds < 0 evaluate the forward
        // law with the terminals swapped and negate the answer.
        //
        //     a = Vgs - Vds = Vgd    the gate against what is now the source
        //     b = -Vds               a positive drain-source drop again
        //     Id = -f(a, b)
        //
        // The derivatives follow by the chain rule through those two:
        //
        //     dId/dVgs = -f_a
        //     dId/dVds = -(f_a * da/dVds + f_b * db/dVds) = f_a + f_b
        //
        // which is continuous *and* smooth across zero -- at Vds = 0 both sides
        // give a current of zero and a slope of 2*beta*overdrive, so Newton sees
        // no kink where the signal spends most of its time.
        //
        // Note this does not make the two ends interchangeable in every respect:
        // the gate junction above is modelled against the source only, so a
        // reverse-biased channel with a forward-biased *gate-drain* junction is
        // still an approximation. That is the pre-existing simplification noted
        // in LIMITATIONS.md, not something this changes.
        double channelCurrent = 0.0;
        double dIdVgs = 0.0;
        double dIdVds = 0.0;

        if (vds >= 0.0)
        {
            evaluateJfetChannel(model, vgs, vds, channelCurrent, dIdVgs, dIdVds);
        }
        else
        {
            double forward = 0.0, dFdA = 0.0, dFdB = 0.0;
            evaluateJfetChannel(model, vgs - vds, -vds, forward, dFdA, dFdB);

            channelCurrent = -forward;
            dIdVgs = -dFdA;
            dIdVds = dFdA + dFdB;
        }

        out.current[1] = p * (channelCurrent + gmin * vds);
        out.jacobian[2] = dIdVgs;         // d(drain current) / d(Vgs)
        out.jacobian[3] = dIdVds + gmin;  // d(drain current) / d(Vds)
    }
} // namespace CircuitComponents
