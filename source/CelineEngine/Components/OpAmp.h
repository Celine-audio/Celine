#pragma once

#include "Types.h"

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

        Not modelled: slew rate, input bias current, common-mode rejection,
        noise.
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

        double midpoint() const noexcept { return 0.5 * (positiveRail + negativeRail); }

        /** Transconductance of the input stage: gain divided by the gain node's
            impedance, so that the two multiply back to the open-loop gain. */
        double inputTransconductance() const noexcept
        {
            return openLoopGain / gainNodeResistance;
        }

        /** The capacitance that puts the dominant pole where the
            gain-bandwidth product says it should be. */
        double poleCapacitance() const noexcept
        {
            const double poleFrequency = gainBandwidth / openLoopGain;
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
            Output resistance is not on the sheet. Slew rate, 13 V/us, is not
            modelled, and at that speed rarely matters in a pedal. */
        static OpAmpModel tl072() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 3.0e6;
            m.inputResistance = 1.0e12;
            m.railHeadroom = 1.5;
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

            Slew rate is 2.2 V/us typ, which this model does not represent. On a
            part this slow that is not nothing: it rounds fast transients in a
            way the frequency response alone doesn't capture. */
        static OpAmpModel jrc4558() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 2.8e6;
            m.inputResistance = 2.0e6;
            m.outputResistance = 75.0;
            m.railHeadroom = 1.0;
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
            ones, so those are what is used here. */
        static OpAmpModel ne5532() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 1.0e5;
            m.gainBandwidth = 12.0e6;
            m.inputResistance = 300.0e3;
            m.outputResistance = 50.0; // not in SLOS075K -- see above
            m.railHeadroom = 1.5;      // not in SLOS075K -- see above
            return m;
        }

        /** LM308, as used in the ProCo Rat. Slow, and the slowness is the
            sound: a 1 MHz gain-bandwidth takes the top off the distortion. */
        static OpAmpModel lm308() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 3.0e5;
            m.gainBandwidth = 1.0e6;
            m.outputResistance = 100.0;
            m.railHeadroom = 1.2;
            return m;
        }

        /** LM741 -- the 1968 general-purpose op-amp, and the original MXR
            Distortion+'s. From TI's SNOSC25D, LM741 typical column, +/-15 V:

                large-signal voltage gain   200 V/mV       -> 2e5
                bandwidth                   0.35 / 0.3 us rise time -> 1.2 MHz
                input resistance            2 MOhm
                output swing                +/-14 V into 10k -> 1 V headroom

            Output resistance is not on TI's sheet; 75 Ohm is the figure the
            original Fairchild uA741 sheet gives.

            Two things this does not model, and on a 741 both are audible. Its
            slew rate is 0.5 V/us -- a 7 V swing takes 14 us -- which rounds the
            edges of anything it clips; the bandwidth here only rolls off small
            signals. And it is specified from +/-10 V: on a 9 V pedal its
            inputs lose their common-mode range long before the output reaches
            the headroom above, which a real one turns into early, gritty
            clipping. The gain-bandwidth product -- a gain of 1000 is flat only
            to about a kilohertz -- is the part of its sound this does get. */
        static OpAmpModel lm741() noexcept
        {
            OpAmpModel m;
            m.openLoopGain = 2.0e5;
            m.gainBandwidth = 1.2e6;
            m.inputResistance = 2.0e6;
            m.outputResistance = 75.0;
            m.railHeadroom = 1.0;
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
        ComponentId gainStage = -1; // input transconductance
        ComponentId gainNodeResistor = -1;
        ComponentId poleCapacitor = -1;
        ComponentId outputStage = -1; // buffered output transconductance
        ComponentId positiveClamp = -1;
        ComponentId negativeClamp = -1;
    };
} // namespace CircuitComponents
