#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        An inductor between two nodes.

        Same trapezoidal companion model as the capacitor, just with the dual
        relationship (v = L di/dt rather than i = C dv/dt):

            i = conductance * v + (iPrev + conductance * vPrev)

        with conductance = dt/2L. Circuit stores the equivalent current source
        with the same sign convention it uses for capacitors, so both end up
        stamping identically.
    */
    struct Inductor
    {
        NodeIndex a, b;
        double henries;
        double conductance = 0.0; // dt/2L, cached by Circuit when dt or henries changes
        double vPrev = 0.0;
        double iPrev = 0.0;
    };
} // namespace CircuitComponents
