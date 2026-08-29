#pragma once

#include "Ports.h"
#include "SpaceCharge.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A rectifier valve: 5U4, GZ34, 5Y3.

        Electrically it's the simplest valve there is -- a heated cathode and a
        plate, conducting one way by space charge. What makes it worth modelling
        separately from a silicon diode is that it is a *bad* rectifier, and the
        badness is the sound.

        A silicon diode drops about 0.7 V no matter what you draw through it, so
        the supply behind it barely moves. A rectifier valve follows
        i = perveance * v^1.5, so its drop climbs with current: tens of volts at
        idle, far more when the power stage pulls hard on a loud chord. The B+
        rail sags under that load and recovers as the note decays, which is the
        compression and "bloom" people buy vintage amps for. Swap in a solid
        state rectifier and the amp gets tighter, louder and less forgiving --
        the same circuit, a different rectifier.

        So model a valve rectifier with this, and a solid-state one with an
        ordinary Diode. The difference between them will come out on its own.
    */
    struct VacuumDiodeModel
    {
        /** Perveance, amps per volt^1.5. Bigger means a stiffer supply and
            less sag. */
        double perveance = 7.07e-4;

        //======================================================================
        // Models, all three now fitted to the JJ datasheets.
        //
        // The GZ34S and 5Y3S sheets print a forward characteristic -- anode
        // current against anode volts -- which is exactly what the perveance
        // describes, so those two are least-squares fits to that curve.
        //
        // The 5U4GB sheet prints no such curve, only capacitor-input design
        // graphs. So it was fitted the long way round: build the supply those
        // graphs describe (350 V rms per plate, Rt 36 ohm, C 40 uF, 50 Hz),
        // run it, and find the perveance whose DC output matches. That agreed
        // with the figure already here, so this one is confirmed rather than
        // changed -- and the exercise doubles as a check that the rectifier
        // model behaves correctly inside a real supply, not just at a point.
        //
        // A caveat that applies to all three: the 3/2 law is too steep at low
        // current. Real rectifiers conduct more than Child-Langmuir predicts
        // near the origin, where initial electron velocity matters and space
        // charge does not yet dominate. On the 5Y3S curve the fit reads 25 mA
        // at 20 V where the sheet shows 45. It converges by mid-range, which is
        // where a supply actually sits, so the sag behaviour is right; the very
        // start of conduction is not.

        /** GZ34 / 5AR4. The stiffest of the common valve rectifiers --
            indirectly heated, low drop. Used where an amp wants valve
            rectification without much sag.

            Fitted to the JJ GZ34S forward curve, which is nearly twice as
            conductive as the figure previously used here: 336 mA at 20 V
            against the sheet's 320, where the old value gave 179. */
        static VacuumDiodeModel gz34() noexcept { return {3.752e-3}; }

        /** 5U4GB. The classic big-bottle rectifier of tweed and blackface amps,
            and noticeably softer than a GZ34.

            Confirmed against the JJ capacitor-input design graph by simulation:
            407 V at 99 mA and 366 V at 203 mA, against the sheet's 410/100 and
            360/200. */
        static VacuumDiodeModel u5u4gb() noexcept { return {7.07e-4}; }

        /** 5Y3GT. Small, and the saggiest of the three -- the sound of a small
            tweed amp working hard.

            Fitted to the JJ 5Y3S forward curve. Tracks it from about 60 V
            upward; below that see the note above about the 3/2 law. */
        static VacuumDiodeModel u5y3gt() noexcept { return {2.807e-4}; }
    };

    /** A rectifier valve conducting from plate to cathode. */
    struct VacuumDiode
    {
        NodeIndex plate, cathode;
        VacuumDiodeModel model;

        double vLast = 0.0;
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const VacuumDiode&) noexcept { return 1; }

    inline void fillPorts(const VacuumDiode& d, Port* ports) noexcept
    {
        ports[0] = {d.plate, d.cathode};
    }

    inline bool limitPortVoltages(VacuumDiode& d, double* v) noexcept
    {
        const double limited = limitStep(v[0], d.vLast, maxPlateStep);
        const bool acted = std::abs(limited - v[0]) > 0.0;

        d.vLast = limited;
        v[0] = limited;
        return acted;
    }

    inline void linearise(const VacuumDiode& d, const double* v, DeviceLinearisation& out) noexcept
    {
        evaluateSpaceCharge(v[0], d.model.perveance, out.current[0], out.jacobian[0]);
    }
} // namespace CircuitComponents
