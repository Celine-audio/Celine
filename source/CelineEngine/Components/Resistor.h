#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A resistor between two nodes.

        The only component with no state and no model: it contributes the same
        conductance 1/R to the matrix regardless of voltage, current or history,
        which is why a circuit made only of these (and capacitors, and
        inductors) can be factorised once and reused every sample.
    */
    struct Resistor
    {
        NodeIndex a, b;
        double ohms;
    };
} // namespace CircuitComponents
