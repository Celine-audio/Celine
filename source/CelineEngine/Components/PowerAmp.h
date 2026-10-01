#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        An LM386-style audio power amplifier.

        Not an op-amp, whatever the triangle suggests, and wiring one as an
        op-amp gets it wrong: its feedback is *inside* the package. The
        datasheet's equivalent schematic is what this is built from:

                 Vs --[15k]--+--[15k]-- EA --[150]-- pin 8 --[1.35k]-- pin 1 --[15k]-- out
                          (bypass)       |                               |
                                       Q2 (PNP)                        Q3 (PNP)
                   in- --> follower --> base                base <-- follower <-- in+
                   (50k to ground)                                 (50k to ground)

        Q2 and Q3 are a differential pair whose emitters are held a couple of
        Vbe above their inputs; a current mirror compares their currents and a
        high-gain stage drives the output until the two are equal. The
        resistors do the rest. With the inputs at rest, equal currents put the
        output at (Vs + 2 Vbe) / 2 -- the "self-centering" output, a little
        above half the supply -- and a signal sees

            gain = 2 * 15k / (150 + 1.35k) = 20      pins 1 and 8 open
            gain = 2 * 15k / 150           = 200     a capacitor from 1 to 8

        or anything between with a resistor in series with that capacitor,
        which is what every "gain" knob on an LM386 amp is. Pins 1 and 8 are
        brought out for exactly that. From the + input it is one more -- 21 and
        201 -- because the output also has to carry that input's own swing
        through the 15k; the datasheet's 26 dB curve sits at 26.4, which is 21.

        Circuit::addPowerAmp() builds it from primitives, as addOpAmp() does and
        for the same reason: the resistor network is literal, the emitters are
        held by ideal followers (plus 2 Vbe), each pair transistor's current is
        read across its emitter resistance, and the difference drives a gain
        node rolled off by one capacitor. Everything is linear except the two
        diodes that clamp that node short of the rails.

        Two figures here are fitted to the datasheet rather than read off it,
        and they are the whole of its frequency response: the effective emitter
        resistance and the loop's crossover. With them, a 6 V part is -3 dB at
        60 kHz at gain 200 and 260 kHz at gain 20 -- the datasheet's curves say
        60 and 300 -- and within 0.5 dB at 20 kHz either way. A second pole
        would buy the last 40 kHz of the gain-20 curve, all of it ultrasonic.

        Not modelled: the output stage's crossover distortion and current limit
        (it drives 8 ohms fine but a 4 ohm load swings wider than the
        datasheet's 3.5 V), the bypass pin (the supply here is ideal, so there
        is no ripple for it to reject), input bias current, noise.
    */
    struct PowerAmpModel
    {
        /** Supply, volts -- the one rail; ground is the other. */
        double supply = 9.0;

        //======================================================================
        // The resistor network, from the datasheet's equivalent schematic.

        double inputResistance = 50.0e3;    // each input to ground
        double biasResistance = 15.0e3;     // each half of the 30k from Vs to EA
        double emitterResistance = 150.0;   // EA to pin 8
        double gainResistance = 1.35e3;     // pin 8 to pin 1
        double feedbackResistance = 15.0e3; // pin 1 to the output

        /** How far each pair emitter sits above its input: the input
            follower's Vbe and the pair transistor's. It is what puts the
            output above half the supply at rest. */
        double emitterOffset = 1.1;

        /** The effective resistance in each pair emitter, as a multiple of the
            bare transistor's Vt / I at the quiescent current the bias network
            sets. Fitted: 2.75, which is about the transistor's own plus the
            starved input follower's reflected through the pair's gain -- the
            input follower carries only a base current, so its own emitter
            resistance is large. This is what makes the gain-200 wiring slower
            than the gain-20 one, as the datasheet shows. */
        double emitterResistanceScale = 2.75;

        double thermalVoltage = 0.025852;

        //======================================================================
        // The loop.

        /** DC transimpedance of the mirror and gain stage, ohms: volts at the
            gain node per amp of pair-current difference. Large enough that the
            resistors alone set the gain, to 0.1%. */
        double transimpedance = 1.5e8;

        /** Where the loop gain would cross unity with no emitter resistance to
            share the feedback current, Hz. Fitted with the scale above. */
        double crossoverFrequency = 0.41e6;

        /** Impedance of the internal gain node. It and the current gain only
            appear as their product, the transimpedance, in anything linear --
            but not at the clamps. The current gain is what reaches them once
            the loop has let go, and a real mirror can only hand the gain stage
            about its pair's own current. Put the impedance low, as the op-amp
            does, and a hard-driven output pushes amps into a clamp diode and
            rides a third of a volt past where the swing should stop; here the
            current gain is 1.5, so an overdriven pair delivers milliamps and
            the output stops where the datasheet's swing says. */
        double gainNodeResistance = 1.0e8;

        //======================================================================
        // The output stage.

        /** How far short of each rail the output stops, unloaded. The datasheet
            gives 7.8 V peak to peak on 9 V and 4.9 on 6 V, so 1.1 to 1.2 V lost
            altogether; it does not say how that divides, so evenly. Since the
            output rests above mid-supply, it still clips the top first. */
        double headroomHigh = 0.6;
        double headroomLow = 0.6;

        /** What the swing loses into a load once the output is clipping: the
            datasheet's 8 ohm swing (6.0 V peak to peak on 9 V) against its
            unloaded one. Inside the loop the output impedance is this over the
            loop gain -- a few milliohms. */
        double outputResistance = 2.4;

        double midpoint() const noexcept { return 0.5 * supply; }

        /** The pair transistors' quiescent current: the bias network's current
            with the emitters at emitterOffset. */
        double quiescentCurrent() const noexcept
        {
            return (supply - emitterOffset) / (2.0 * biasResistance);
        }

        double effectiveEmitterResistance() const noexcept
        {
            const double current = quiescentCurrent();
            return current > 0.0 ? emitterResistanceScale * thermalVoltage / current : 1.0e3;
        }

        /** The gain node's current per volt across an emitter resistance. */
        double inputTransconductance() const noexcept
        {
            return transimpedance / (gainNodeResistance * effectiveEmitterResistance());
        }

        /** Puts the loop's crossover where crossoverFrequency says: the loop
            gain is transimpedance / feedbackResistance at DC, rolled off by one
            pole. */
        double poleCapacitance() const noexcept
        {
            const double dcLoopGain = transimpedance / feedbackResistance;
            const double poleFrequency = crossoverFrequency / dcLoopGain;
            return 1.0 / (2.0 * 3.14159265358979323846 * gainNodeResistance * poleFrequency);
        }

        //======================================================================
        // Models.

        /** LM386 (National / TI SNAS545). The -1 and -3 are one die sorted by
            output power, and the -4 the high-voltage version; the model is the
            same for all, and the supply is whatever the part is drawn with. */
        static PowerAmpModel lm386() noexcept { return {}; }

        PowerAmpModel withSupply(double volts) const noexcept
        {
            PowerAmpModel m = *this;
            m.supply = volts;
            return m;
        }
    };

    //==========================================================================
    /** Handles to what Circuit::addPowerAmp() created. */
    struct PowerAmp
    {
        ComponentId positiveClamp = -1;
        ComponentId negativeClamp = -1;
        ComponentId poleCapacitor = -1;
    };
} // namespace CircuitComponents
