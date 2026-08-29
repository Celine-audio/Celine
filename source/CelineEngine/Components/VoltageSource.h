#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        An ideal voltage source between two nodes: a battery, a regulated
        supply rail, a bias reference.

        The one component that does not fit the nodal formulation. Everything
        else answers "what current flows given the voltages?", which is what a
        row of G·V = I asks; a source fixes a voltage and lets the circuit decide
        the current, so there is no conductance to stamp.

        The standard fix, and where "modified" nodal analysis gets its name, is
        to make the branch current an unknown of its own. The system grows by a
        row and a column per source: the column carries the current into the two
        node equations, and the row states the constraint,

            V(positive) - V(negative) = volts

        which is why Circuit's matrix is bigger than its node count once sources
        are involved, and why `current` below is handed back rather than set.

        A grounded supply could instead have its node declared known and
        eliminated, as ground and the input are. Not done here: the saving is one
        row, and it would not generalise to a source floating between two nodes.
    */
    struct VoltageSource
    {
        NodeIndex positive, negative;

        /** The DC part, in volts. */
        double volts;

        /** An optional sinusoid added on top -- a mains transformer secondary,
            which is the only thing a rectifier has to work with. Amplitude is a
            peak value, not RMS.

            Circuit advances `phase` itself, once per sample. */
        double acAmplitude = 0.0;
        double acFrequency = 0.0;
        double phase = 0.0;

        /** The solved branch current, in amps, flowing into the positive
            terminal -- the same sign convention SPICE reports for a source, so
            a supply powering a circuit shows a negative current. Written by
            Circuit on every sample; useful for metering current draw. */
        double current = 0.0;
    };
} // namespace CircuitComponents
