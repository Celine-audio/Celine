#pragma once

#include "Types.h"

namespace CircuitComponents
{
    /** Windings one transformer may have. Two for an ordinary one, three for a
        centre-tapped secondary, four for a power transformer with a separate
        heater or bias winding. */
    inline constexpr int maxWindings = 4;

    //==========================================================================
    /**
        An ideal transformer of any number of windings.

        Every winding is a node pair and a turns count. Only the ratios between
        the turns matter, so 1:1, 100:100 and 0.5:0.5 are the same transformer.

        The physics is two statements, and they are the only two:

            all windings see the same volts per turn      V(k)/N(k) equal for all k
            the net magnetomotive force is zero           sum of N(k)*I(k) = 0

        Together they conserve power exactly, and impedance reflection follows
        without being stated: a load R on a secondary appears at the primary as
        R times the square of the turns ratio.

        N windings rather than two costs nothing and buys what guitar amps need.
        A centre-tapped secondary is not two transformers -- both halves share
        one core, so their fluxes are locked -- but a three-winding one whose
        secondaries meet at the tap:

            primary   (pa, pc)     N = 1
            upper     (sa, tap)    N = 0.5
            lower     (tap, sb)    N = 0.5

        which is what a push-pull output stage and a full-wave valve rectifier
        are both built on.

        It is ideal, and an output transformer is famously not -- an ideal one
        passes DC, which a real one cannot. The fix is to build the real thing
        out of this plus ordinary components rather than to make this cleverer:
        an inductor across the primary for the magnetising inductance (a short
        at DC, which is what blocks it), one in series for leakage, a resistor
        in series for the copper. SchematicBuilder's Real model does exactly
        that.

        Core saturation is the one thing that cannot be composed this way: it
        needs a nonlinear inductor, which does not exist here yet. */
    struct Winding
    {
        NodeIndex a, b;
        double turns;

        /** The solved current, amps, flowing into terminal `a`. Written by
            Circuit every sample. */
        double current = 0.0;
    };

    struct Transformer
    {
        Winding windings[maxWindings];
        int windingCount = 0;
    };
} // namespace CircuitComponents
