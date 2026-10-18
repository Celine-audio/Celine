#pragma once

#include "Types.h"

#include <cmath>

namespace CircuitComponents
{
    //==========================================================================
    /**
        Op-amp parameters.

        The op-amp is not a device in this engine. It is a small subcircuit,
        assembled by Circuit::addOpAmp() out of parts that already exist:

            in+ o--+--[Rin]--+--o in-
                   |         |
                   +--( gm )-+        gm = openLoopGain / gainNodeResistance
                       |
                       v  current into the gain node
                  +----+------+------+
                  |    |      |      |
              [Rgain] [Cpole] |>|   |<|    clamps to just inside the rails
                  |    |      |      |
                 gnd  gnd    vHi    vLo
                       |
                     ( gm )           gm = 1 / outputResistance
                       |
                       v  current into out, drawing nothing back

        Everything there is linear except the two clamp diodes, and that is the
        entire point.

        The obvious model -- a single nonlinear element saturating on the
        differential input -- hands the solver a Jacobian block with an
        off-diagonal term of gain over output resistance, of order a thousand.
        The DK reduction cannot cope: on a clipping op-amp it walks the output
        off to thousands of volts, where the full-matrix solve of the identical
        circuit sits correctly between the rails. Assembled from primitives the
        gain is an ordinary linear controlled source, stamped once into the
        cached matrix, and the only nonlinearity left is two plain diodes.

        The capacitor on the gain node gives a real single-pole rolloff, so
        open-loop gain falls at 6 dB/octave above gainBandwidth/openLoopGain and
        datasheet numbers can go straight in.

        A model with a `slewRate` swaps the input source for a long-tailed
        pair (DifferentialPair.h) whose tail current bounds how fast the
        capacitor can be charged -- the Boyle macromodel's input stage, and the
        one place the op-amp's own nonlinearity is not just its clamps.

        Not modelled: input bias current, common-mode rejection, noise.
    */
    struct OpAmpModel
    {
        /** Open-loop gain at DC, straight off the datasheet. */
        double openLoopGain = 1.0e5;

        /** Gain-bandwidth product, Hz. With the gain above, this puts the
            dominant pole at gainBandwidth/openLoopGain -- a few hertz, as on a
            real part. */
        double gainBandwidth = 3.0e6;

        /** Differential input resistance, ohms. */
        double inputResistance = 2.0e6;

        /** Output resistance, ohms. */
        double outputResistance = 75.0;

        /** Supply rails, volts. A pedal on a single 9 V battery uses 0 and 9,
            and biases its inputs to the midpoint. */
        double positiveRail = 9.0;
        double negativeRail = 0.0;

        /** How far short of each rail the output stops. A TL072 gives up about
            1.5 V either side; a rail-to-rail part manages a tenth of that. */
        double railHeadroom = 1.5;

        /** Impedance of the internal gain node. Not a physical quantity -- it
            and the transconductance only ever appear as a product, which is the
            open-loop gain. It exists so the pole capacitor lands on a sensible
            value. */
        double gainNodeResistance = 1000.0;

        /** Slew rate, V/s: the fastest the output can move, however hard the
            input is driven. Zero means unlimited -- a linear input stage, as
            every model was before this existed. */
        double slewRate = 0.0;

        /** The input pair's emitter degeneration, 0 to just under 1 -- see
            DifferentialPair. Zero is a bare pair. Matters only to a part that
            slews, and only to how its edge begins. */
        double inputDegeneration = 0.0;

        /** Phase margin, degrees: how far short of 180 the open loop is where
            its gain crosses unity. Zero means none given -- the single-pole
            response every model had before, which is a margin of 90.

            Set, it places a second pole at gainBandwidth / tan(90 - margin),
            and -- the part that matters at audio frequencies -- raises the
            dominant pole so the gain still crosses unity at gainBandwidth. A
            datasheet's unity-gain bandwidth is where the gain *crosses*; with a
            second pole that crossing sits below the gain-bandwidth the audio
            band actually sees, by the factor in lowFrequencyGainBandwidth().
            Treating the two as one, as the single-pole model did, left a
            TL072's closed-loop response 5% low and 4 degrees late at 20 kHz in
            a gain of 101 against TI's own macromodel.

            Datasheets do not quote it directly. Each model says how its figure
            was got: from the vendor's SPICE card where there is one, measured in
            ngspice, or from the sheet's quoted follower overshoot. */
        double phaseMargin = 0.0;

        /** Impedance of the second pole's node. Like gainNodeResistance, not a
            physical quantity: only its product with the capacitor matters. */
        double secondPoleResistance = 1000.0;

        bool hasSecondPole() const noexcept { return phaseMargin > 0.0 && phaseMargin < 90.0; }

        /** Where the second pole sits, Hz -- zero when there is none. */
        double secondPoleFrequency() const noexcept
        {
            if (! hasSecondPole())
                return 0.0;

            return gainBandwidth / std::tan((90.0 - phaseMargin) * 3.14159265358979323846 / 180.0);
        }

        /** The gain-bandwidth the band below the second pole sees: the
            crossing, raised by what the second pole takes off there. Equal to
            gainBandwidth when there is no second pole. */
        double lowFrequencyGainBandwidth() const noexcept
        {
            if (! hasSecondPole())
                return gainBandwidth;

            const double ratio = gainBandwidth / secondPoleFrequency();
            return gainBandwidth * std::sqrt(1.0 + ratio * ratio);
        }

        double secondPoleCapacitance() const noexcept
        {
            return 1.0 / (2.0 * 3.14159265358979323846 * secondPoleResistance * secondPoleFrequency());
        }

        double midpoint() const noexcept { return 0.5 * (positiveRail + negativeRail); }

        /** The input pair's tail: the most current it can steer into the
            compensation capacitor, which is what makes the slew rate the slew
            rate. */
        double tailCurrent() const noexcept { return slewRate * poleCapacitance(); }

        /** How far the output can swing, rail clamp to rail clamp. */
        double outputSwing() const noexcept { return positiveRail - negativeRail - 2.0 * railHeadroom; }

        /** Whether this part can be slew-limited at all when the circuit runs
            at `sampleRate`.

            A sampled output moves at most its whole swing in one sample, so the
            fastest it can ever be asked to move is swing x rate. A part whose
            slew rate is above that can always deliver -- its tail current
            carries the output across the whole swing inside a sample -- and the
            limit never binds. On a 9 V pedal at 48 kHz that is about a third
            of a volt per microsecond -- 0.32 for the LM308's swing, 0.34 for
            the LM741's -- so the 308 slews and the 741 cannot; at 4x
            oversampling the 741 can again, and at 8x the JRC4558. Used to give such a part the linear input stage instead,
            which costs no Newton ports. */
        bool slewsAt(double sampleRate) const noexcept
        {
            return slewRate > 0.0 && slewRate < outputSwing() * sampleRate;
        }

        /** Transconductance of the input stage: gain divided by the gain node's
            impedance, so that the two multiply back to the open-loop gain. */
        double inputTransconductance() const noexcept
        {
            return openLoopGain / gainNodeResistance;
        }

        /** The capacitance that puts the dominant pole where the
            gain-bandwidth product says it should be -- the low-frequency one,
            so the gain crosses unity at gainBandwidth with the second pole in
            place. */
        double poleCapacitance() const noexcept
        {
            const double poleFrequency = lowFrequencyGainBandwidth() / openLoopGain;
            return 1.0 / (2.0 * 3.14159265358979323846 * gainNodeResistance * poleFrequency);
        }

        //======================================================================
        // Models.

        /** TL071 / TL072 -- the JFET-input part in a large fraction of all
            pedals. One datasheet covers the single, the dual and the quad (TI
            SLOS080), and one amplifier of a TL072 *is* a TL071, so they are one
            model. Typical column, +/-15 V:

                large-signal voltage gain   200 V/mV       -> 2e5
                unity-gain bandwidth        3 MHz
                input resistance            1e12 Ohm       -- JFET inputs
                output swing                +/-13.5 V into 10k -> 1.5 V headroom

            The gain and the input resistance used to be the generic defaults
            (1e5 and 2 MOhm); an input resistance a million times too low
            loaded any high-impedance bias network wired across the inputs.
            Output resistance is not on the sheet.

            Slew rate 13 V/us. At that speed it never slews on audio -- a 48 kHz
            sample cannot ask more than about 0.8 V/us of it -- and a follower
            hit with a step proves it: the output takes the step at the
            bandwidth's pace, not the tail current's. It is here so a sheet
            that swaps a TL072 for a slow part compares like for like. */
        static OpAmpModel tl072() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 3.0e6;
            m.inputResistance = 1.0e12;
            m.railHeadroom = 1.5;
            m.slewRate = 13.0e6;

            // 64 degrees: TI's own TL072 macromodel, run open-loop in
            // ngspice-47, crosses unity at 3.07 MHz with 115.5 degrees of lag.
            // The sheet's quoted follower overshoot -- 20% into 100 pF -- puts
            // it at 62 once that load is counted, so the two agree. Against
            // that card on a gain of 101 it takes 20 kHz from 8.9% rms out to
            // 3.2%, and 10 kHz from 4.2% to 1.0%: the crossing is the sheet's
            // 3 MHz, and the band below it sees 3.3.
            m.phaseMargin = 64.0;
            return m;
        }

        /** JRC4558 -- the Tube Screamer op-amp, and the part the pedal's
            mythology is built around.

            Every figure below is from the JRC datasheet (Vcc = +/-15 V,
            Ta = 25 C), typical column:

                large-signal voltage gain   200 V/mV      -> 2e5
                unity-gain bandwidth        2.8 MHz
                input resistance            2 MOhm
                output resistance           75 Ohm
                output swing                +/-14 V on +/-15 -> 1 V of headroom

            Slew rate 2.2 V/us typ (1.2 min). Slow enough to round a fast edge,
            fast enough that in a Tube Screamer -- where the clipping diodes in
            the feedback loop keep the output swing to a volt or so -- it barely
            slews at all. */
        static OpAmpModel jrc4558() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 2.8e6;
            m.inputResistance = 2.0e6;
            m.outputResistance = 75.0;
            m.railHeadroom = 1.0;
            m.slewRate = 2.2e6;

            // 60 degrees, from the sheet's 15% follower overshoot with its
            // own 75 ohms driving the 100 pF test load. No vendor card here to
            // check it against.
            m.phaseMargin = 60.0;
            return m;
        }

        /** NE5532 -- the low-noise studio workhorse. Far quicker and quieter
            than a 4558, and audibly cleaner in the same socket.

            From TI's SLOS075K (December 2025), typical column:

                large-signal voltage gain   100 V/mV      -> 1e5
                unity-gain bandwidth        12 MHz
                input resistance            300 kOhm      -- bipolar inputs, so
                                                             far lower than the
                                                             JFET-input parts

            Two figures below are *not* from that datasheet, because revision K
            deleted them -- its own revision history lists "Output impedance" and
            "Maximum peak-to-peak output voltage swing" among the parameters
            removed. The output resistance and headroom here are therefore
            reasonable values for a bipolar output stage of this type rather than
            specified ones. They matter least of all the parameters in a pedal,
            where the load is tens of kilohms and diodes clamp long before the
            rails do, but they are guesses and should be read as such.

            Note also that the datasheet contradicts itself: sections 6.3.1 and
            6.3.3 still say 10 MHz and 9 V/us where the tables say 12 MHz and
            5 V/us. The revision history confirms the tables are the updated
            ones, so those are what is used here -- slew rate included. The copy
            in docs/specs is the older revision H (May 2004), whose 10 MHz and
            9 V/us are the figures K later revised; at audio rates the part
            slews at neither. */
        static OpAmpModel ne5532() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 1.0e5;
            m.gainBandwidth = 12.0e6;
            m.inputResistance = 300.0e3;
            m.slewRate = 5.0e6;

            // 73 degrees, from revision H's 10% follower overshoot into
            // 600 ohms and 100 pF. Assumes 50 ohms of open-loop output
            // resistance -- the same guess as above, since neither revision
            // gives it -- and that the margin carries from H's 10 MHz to K's
            // 12. At these frequencies it only matters to a loop with a
            // gain near one.
            m.phaseMargin = 73.0;
            m.outputResistance = 50.0; // not in SLOS075K -- see above
            m.railHeadroom = 1.5;      // not in SLOS075K -- see above
            return m;
        }

        /** LM308, as used in the ProCo Rat. Slow, and the slowness is the
            sound: a 1 MHz gain-bandwidth takes the top off the distortion, and
            a 0.3 V/us slew rate rounds what is left.

            The 308 has no internal compensation: its bandwidth and slew rate
            are both set by the capacitor across pins 1 and 8, and fall as 1/Cf
            (TI LM108-N sheet, docs/specs). These are its figures at the
            standard 30 pF the Rat uses -- unity gain at 1 MHz on the
            open-loop plot, and 0.3 V/us read off the voltage-follower pulse
            response, which the large-signal response agrees with (full swing
            gone by 3 kHz). At the Rat's gain of 1000 the input stage runs out
            of current at 48 mV across its inputs, so a hard-driven Rat slews
            on every cycle. */
        static OpAmpModel lm308() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 3.0e5;
            m.gainBandwidth = 1.0e6;
            m.outputResistance = 100.0;
            m.railHeadroom = 1.2;
            m.slewRate = 0.3e6;

            // About 65 degrees, read off the 30 pF phase curve of the
            // open-loop plot, a scanned image (TI LM108-N Figure 15): 110-120
            // degrees of lag where the gain crosses unity. Coarse -- half a
            // division either way is five degrees -- and immaterial in a Rat,
            // whose loop crosses at a kilohertz.
            m.phaseMargin = 65.0;
            return m;
        }

        /** LM741 -- the 1968 general-purpose op-amp, and the original MXR
            Distortion+'s. From TI's SNOSC25D, LM741 typical column, +/-15 V:

                large-signal voltage gain   200 V/mV       -> 2e5
                input resistance            2 MOhm
                output swing                +/-14 V into 10k -> 1 V headroom

            Its bandwidth, phase margin and input stage are TI's uA741 Boyle
            macromodel's, measured in ngspice-47: unity gain crossed at
            0.885 MHz with 63 degrees of margin, and the input pair's emitter
            resistors (1.836k on 15.16 uA, against 2Vt) making it 35%
            degenerated. The sheet agrees within what it says: its 5% follower
            overshoot is 65 degrees, and its 0.3 us rise time puts the
            crossing at 0.73 MHz -- quoted to one figure, and 0.25 us would be
            0.87. This model used to read that rise time as 0.35 / 0.3 us =
            1.2 MHz, which is the closed-loop bandwidth of a follower rather
            than where the open loop crosses unity: it left the small-signal
            response 8-11% too high at 10-20 kHz in a gain of 101. Against the
            card on that stage at 10 V out, where it slews, it now lands within
            0.1-1.2% rms from 1 to 10 kHz and 4.0% at 20 kHz.

            Output resistance is not on TI's sheet; 75 Ohm is the figure the
            original Fairchild uA741 sheet gives.

            Slew rate 0.5 V/us -- a 7 V swing takes 14 us -- which rounds the
            edges of anything it clips; the bandwidth only rolls off small
            signals. The gain-bandwidth product -- a gain of 1000 is flat only
            to about a kilohertz -- is the other half of its sound.

            It is specified from +/-10 V, and on a 9 V pedal its inputs lose
            their common-mode range before the output reaches the headroom
            above. That is not modelled: the sheet gives the range but not what
            the part does past it -- see LIMITATIONS.md. */
        static OpAmpModel lm741() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 0.885e6;
            m.inputResistance = 2.0e6;
            m.outputResistance = 75.0;
            m.railHeadroom = 1.0;
            m.slewRate = 0.5e6;
            m.phaseMargin = 63.0;
            m.inputDegeneration = 0.35;
            return m;
        }

        /** The same part on a split supply, as bench and studio gear uses. */
        OpAmpModel withRails(double positive, double negative) const noexcept
        {
            OpAmpModel m = *this;
            m.positiveRail = positive;
            m.negativeRail = negative;
            return m;
        }
    };

    //==========================================================================
    /**
        Handles to the parts Circuit::addOpAmp() created, so they can be reached
        afterwards -- most usefully the clamp diodes, if you want to change how
        the output gives up as it approaches the rails.
    */
    struct OpAmp
    {
        ComponentId inputResistor = -1;
        ComponentId gainStage = -1; // input transconductance, when it is linear
        ComponentId inputPair = -1; // or the long-tailed pair, when it slews
        ComponentId gainNodeResistor = -1;
        ComponentId poleCapacitor = -1;
        ComponentId outputStage = -1; // buffered output transconductance
        ComponentId secondPoleStage = -1, secondPoleResistor = -1, secondPoleCapacitor = -1;
        ComponentId positiveClamp = -1;
        ComponentId negativeClamp = -1;
    };
} // namespace CircuitComponents
