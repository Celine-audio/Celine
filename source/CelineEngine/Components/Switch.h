#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A switch: electrically a resistor that takes one of two values.

        There is no such thing as a switch in nodal analysis: an open circuit
        has no place in a conductance matrix and a perfect short is an infinite
        conductance. So a switch is a resistor of about ten milliohms closed and
        a gigaohm open, which is what a real one is anyway -- both far enough
        outside anything else in a guitar circuit to be indistinguishable from a
        short and an open.

        Being an ordinary resistor costs nothing extra: the circuit stays linear
        and the factorisation is still cached. Flipping one dirties the matrix
        exactly like turning a knob, which is cheap once per block and wasteful
        once per sample -- so drive these from a toggle, not audio.

        Wire one in series to break a connection or in parallel to short
        something out. Expect a click; real switches click too. */
    struct Switch
    {
        ComponentId resistor = -1;

        /** Contact resistance when closed. Ten milliohms is a real switch; the
            only reason not to make it smaller is that a very large conductance
            next to gmin stretches the matrix's dynamic range for no benefit. */
        double closedOhms = 0.01;

        /** Leakage when open. A gigaohm is far higher than any real circuit
            impedance, so it reads as an open, while staying well above gmin so
            the node is still tied to something. */
        double openOhms = 1.0e9;

        /** Implemented in Switch.cpp. */
        void setClosed(Circuit& circuit, bool closed) const;
    };

    //==========================================================================
    /**
        A changeover (SPDT) switch: a common terminal connected to one of two
        throws, never both.

        This is the shape of most guitar-circuit switching -- selecting between
        clipping diodes, picking a tone-stack voicing, choosing a rectifier.
        It's two Switches kept in opposition, which is worth having as a type
        purely so the invariant can't be broken by accident.
    */
    struct Changeover
    {
        Switch throwA;
        Switch throwB;

        /** Implemented in Switch.cpp. */
        void select(Circuit& circuit, bool useThrowB) const;
    };
} // namespace CircuitComponents
