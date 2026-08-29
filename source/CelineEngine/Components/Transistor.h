#pragma once

#include "Junction.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        Bipolar junction transistor parameters (Ebers-Moll).

        Ebers-Moll treats the transistor as what it is: two p-n junctions
        sharing a base, plus a transport current carrying most of the emitter's
        injection through to the collector. Hence the four numbers below -- Is
        scaling both junctions, and forward and reverse current gains.

        Essentially the level-1 SPICE model, plus the base leakage terms
        (ISE/NE, ISC/NC) a Gummel-Poon card carries. Those are cheap -- they add
        to the *base* current only, leaving the Jacobian structure alone -- and
        omitting them badly misreads an extracted card, since BF there is the
        ideal-region gain that leakage pulls down. On the 2N2222A card BF says
        930 where the real gain at 1 mA is nearer 105, entirely from ISE.

        Junction capacitance and the forward Early voltage are here too, both
        zero -- off -- unless a model carries a figure.

        Left out, all Gummel-Poon parameters with no home here:

          VAR         The reverse Early effect. Only visible in reverse-active
                      operation, which an audio stage barely visits.
          IKF / IKR   High-injection roll-off. Without it, current gain keeps
                      climbing with collector current instead of turning over
                      past the knee -- on the 2N2222A card that knee is 19.5 mA,
                      and by 60 mA the gain here reads roughly double reality.
                      A pedal stage sits near 1 mA, well below it, so this only
                      bites if you model something that draws real current.
          RB/RE/RC    Parasitic terminal resistances, milliohms to an ohm here.
                      Ignorable next to any real circuit resistor.
          VJE/MJE     Bias dependence of the junction capacitances. A real one
          VJC/MJC     falls off as its junction reverse-biases; what is wired
                      here is a constant, the card's zero-bias or typical
                      figure. Right at the bias a small-signal stage sits at,
                      wrong by a factor of two at the extremes.
          TF / TR     Transit times, so fT does not degrade with current.
          XTB/XTI/EG  Temperature coefficients. Everything here is fixed at
                      roughly 300 K via thermalVoltage.
          KF / AF     Flicker noise, not modelled at all.
    */
    struct BjtModel
    {
        enum class Polarity
        {
            NPN,
            PNP
        };

        Polarity polarity = Polarity::NPN;
        double saturationCurrent = 6.734e-15; // Is, amps
        double forwardBeta = 416.4;           // BF, forward current gain
        double reverseBeta = 0.7371;          // BR, reverse current gain
        double forwardEmission = 1.0;         // NF, base-emitter emission coefficient
        double reverseEmission = 1.0;         // NR, base-collector emission coefficient
        double thermalVoltage = 0.025852;     // Vt, volts (kT/q at ~300 K)

        /** Base-emitter and base-collector leakage (recombination) currents and
            their emission coefficients -- ISE/NE and ISC/NC on a SPICE card.

            These are what make the current gain fall off at low collector
            current instead of staying flat at BF. Leave the currents at zero
            and the device is plain Ebers-Moll, which is what the generic models
            below use. */
        double baseEmitterLeakage = 0.0;           // ISE, amps
        double baseEmitterLeakageEmission = 1.5;   // NE
        double baseCollectorLeakage = 0.0;         // ISC, amps
        double baseCollectorLeakageEmission = 2.0; // NC

        /** Junction capacitances, farads -- CJE and CJC on a SPICE card.

            Alone these put a pole above 1 MHz and you would never know. Miller
            is what makes them audible: base-collector is multiplied by the
            stage's gain, so a 4 pF CJC on a gain-of-100 stage presents about
            400 pF to whatever drives the base -- against a 100k source, a
            lowpass at 4 kHz.

            Zero means "not modelled", the default. addTransistor() wires
            non-zero ones as ordinary capacitors between terminals the transistor
            already has, so the Miller multiplication emerges from the nodal
            solve rather than from a formula here. */
        double capBaseEmitter = 0.0;   // CJE
        double capBaseCollector = 0.0; // CJC

        /** Forward Early voltage, VAF on a SPICE card. Zero means none.

            The collector voltage reaching back through the base width and
            modulating the transport current, which picks up a factor
            (1 + Vce/VAF) -- exactly an output resistance ro = VAF/Ic across the
            collector load. At 1 mA with a 74 V part that is 75k against a 4.7k
            load, so the gain drops about 6%: the difference between what the
            resistor ratio computes and what the stage actually does. */
        double forwardEarlyVoltage = 0.0; // VAF, volts

        /** +1 for NPN, -1 for PNP. Every equation below is written for an NPN;
            a PNP is the same device with every voltage and current negated. */
        double polaritySign() const noexcept { return polarity == Polarity::NPN ? 1.0 : -1.0; }

        double forwardScaleVoltage() const noexcept { return forwardEmission * thermalVoltage; }
        double reverseScaleVoltage() const noexcept { return reverseEmission * thermalVoltage; }

        //======================================================================
        // Models

        /** 2N3904 -- the default small-signal silicon NPN. Big Muff, Rat,
            countless boost and overdrive stages. Turns on around 0.65 V.

            The DC parameters are Fairchild's datasheet SPICE card verbatim
            (Is 6.734f, BF 416.4, BR 0.7371), and the capacitances and Early
            voltage come off the same card: CJE 4.493 pF, CJC 3.638 pF,
            VAF 74.03. */
        static BjtModel npnSilicon() noexcept
        {
            BjtModel m {Polarity::NPN, 6.734e-15, 416.4, 0.7371, 1.0, 1.0, 0.025852};
            m.capBaseEmitter = 4.493e-12;   // CJE
            m.capBaseCollector = 3.638e-12; // CJC
            m.forwardEarlyVoltage = 74.03;  // VAF
            return m;
        }

        /** 2N3906 -- the PNP complement of the 2N3904. Same provenance as the
            NPN: the National card whose Is 1.41f, BF 180.7, BR 4.977 the DC
            parameters already were, which also carries CJE 8.063 pF,
            CJC 9.728 pF and VAF 18.7. A markedly lower VAF than the NPN, so a
            PNP stage loses noticeably more gain to it. */
        static BjtModel pnpSilicon() noexcept
        {
            BjtModel m {Polarity::PNP, 1.41e-15, 180.7, 4.977, 1.0, 1.0, 0.025852};
            m.capBaseEmitter = 8.063e-12;   // CJE
            m.capBaseCollector = 9.728e-12; // CJC
            m.forwardEarlyVoltage = 18.7;   // VAF
            return m;
        }

        /** 2N2222A, from a Symmetry MODPEX-extracted SPICE3 card.

            Note BF is 930 while the gain you'll actually measure around 1 mA is
            nearer 105 -- on an extracted card BF is the ideal-region figure and
            ISE pulls it down everywhere a pedal actually operates. Both numbers
            are below and the model reconciles them; don't "fix" BF to look more
            like a datasheet hFE or you'll get the gain wrong twice over. */
        static BjtModel npn2N2222A() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 3.88184e-14,      // IS
                .forwardBeta = 929.846,                // BF
                .reverseBeta = 48.4545,                // BR
                .forwardEmission = 1.10496,            // NF
                .reverseEmission = 1.07004,            // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 1.0168e-11,      // ISE
                .baseEmitterLeakageEmission = 1.94752, // NE
                .baseCollectorLeakage = 1.0168e-11,    // ISC
                .baseCollectorLeakageEmission = 4.0,   // NC
                // Cibo / Cobo from the ON Semi P2N2222A sheet. It publishes
                // maxima only (at VEB 0.5 V / VCB 10 V), so these read high
                // against a typical part -- the right side to err on for a
                // capacitance whose whole effect is to take top end away.
                .capBaseEmitter = 25.0e-12,            // Cibo, 25 pF max
                .capBaseCollector = 8.0e-12,           // Cobo, 8 pF max
                // VAF: not on the datasheet and not verified from the card
                // family this DC set came from, so off rather than guessed.
            };
        }

        /** 2N5133. Low-noise, high-gain NPN in a TO-106 can -- an early Fairchild
            planar part, second-sourced by NJ Semi, whose sheet this is fitted to.

            The interesting thing about it is how hard its gain falls away at low
            current, which the sheet states outright by grading hFE at two points
            twenty times apart:

                hFE  220 typ  at Ic 1.0 mA,  Vce 5 V   (60 min, 1000 max)
                hFE   50 typ  at Ic  50 uA,  Vce 10 V
                Vbe(on)          0.75 V max
                NF   1.5 dB typ at 1 kHz
                BVceo 18 V min

            Two points is what ISE and NE exist to fit: a 4.4x drop over 20x of
            current is steeper than NE = 1.5 can produce at all, and forcing it
            there gives a *negative* BF. The constraint puts NE above about 1.98,
            so 2.5 is the low end of what the data allows. BF then falls out as
            the no-recombination asymptote; 678 sits just under the sheet's 1000
            maximum, which is the right side of a device-to-device spread.

            Through the engine's own equations this gives hFE 220 at 1 mA and 50
            at 50 uA, both exact, with Vbe 0.696 V at 1 mA.

            Two caveats. No high-current roll-off (no IKF), so gain climbs
            towards BF instead of turning over -- 378 at 5 mA, already hot for a
            0.5 W part. And BR is not on the sheet; 4.0 is a small-signal
            assumption, so hard collector-junction conduction is unverified. */
        static BjtModel npn2N5133() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 2.0e-15,        // IS, set by Vbe at 1 mA
                .forwardBeta = 678.0,                // BF, the ideal-region figure
                .reverseBeta = 4.0,                  // BR, assumed -- not on the sheet
                .forwardEmission = 1.0,              // NF
                .reverseEmission = 1.0,              // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 6.42e-11,      // ISE, from the two hFE points
                .baseEmitterLeakageEmission = 2.5,   // NE
                .baseCollectorLeakage = 1.0e-13,     // ISC
                .baseCollectorLeakageEmission = 2.0, // NC
                // Ccb 5 pF max on the NJ Semi sheet (docs/specs/2N5133.pdf). Its
                // input capacitance is not published at all, so CJE stays off
                // rather than guessed -- half the Miller pair beats an invented
                // number, and it is the half that is multiplied by the gain.
                .capBaseCollector = 5.0e-12,
            };
        }

        /** BC109C -- low-noise silicon NPN, the high-gain C group of the
            BC107/108/109 family. The Big Muff transistor, and the house NPN of
            most British pedal and preamp designs.

            The C suffix is the whole point of the part: hFE is graded 420-800
            where the plain BC109 is 200-450, so a circuit designed around one
            of these is leaning on gain the ungraded part doesn't have.

            Provenance differs from npn2N2222A(), and it matters which you're
            using. That one is a vendor-extracted MODPEX card, precise to six
            figures because it was fitted to a measured part. These parameters
            are fitted here to the published datasheet figures instead -- Vbe
            and the hFE grading. Measured back out of the solver, that gives
            hFE 499 at 1.2 mA and 560 at 5.4 mA, so about 520 at the 2 mA the
            datasheet grades at, sitting mid-band in its 420-800 window, with
            Vbe 0.63 V there. Gain falls to 406 at 200 uA and 297 at 29 uA, the
            low-current sag a real one has and a plain Ebers-Moll model can't
            reproduce at all.

            That makes them good for designing a bias network around and honest
            about the low-current roll-off, but they're a fit to a spec sheet
            rather than a measurement of silicon. Drop in an extracted card if
            you find one; nothing else has to change. */
        static BjtModel npnBC109C() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 5.0e-14,        // IS
                .forwardBeta = 690.0,                // BF, the ideal-region figure
                .reverseBeta = 5.0,                  // BR
                .forwardEmission = 1.0,              // NF
                .reverseEmission = 1.0,              // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 8.0e-14,       // ISE
                .baseEmitterLeakageEmission = 1.5,   // NE
                .baseCollectorLeakage = 1.0e-13,     // ISC
                .baseCollectorLeakageEmission = 2.0, // NC
                // Cc 2.5 pF typ on the datasheet; input capacitance
                // unpublished, so CJE stays off as on the 2N5133.
                .capBaseCollector = 2.5e-12,
            };
        }

        /** Germanium PNP of the AC128 / OC44 class -- the Fuzz Face and Rangemaster
            transistor. Turns on around 0.25 V and leaks far more than silicon,
            which is most of why germanium fuzz behaves the way it does.

            Treat this as a typical part, not a datasheet part. Real germanium
            transistors vary enormously unit to unit -- gains anywhere from 60 to
            150 on the same part number -- which is exactly why people hand-select
            them for fuzz pedals. Change forwardBeta to taste.

            The leakage that makes germanium germanium.

            A silicon transistor's collector-base leakage is picoamps and may as
            well not exist. A germanium one's is microamps -- four to six orders
            of magnitude more -- and at the fraction of a milliamp a fuzz pedal
            actually runs at, that leakage is a real share of the base current.
            It is why germanium circuits drift with temperature, why they need
            trimming, and why builders hand-select transistors at all.

            Without it these models were germanium in name and turn-on voltage
            only: 0.15 uA of leakage, which is a silicon figure.

            ICBO on a real AC128 spans about 1 uA on a good specimen to well over
            100 uA on a leaky one, against a datasheet maximum of 15. A pedal
            builder selects from the low end -- a leaky one will not bias -- so
            this is set near it. Raise it to model a worse specimen; it is the
            single parameter that most changes how a germanium circuit behaves.

            Note what this does to gain at low current: hFE measured at 1 mA
            stays near BF, but by 10 uA the leakage is comparable to the base
            current and apparent gain collapses. That is not a modelling
            artefact, it is the effect itself. */

        static BjtModel pnpGermanium() noexcept
        {
            return{
                .polarity = Polarity::PNP,
                .saturationCurrent = 1.0e-7,
                .forwardBeta = 100.0,
                .reverseBeta = 2.0,
                .forwardEmission = 1.0,
                .reverseEmission = 1.0,
                .thermalVoltage = 0.025852,
                .baseCollectorLeakage = 1.0e-6,  // ICBO comes out near 1.2 uA
                .baseCollectorLeakageEmission = 2.0
                // No capacitances: alloy-junction parts predate the practice
                // of publishing them, and unit-to-unit spread would make any
                // single figure a fiction.
            };
        }

        /** Germanium NPN of the AC127 class. Same caveats as pnpGermanium(). */
        static BjtModel npnGermanium() noexcept
        {
            return{
                .polarity = Polarity::NPN,
                .saturationCurrent = 1.0e-7,
                .forwardBeta = 100.0,
                .reverseBeta = 2.0,
                .forwardEmission = 1.0,
                .reverseEmission = 1.0,
                .thermalVoltage = 0.025852,
                .baseCollectorLeakage = 1.0e-6,  // ICBO comes out near 1.2 uA
                .baseCollectorLeakageEmission = 2.0
            };
        }
    };

    //==========================================================================
    /**
        A bipolar transistor. Terminal order everywhere below is base,
        collector, emitter.
    */
    struct Bjt
    {
        NodeIndex base, collector, emitter;
        BjtModel model;

        /** The junction voltages the last Newton iteration linearised around,
            in the model's own polarity. Carried between iterations (and between
            samples) so the voltage limiter has previous values to damp against. */
        double vBeLast = 0.0;
        double vBcLast = 0.0;

        /** criticalVoltage() for each junction, cached by Circuit when the model
            changes so the Newton loop doesn't repeat the logarithms every
            iteration. */
        double vCritBe = 0.0;
        double vCritBc = 0.0;
    };

    //==========================================================================
    // Port interface -- see Ports.h.
    //
    // The two ports are (base, emitter) and (base, collector), so the port
    // voltages are exactly the two junction voltages the device equations are
    // written in, and the port currents are the two junction currents. The
    // emitter's current never has to be stated: it falls out of the two ports
    // sharing the base node.

    constexpr int portCount(const Bjt&) noexcept { return 2; }

    inline void fillPorts(const Bjt& t, Port* ports) noexcept
    {
        ports[0] = {t.base, t.emitter};
        ports[1] = {t.base, t.collector};
    }

    /** Damps the Newton step across both junctions, and reports whether either
        needed it. Voltages come in and go out in real circuit orientation; the
        model's polarity is applied internally. */
    inline bool limitPortVoltages(Bjt& t, double* v) noexcept
    {
        const double p = t.model.polaritySign();

        const double vBe = limitJunctionVoltage(p * v[0], t.vBeLast, t.model.forwardScaleVoltage(), t.vCritBe);
        const double vBc = limitJunctionVoltage(p * v[1], t.vBcLast, t.model.reverseScaleVoltage(), t.vCritBc);

        // Exactness is the intent; written as magnitudes to keep -Wfloat-equal quiet.
        const bool acted = std::abs(vBe - p * v[0]) > 0.0 || std::abs(vBc - p * v[1]) > 0.0;

        t.vBeLast = vBe;
        t.vBcLast = vBc;
        v[0] = p * vBe;
        v[1] = p * vBc;
        return acted;
    }

    /**
        Linearises the transistor about the given port voltages.

        Port voltages arrive in real circuit orientation and currents leave the
        same way, so a PNP and an NPN are interchangeable from outside. The
        Jacobian comes out identical for both: the chain rule picks up the
        polarity twice on the way through and the two cancel.
    */
    inline void linearise(const Bjt& t, const double* v, DeviceLinearisation& out) noexcept
    {
        const BjtModel& model = t.model;
        const double p = model.polaritySign();
        const double vBe = p * v[0];
        const double vBc = p * v[1];

        const double vteF = model.forwardScaleVoltage();
        const double vteR = model.reverseScaleVoltage();
        const double is = model.saturationCurrent;

        const double ef = fastOrExactExp(std::min(vBe / vteF, maxExponent));
        const double er = fastOrExactExp(std::min(vBc / vteR, maxExponent));

        // The two junction currents -- what leaks out of the base -- and the
        // transport current, which is the part that actually makes it across.
        double iBe = (is / model.forwardBeta) * (ef - 1.0) + gmin * vBe;
        double iBc = (is / model.reverseBeta) * (er - 1.0) + gmin * vBc;

        // Early effect: the collector voltage modulates the base width, and
        // with it the transport current, which picks up the level-1 SPICE
        // factor 1 + Vce/VAF. Vce here is vBe - vBc, so the scale's slope is
        // +1/VAF in vBe and -1/VAF in vBc. Zero VAF means the idealised model,
        // and the terms below reduce exactly to what they were.
        //
        // The scale is floored clear of zero: nothing physical reaches there
        // (it wants Vce = -VAF, tens of volts into reverse), but the clamp
        // keeps a pathological Newton guess stamping a negative current rather
        // than a finite one. The slope goes to zero with it, so the Jacobian
        // below stays the exact derivative of what is actually stamped.
        const double transport = is * (ef - er); // before the Early scale

        double earlyScale = 1.0;
        double earlySlope = 0.0; // d(earlyScale)/d(vBe)

        if (model.forwardEarlyVoltage > 0.0)
        {
            const double raw = 1.0 + (vBe - vBc) / model.forwardEarlyVoltage;
            earlyScale = std::max(raw, 1.0e-3);
            earlySlope = raw > 1.0e-3 ? 1.0 / model.forwardEarlyVoltage : 0.0;
        }

        const double iCt = transport * earlyScale;

        double gBe = (is / (model.forwardBeta * vteF)) * ef + gmin;
        double gBc = (is / (model.reverseBeta * vteR)) * er + gmin;
        const double gIf = (is / vteF) * ef;  //  d(transport)/d(vBe)
        const double gIr = (is / vteR) * er;  // -d(transport)/d(vBc)

        // The scaled transport current's two slopes, shared by the Jacobian
        // below: d(iCt)/d(vBe) and -d(iCt)/d(vBc).
        const double gIfE = gIf * earlyScale + transport * earlySlope;
        const double gIrE = gIr * earlyScale + transport * earlySlope;

        // Recombination leakage. It adds to the base current only -- the
        // transport current, and so everything the collector does, is untouched.
        // This is what makes current gain sag at low collector current rather
        // than sitting flat at BF.
        if (model.baseEmitterLeakage > 0.0)
        {
            const double vteE = model.baseEmitterLeakageEmission * model.thermalVoltage;
            const double e = fastOrExactExp(std::min(vBe / vteE, maxExponent));

            iBe += model.baseEmitterLeakage * (e - 1.0);
            gBe += model.baseEmitterLeakage * e / vteE;
        }

        if (model.baseCollectorLeakage > 0.0)
        {
            const double vteC = model.baseCollectorLeakageEmission * model.thermalVoltage;
            const double e = fastOrExactExp(std::min(vBc / vteC, maxExponent));

            iBc += model.baseCollectorLeakage * (e - 1.0);
            gBc += model.baseCollectorLeakage * e / vteC;
        }

        // Port currents, base to emitter and base to collector. The transport
        // current leaves the base-emitter port and arrives at the base-collector
        // one, which is the whole of what a transistor does.
        out.current[0] = p * (iBe + iCt);
        out.current[1] = p * (iBc - iCt);

        out.jacobian[0] = gBe + gIfE; // d(i base-emitter) / d(v base-emitter)
        out.jacobian[1] = -gIrE;      // d(i base-emitter) / d(v base-collector)
        out.jacobian[2] = -gIfE;      // d(i base-collector) / d(v base-emitter)
        out.jacobian[3] = gBc + gIrE; // d(i base-collector) / d(v base-collector)
    }
} // namespace CircuitComponents
