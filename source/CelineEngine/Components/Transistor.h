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

        Junction capacitance, the forward Early voltage and the forward
        high-injection knee are here too, all zero -- off -- unless a model
        carries a figure.

        Left out, all Gummel-Poon parameters with no home here:

          VAR         The reverse Early effect. Only visible in reverse-active
                      operation, which an audio stage barely visits.
          IKR         The reverse high-injection knee, for the same reason.
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

        /** Forward high-injection knee, IKF on a SPICE card. Zero means none.

            Past this collector current the base fills with carriers and the
            transport current stops following the exponential: Gummel-Poon
            divides it by qb = (1 + sqrt(1 + 4 If/IKF)) / 2, so gain turns over
            instead of climbing towards BF forever. Most cards put the knee far
            above anything a pedal draws -- 0.14 A on the BC547 family -- but
            the low-noise, high-gain parts have it low: 15 mA on the 2N5088 and
            2N5089, where it already trims the gain by 6% at 1 mA. Leaving it
            out of those cards reads their gain high everywhere a fuzz runs. */
        double forwardKneeCurrent = 0.0; // IKF, amps

        /** Two of this transistor in one package, the first one's emitter
            driving the second one's base, collectors common -- a Darlington.

            The card then describes each half, not the pair: addTransistor()
            wires two devices and the internal node between them, which is the
            only way the pair's Vbe comes out as two junctions' worth and its
            gain as the product of two gains that each sag at their own
            current. A single transistor with BF = 30000 gets neither. */
        bool darlington = false;

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

        //======================================================================
        // The BC107/108/109 family, and the BC547/548/549/550 that replaced it
        // in plastic.
        //
        // One die, sorted twice. The *number* is a sort for breakdown voltage
        // and noise -- the BC107 and BC547 for 45 V, the BC109 and BC549 for a
        // low noise figure -- and the engine models neither, so a BC108B and a
        // BC549B are the same part here. The *letter* is a sort for gain, which
        // the engine models exactly, so the grades are three different parts:
        //
        //     A   hFE 110-220 at 2 mA   typ 180,  90 at 10 uA
        //     B   hFE 200-450           typ 290, 150 at 10 uA
        //     C   hFE 420-800           typ 520, 270 at 10 uA
        //
        // (Philips's typicals, reprinted on the Comset BC107-109 sheet in
        // docs/specs; onsemi's BC546-550 sheet gives the same windows.)
        //
        // Each grade is Philips's own extracted card for that grade -- IS, NF,
        // the reverse terms, the high-injection knee and the Early voltage --
        // with BF, ISE and NE refitted. The refit is not optional: Philips's
        // cards keep hFE flat all the way down to 10 uA, where their datasheet
        // says it halves, and the low-current gain is exactly what the first
        // stage of a fuzz runs on. Through the engine's own equations each
        // grade now gives its typical at both 10 uA and 2 mA, exactly, with
        // Vbe 0.67 / 0.65 / 0.64 V at 2 mA against the sheet's 0.65 typ. The
        // grade shows in the Early voltage too: a thinner base buys the gain
        // and costs output resistance, 144 V on an A and 53 V on a C.
        //
        // Capacitances are the datasheet typicals, the same for every grade:
        // Cob 3.5 pF at 10 V and Cib 9 pF (onsemi), which the BC107's 4 pF
        // collector figure agrees with.

        /** BC108A: gain grade A. Also sold as BC107A, BC109A, BC547A, BC548A. */
        static BjtModel npnBC108A() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 9.677e-15,       // IS   Philips BC847A (the BC547A die)
                .forwardBeta = 201.07,                // BF   refitted
                .reverseBeta = 7.004,                 // BR
                .forwardEmission = 0.9922,            // NF
                .reverseEmission = 0.9935,            // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 3.8839e-13,     // ISE  refitted
                .baseEmitterLeakageEmission = 1.7137, // NE   refitted
                .baseCollectorLeakage = 5.236e-12,    // ISC
                .baseCollectorLeakageEmission = 1.53, // NC
                .capBaseEmitter = 9.0e-12,            // Cib typ
                .capBaseCollector = 3.5e-12,          // Cob typ
                .forwardEarlyVoltage = 143.8,         // VAF
                .forwardKneeCurrent = 0.14,           // IKF
            };
        }

        /** BC108B: gain grade B. Also sold as BC107B, BC109B, BC547B, BC548B,
            BC549B, BC550B. */
        static BjtModel npnBC108B() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 2.39e-14,        // IS   Philips BC547B
                .forwardBeta = 305.68,                // BF   refitted
                .reverseBeta = 7.946,                 // BR
                .forwardEmission = 1.008,             // NF
                .reverseEmission = 1.004,             // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 4.623e-13,      // ISE  refitted
                .baseEmitterLeakageEmission = 1.7684, // NE   refitted
                .baseCollectorLeakage = 6.272e-14,    // ISC
                .baseCollectorLeakageEmission = 1.243, // NC
                .capBaseEmitter = 9.0e-12,            // Cib typ
                .capBaseCollector = 3.5e-12,          // Cob typ
                .forwardEarlyVoltage = 63.2,          // VAF
                .forwardKneeCurrent = 0.1357,         // IKF
            };
        }

        /** BC109C: gain grade C. The Big Muff transistor, and the house NPN of
            most British pedal and preamp designs. Also sold as BC107C, BC108C,
            BC547C, BC548C, BC549C, BC550C.

            This used to be a fit of its own, to Vbe and the two hFE points
            alone, with no Early voltage, knee or input capacitance. It gave
            hFE 239 at 10 uA and 522 at 2 mA; it now gives the datasheet's 270
            and 520, from the same card structure as its two siblings. */
        static BjtModel npnBC109C() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 4.679e-14,       // IS   Philips BC547C
                .forwardBeta = 560.63,                // BF   refitted
                .reverseBeta = 11.57,                 // BR
                .forwardEmission = 1.01,              // NF
                .reverseEmission = 1.019,             // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 1.6442e-13,     // ISE  refitted
                .baseEmitterLeakageEmission = 1.6415, // NE   refitted
                .baseCollectorLeakage = 2.337e-14,    // ISC
                .baseCollectorLeakageEmission = 1.164, // NC
                .capBaseEmitter = 9.0e-12,            // Cib typ
                .capBaseCollector = 3.5e-12,          // Cob typ
                .forwardEarlyVoltage = 52.64,         // VAF
                .forwardKneeCurrent = 0.1371,         // IKF
            };
        }

        //======================================================================
        // Fairchild's process 07: the 2N5088, 2N5089 and MPSA18 are one
        // low-noise, high-gain die sorted by gain -- 2N5088 300-900, 2N5089
        // 400-1200, MPSA18 500-1500 at 100 uA -- which, unlike the BC family's
        // sorts, the engine can tell apart.

        /** 2N5088. The Fairchild datasheet's own SPICE card, verbatim for
            everything this model has a field for. Through the engine: hFE 487
            at 100 uA, 650 at 1 mA and 554 at 10 mA against the sheet's minima
            of 300, 350 and 300, with Vbe 0.74 V at 10 mA (0.8 max). The 15 mA
            knee is why the gain has already turned over by 10 mA. */
        static BjtModel npn2N5088() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 5.911e-15,       // IS
                .forwardBeta = 1122.0,                // BF
                .reverseBeta = 1.271,                 // BR
                .forwardEmission = 1.0,               // NF
                .reverseEmission = 1.0,               // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 5.911e-15,      // ISE
                .baseEmitterLeakageEmission = 1.394,  // NE
                .baseCollectorLeakage = 0.0,          // ISC
                .baseCollectorLeakageEmission = 2.0,  // NC
                .capBaseEmitter = 4.973e-12,          // CJE
                .capBaseCollector = 4.017e-12,        // CJC
                .forwardEarlyVoltage = 62.37,         // VAF
                .forwardKneeCurrent = 14.92e-3,       // IKF
            };
        }

        /** 2N5089, the higher-gain sort of the same die. Fairchild's card
            again: hFE 651 at 100 uA, 870 at 1 mA, 742 at 10 mA (sheet minima
            400, 450, 400). The Big Muff's later transistor. */
        static BjtModel npn2N5089() noexcept
        {
            auto m = npn2N5088();
            m.forwardBeta = 1434.0;               // BF
            m.reverseBeta = 1.262;                // BR
            m.baseEmitterLeakageEmission = 1.421; // NE
            m.forwardKneeCurrent = 15.4e-3;       // IKF
            return m;
        }

        /** MPSA18, the highest-gain sort. Fitted here to onsemi's typicals
            (docs/specs is Central's sheet, which only has minima): hFE 580 at
            10 uA, 850 at 100 uA, 1100 at 1 mA and 1150 at 10 mA, and Vbe 0.60 V
            at 1 mA -- all five reproduced exactly by the engine, the last point
            by the knee. Capacitances are onsemi's typicals too, Ccb 1.7 pF and
            Ceb 5.6 pF.

            Neither sheet gives an Early voltage or a reverse gain, so those two
            are process 07's, from the 2N5088 card: a stated borrowing, since
            onsemi's part is a different fab's. */
        static BjtModel npnMPSA18() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 7.8336e-14,      // IS
                .forwardBeta = 1522.9,                // BF
                .reverseBeta = 1.271,                 // BR   process 07
                .forwardEmission = 1.0,               // NF
                .reverseEmission = 1.0,               // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 2.2366e-14,     // ISE
                .baseEmitterLeakageEmission = 1.4167, // NE
                .baseCollectorLeakage = 0.0,          // ISC
                .baseCollectorLeakageEmission = 2.0,  // NC
                .capBaseEmitter = 5.6e-12,            // Ceb typ
                .capBaseCollector = 1.7e-12,          // Ccb typ
                .forwardEarlyVoltage = 62.37,         // VAF  process 07
                .forwardKneeCurrent = 0.056884,       // IKF
            };
        }

        /** MPSA13, an NPN Darlington: two transistors, the first one's
            emitter driving the second one's base. This card describes each
            half; addTransistor() builds the pair.

            onsemi publish one set of typical curves for the MPSA13 and its
            higher-gain sort the MPSA14, and the halves are fitted to those:
            the pair gives hFE 27k at 5 mA, 36k at 10 mA, 50k at 30 mA and 59k
            at 100 mA (sheet 27k, 37k, 49k, 59k) and Vbe 1.12 / 1.15 / 1.30 V
            at 5 / 10 / 100 mA (sheet 1.12, 1.15, 1.30). The MPSA13 is only
            guaranteed 5000 at 10 mA, so a real one can have far less gain
            than this typical -- lower BF to model a poor specimen.

            The reverse terms and the Early voltage are the process-05 card
            LTspice ships under the MPSA14's name; the capacitances are chosen
            so the pair presents onsemi's Cibo 10 pF and Cobo 7 pF at the few
            volts a pedal biases it to. Below 5 mA -- most of where a pedal
            runs one -- the curves stop, and this is extrapolation. */
        static BjtModel npnMPSA13() noexcept
        {
            return {
                .polarity = Polarity::NPN,
                .saturationCurrent = 1.1113e-13,      // IS
                .forwardBeta = 4071.9,                // BF
                .reverseBeta = 0.657,                 // BR   process 05
                .forwardEmission = 1.0,               // NF
                .reverseEmission = 1.0,               // NR
                .thermalVoltage = 0.025852,
                .baseEmitterLeakage = 2.9047e-13,     // ISE
                .baseEmitterLeakageEmission = 1.3967, // NE
                .baseCollectorLeakage = 9.0e-13,      // ISC  process 05
                .baseCollectorLeakageEmission = 2.0,  // NC
                .capBaseEmitter = 20.0e-12,           // per half
                .capBaseCollector = 4.0e-12,          // per half
                .forwardEarlyVoltage = 136.7,         // VAF  process 05
                .forwardKneeCurrent = 0.044779,       // IKF
                .darlington = true,
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

        double gBe = (is / (model.forwardBeta * vteF)) * ef + gmin;
        double gBc = (is / (model.reverseBeta * vteR)) * er + gmin;
        const double gIf = (is / vteF) * ef;  //  d(transport)/d(vBe)
        const double gIr = (is / vteR) * er;  // -d(transport)/d(vBc)

        // The scaled transport current's two slopes, shared by the Jacobian
        // below: d(iCt)/d(vBe) and -d(iCt)/d(vBc).
        double gIfE = gIf * earlyScale + transport * earlySlope;
        double gIrE = gIr * earlyScale + transport * earlySlope;

        double iCt = transport * earlyScale;

        // High injection: the transport current over qb, which depends on the
        // forward current alone (IKR is not modelled), so only the vBe slope
        // picks up a term of its own. With no knee qb is exactly 1 and all of
        // this is skipped.
        if (model.forwardKneeCurrent > 0.0)
        {
            const double q2 = is * (ef - 1.0) / model.forwardKneeCurrent;
            const double root = std::sqrt(std::max(1.0 + 4.0 * q2, 1.0e-12));
            const double qb = 0.5 * (1.0 + root);
            const double dQbdVbe = gIf / (model.forwardKneeCurrent * root);

            iCt /= qb;
            gIfE = gIfE / qb - iCt * dQbdVbe / qb;
            gIrE /= qb;
        }

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
