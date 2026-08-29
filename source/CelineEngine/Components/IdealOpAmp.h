#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        An ideal op-amp -- a nullor.

        It does exactly two things: it forces its inputs to the same voltage, and
        it supplies whatever output current that takes. No gain figure, no
        bandwidth, no rails, no saturation. In feedback it gives precisely the
        textbook answer, because it *is* the textbook assumption.

        Compare OpAmp.h, which is a macro model built out of primitives. The two
        are for different jobs:

            IdealOpAmp    one row in the matrix and nothing else. No internal
                          nodes, no clamp diodes, so it adds no nonlinear ports
                          at all and costs almost nothing. Use it when the op-amp
                          is not the interesting part of the circuit -- a buffer,
                          a filter, a summing stage -- or when you want to see
                          what a topology does without the op-amp's own limits
                          confusing the picture.

            OpAmp         finite gain, a real dominant pole, and rails it clips
                          against. Costs internal nodes and two nonlinear ports.
                          Use it when the op-amp's limits are the point, which in
                          a guitar pedal they usually are -- a ProCo Rat's dirt is
                          the op-amp clipping.

        Every other component answers "what current flows, given the voltages?",
        which is what a row of G*V = I asks. A nullor answers neither: its input
        current is zero whatever the voltage, and its output current is whatever
        the circuit demands. So that current becomes an unknown of its own and
        the new row states the constraint replacing it:

            V(in+) - V(in-) = 0

        the same trick a voltage source uses, except the constraint is on one
        pair of nodes and the current appears at a different one.

        The output current is not returned anywhere -- a real op-amp draws it
        from supplies that are not modelled here, which is why this device can
        say nothing about supply current or rail sag.
    */
    struct IdealOpAmp
    {
        NodeIndex inPlus, inMinus, output;

        /** The solved output current, amps, flowing out of `output` into the
            circuit. Written by Circuit every sample. */
        double current = 0.0;
    };
} // namespace CircuitComponents
