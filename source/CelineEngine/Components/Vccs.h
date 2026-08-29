#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A voltage-controlled current source -- SPICE's G element.

            i(from -> to) = transconductance * (V(controlPositive) - V(controlNegative))

        Completely linear, so it lives in the cached matrix alongside the
        resistors and never touches the Newton iteration. That is why it exists:
        an amplifier is a controlled source, and modelling one as a single
        nonlinear device gives the solver a strongly asymmetric Jacobian block
        with an enormous off-diagonal term, which the DK reduction handles
        badly. Built from a controlled source instead, the gain is linear and
        the only nonlinearity left is whatever limits the output. See OpAmp.h.

        The stamp is asymmetric: current is *drawn* from `from` and *delivered*
        to `to`. Wire `from` to ground for a source that pushes current into
        `to` without loading anything, which is how a buffered output is built.
    */
    struct Vccs
    {
        NodeIndex from, to;                         // the current path
        NodeIndex controlPositive, controlNegative; // the voltage that sets it
        double transconductance;                    // amps per volt
    };
} // namespace CircuitComponents
