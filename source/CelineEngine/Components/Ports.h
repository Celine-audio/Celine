#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        The interface every nonlinear device presents to the solver.

        A device is described entirely by its *ports*: a pair of nodes whose
        voltage difference drives the device and through which its current
        flows.

            diode      1 port   (anode, cathode)
            BJT        2 ports  (base, emitter), (base, collector)
            triode     2 ports  (grid, cathode), (plate, cathode)
            pentode    3 ports  + (screen, cathode)

        Given the voltages across its ports a device returns the current through
        each and the partial derivatives between them; it never learns what a
        node index means or that a matrix exists. So adding a device type costs
        a header and no solver changes.

        The port count, not the node count, is what the nonlinear solve costs: a
        four-triode preamp has forty nodes and eight ports, and Newton iterates
        on the ports alone. Exploiting that is the DK method -- see DkSystem in
        Engine.h.
    */

    /** A port: the device's current flows from `a` to `b`, driven by
        V(a) - V(b). */
    struct Port
    {
        NodeIndex a, b;
    };

    /** A pentode needs three; the spare keeps fixed-size arrays comfortable for
        a device that grows a port later. */
    inline constexpr int maxPortsPerDevice = 4;

    /**
        A device linearised about an operating point -- the tangent plane Newton
        needs.

        `current[p]` is the current through port p, and `jacobian[p*n + q]` is
        d(current[p]) / d(voltage[q]) with n the device's port count. Both are
        in real circuit units and orientation: a device with a polarity has
        already applied it.
    */
    struct DeviceLinearisation
    {
        double current[maxPortsPerDevice];
        double jacobian[maxPortsPerDevice * maxPortsPerDevice];
    };
} // namespace CircuitComponents
