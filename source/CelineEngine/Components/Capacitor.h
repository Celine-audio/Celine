#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A capacitor between two nodes, optionally polarised (an electrolytic).

        Discretised with the trapezoidal rule, which turns it into a fixed
        conductance in parallel with a current source that depends on the
        previous sample -- the standard SPICE companion model:

            i = conductance * v - conductance * vEquivalent

        where vEquivalent = vCapPrev + halfStepResistance * iPrev. With no ESR
        that reduces to the familiar i = (2C/dt)*v - ((2C/dt)*vPrev + iPrev).
        Circuit maintains everything here except `farads`, `esrOhms` and
        `polarised`.

        Polarity is a constraint on how you wire it, not different physics, so
        `polarised` does not change the maths -- it tells Circuit to check the
        operating point and complain if the part is backwards, which is a real
        build mistake that a schematic makes easy to commit and hard to spot.

        ESR does change the sound. A film cap's is milliohms; an electrolytic's
        is ohms, which is not ignorable when it bypasses a resistance of the same
        order -- an emitter bypass across a fuzz pot, where the ESR alone decides
        how complete the bypass can be. It is folded into the companion model
        analytically, without costing a node.

        Reverse conduction is deliberately not modelled: it would make every
        circuit containing one nonlinear, and it describes a fault rather than a
        sound. Circuit reports the reversal instead.
    */
    struct Capacitor
    {
        NodeIndex a, b; // for a polarised part, `a` is the positive terminal
        double farads;

        /** Equivalent series resistance. Zero for film and ceramic; roughly
            0.1 to 10 ohms for an electrolytic, depending on value, voltage
            rating, temperature and age. Worth taking from a datasheet if the
            part is doing a bypass job. */
        double esrOhms = 0.0;

        /** Whether Circuit should check this part isn't reverse-biased at the
            DC operating point. */
        bool polarised = false;

        //======================================================================
        // Companion-model state, all maintained by Circuit.

        double conductance = 0.0;         // 1 / (esrOhms + halfStepResistance)
        double halfStepResistance = 0.0;  // dt/2C -- the capacitance alone, over one step
        double vCapPrev = 0.0;            // volts across the capacitance itself, excluding any ESR drop
        double iPrev = 0.0;               // amps through the branch
    };
} // namespace CircuitComponents
