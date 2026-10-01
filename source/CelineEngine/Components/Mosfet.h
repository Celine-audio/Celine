#pragma once

#include "Junction.h"
#include "Ports.h"

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

        Not modelled: the drain and source series resistances (an ohm or two,
        against a pedal's kilohms), mobility degradation (it matters at amps),
        the voltage dependence of the capacitances, and the gate resistance.
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
        // Threshold is the parameter with the widest spread -- 0.8 to 3 V on
        // the same part number for all three below, which is why MOSFET boosts
        // carry a bias trimmer. Each model is the datasheet's typical part, fit
        // to its gate voltage at 1 mA (the Vgs(th) test) and to its typical
        // output curves, so it biases where a typical one does. Fitted with n
        // taken as 1.5: no datasheet publishes the subthreshold slope, and 1.5
        // is where vertical DMOS parts of this size sit.

        /** 2N7000, onsemi (Fairchild). Typical Vgs(th) 2.1 V at 1 mA, which the
            model reproduces; the on-region curves (Id at Vgs 3, 4, 5, 6 V) to
            within 18%, and gfs at 200 mA 267 mS against 320 typ. Lambda from
            the 4 V curve's slope. Capacitances read off the typical curves at
            a drain a few volts above the gate: Crss is 12 pF there against 4 pF
            at the 25 V the table quotes, Ciss 33. */
        static MosfetModel n2N7000() noexcept
        {
            MosfetModel m;
            m.channel = Channel::N;
            m.threshold = 2.012;
            m.transconductance = 0.1617;
            m.lambda = 0.01;
            m.bodyDiodeSaturationCurrent = 1.4e-13; // Fairchild's own model
            m.capGateSource = 21.0e-12;
            m.capGateDrain = 12.0e-12;
            return m;
        }

        /** BS170, onsemi (Motorola). Not a 2N7000 in another package: about a
            third of its transconductance parameter -- typical gfs 200 mmhos and
            rDS(on) 1.8 ohm where the 2N7000 has 320 and 1.2 -- so it needs a
            larger swing on the gate for the same drain current. Typical Vgs(th) 2.0 V at 1 mA,
            reproduced; the output curves at Vgs 4 to 8 V within 22%. Its
            threshold comes out at 1.82 V, which Zetex's independent card for
            the part also has. Capacitances off the typical curves as above:
            Crss 13 pF, Ciss 39 near a few volts. */
        static MosfetModel nBS170() noexcept
        {
            MosfetModel m;
            m.channel = Channel::N;
            m.threshold = 1.820;
            m.transconductance = 0.0562;
            m.lambda = 0.005;
            m.bodyDiodeSaturationCurrent = 5.0e-12; // Zetex's model of the part
            m.capGateSource = 26.0e-12;
            m.capGateDrain = 13.0e-12;
            return m;
        }

        /** BS250, P-channel, Diodes Inc (Zetex) -- the complement to the two
            above. Its sheet gives only a threshold range (-1 to -3.5 V) and a
            typical gfs, 150 mS at -200 mA; the threshold comes from Philips's
            model of the part (1 mA at -2.53 V, mid-range), and the fit hits
            both exactly. Lambda, capacitances and body diode are from Zetex's
            model for the ZVP2106A, which the BS250P's sheet refers to for its
            curves. Wire it as you would the N-channel parts -- the model flips
            the signs -- with the source at the positive rail. */
        static MosfetModel pBS250() noexcept
        {
            MosfetModel m;
            m.channel = Channel::P;
            m.threshold = 2.340;
            m.transconductance = 0.05022;
            m.lambda = 0.012;
            m.bodyDiodeSaturationCurrent = 2.0e-13;
            m.capGateSource = 47.0e-12;
            m.capGateDrain = 10.0e-12;
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

        // The channel, symmetric in its two ends: for Vds < 0 the drain is the
        // source, which is the forward law with the ends exchanged and the
        // answer negated -- exactly the JFET's rule, and smooth across zero for
        // the same reason (see Jfet.h).
        double channel = 0.0, dIdVgs = 0.0, dIdVds = 0.0;

        if (vds >= 0.0)
        {
            evaluateMosfetChannel(model, vgs, vds, channel, dIdVgs, dIdVds);
        }
        else
        {
            double forward = 0.0, dFdA = 0.0, dFdB = 0.0;
            evaluateMosfetChannel(model, vgs - vds, -vds, forward, dFdA, dFdB);

            channel = -forward;
            dIdVgs = -dFdA;
            dIdVds = dFdA + dFdB;
        }

        // The body diode, source to drain: forward-biased by a negative Vds.
        double body = 0.0, bodyConductance = 0.0;
        evaluateJunction(-vds, model.bodyDiodeSaturationCurrent, model.bodyDiodeScaleVoltage(),
                         body, bodyConductance);

        out.current[1] = p * (channel - body + gmin * vds);
        out.jacobian[2] = dIdVgs;                           // d(drain current) / d(Vgs)
        out.jacobian[3] = dIdVds + bodyConductance + gmin;  // d(drain current) / d(Vds)
    }
} // namespace CircuitComponents
