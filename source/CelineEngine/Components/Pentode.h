#pragma once

#include "Ports.h"
#include "SpaceCharge.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        Pentode parameters, again in Koren's formulation.

        A pentode adds a screen grid between the control grid and the plate,
        held at a fixed high voltage. The screen does the accelerating, which
        leaves the plate voltage almost irrelevant to how much current flows --
        so where a triode's plate curves slope, a pentode's are nearly flat, and
        the valve behaves as a current source rather than a resistor. That's why
        pentodes make power stages: far more output for the same drive.

        Koren's model reuses the triode's shape with the screen taking the
        plate's place in setting the current, and then bends the plate current
        over with an arctangent to produce the knee:

            E1  = (Vg2k/KP) * softplus(KP * (1/MU + Vg1k/Vg2k))
            Ip  = 2 * E1^EX / KG1 * atan(Vpk / KVB)
            Ig2 = 2 * E1^EX / KG2

        Note what Ig2 does *not* depend on: the plate. That's the whole point of
        the screen, and it's also why screen current is what kills output valves
        -- drive the amp hard into clipping and the plate stops taking current
        while the screen carries on regardless.

        Gurskii's complementary knee (audioXpress 2/2011), which makes the screen
        pick up what the plate gives up --

            Ig2 = 2 * E1^EX / KG2 * (pi/2 - atan(Vpk / KVB))

        -- was tried here and taken out again: it is refuted by the published
        transfer characteristics. See LIMITATIONS.md under screen current for the
        measurements and for why no choice of knee width rescues it.
    */
    struct PentodeModel
    {
        double mu = 11.0;    // amplification factor, control grid to screen
        double ex = 1.35;    // current-law exponent
        double kg1 = 650.0;  // plate current scaling
        double kg2 = 4200.0; // screen current scaling
        double kp = 60.0;    // cutoff sharpness
        double kvb = 24.0;   // knee voltage -- where the plate curves flatten off

        /** Control-grid conduction once it goes positive. See TriodeModel. */
        double gridPerveance = 1.0e-3;

        /** Interelectrode capacitance, farads, from the datasheet.

            Note how much smaller grid-to-plate is here than on a triode -- 1 to
            2 pF against a triode's 1.7, on a valve with ten times the electrode
            area. That is the screen doing its job: it stands between the control
            grid and the plate and shields one from the other, which is precisely
            why a pentode has so little Miller capacitance and can therefore drive
            a load at frequencies a triode cannot.

            addPentode() wires these itself. Zero any of them to leave it out. */
        double capGridCathode = 15.5e-12;  // Cg1, grid to everything else
        double capGridPlate = 1.3e-12;     // Ca/g1, the Miller path the screen shields
        double capPlateCathode = 10.0e-12; // Ca

        //======================================================================
        // Models, all five fitted here to the JJ datasheets. Method and the
        // reason EX is held at 1.35 are in the note further down.
        //
        // Koren's published sets were badly out on plate resistance -- they gave
        // 40k, 98k and 237k where the datasheets say 15k, 22.5k and 40k, so the
        // plate curves were two to six times too flat and the power stage far
        // stiffer than the real valve.

        /** EL34. The British power valve -- Marshall, Hiwatt, Orange.

            Fitted at Ua 250 V, Ug2 265 V, Ug1 -13.5 V: Ia 100 mA, Ig2 14.9 mA,
            S 11 mA/V, Ri 15k, mu(g2/g1) 11 -- all four reproduced exactly, and
            7.5 mA rms against the published plate curves. */
        static PentodeModel el34() noexcept
        {
            PentodeModel m{11.0, 1.35, 688.6, 3450.6, 55.7, 58.91, 1.0e-3};
            m.capGridCathode = 15.5e-12; m.capGridPlate = 1.3e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** 6L6GC. The American one -- Fender, Mesa. Lower MU, stiffer, cleaner
            for longer before it gives up.

            Fitted at Ua = Ug2 = 250 V, Ug1 -14 V to Ia 72 mA, Ig2 5 mA and
            Ra 22.5k, which it reproduces exactly.

            PARTLY UNVERIFIED: JJ's sheet quotes no transconductance and no mu
            for this valve, so KP and MU are still Koren's. The transconductance
            that falls out is 6.1 mA/V and nothing here confirms it. Everything
            about how hard it is driven is right; how much gain that produces is
            not pinned down. */
        static PentodeModel u6l6gc() noexcept
        {
            PentodeModel m{8.7, 1.35, 1461.2, 15528.5, 48.0, 54.79, 1.0e-3};
            m.capGridCathode = 12.5e-12; m.capGridPlate = 1.5e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** EL84. The small British bottle -- Vox AC30, and every small combo
            that breaks up early and politely.

            Fitted at Ua = Ug2 = 250 V, Ug1 -7.3 V: Ia 48 mA, Ig2 5.5 mA and
            Ri 40k exactly, S 11.06 against the quoted 11.3 mA/V, and 6.6 mA rms
            against the published plate curves. */
        static PentodeModel el84() noexcept
        {
            PentodeModel m{19.0, 1.35, 628.1, 3954.7, 1280.5, 46.70, 1.0e-3};
            m.capGridCathode = 10.0e-12; m.capGridPlate = 0.6e-12; m.capPlateCathode = 5.1e-12;
            return m;
        }

        //======================================================================
        // The two below are fitted here rather than taken from Koren's library,
        // against the JJ datasheets, at Ua = Ug2 = 250 V:
        //
        //     KT66   Ug1 -15.0 V   Ia  85 mA   Ig2  7 mA   S  6.0 mA/V   Ri 22.5k
        //     KT77   Ug1 -13.5 V   Ia 100 mA   Ig2 10 mA   S 10.5 mA/V   Ra 23.0k
        //
        // MU is Koren's control-grid-to-screen amplification factor, which is
        // what a datasheet's pentode "mu" means. The KT77 sheet states it
        // outright as 11.5. The KT66 sheet doesn't, but gives the valve
        // triode-connected at the same point with Ri = 1.3k, and with S = 6 mA/V
        // that is a triode mu of 7.8 -- and triode-strapping a pentode ties the
        // screen to the plate, so that figure *is* the grid-to-screen mu.
        //
        // KVB follows in closed form from Ia and Ra together; KP is then solved
        // for S, and KG1/KG2 fall out of Ia and Ig2. So all four datasheet
        // figures are reproduced, S to better than 0.05% and the rest exactly.
        //
        // EX is held at Koren's 1.35 rather than fitted. Left free it optimises
        // to 1.09 for the KT77, which hits the operating point marginally better
        // and tracks the datasheet's published plate-curve family *worse* --
        // 14.2 mA rms against 10.5 across ten grid voltages. One point is not a
        // valve; 1.35 is both the more physical figure and the better fit.

        /** KT66. The beam tetrode of the Bluesbreaker and the early Marshalls,
            and of British hi-fi before that. Softer and warmer than a 6L6, which
            it otherwise resembles -- lower gm, and it runs out of steam earlier.

            Fitted to the JJ datasheet; see the note above. */
        static PentodeModel kt66() noexcept
        {
            PentodeModel m{7.8, 1.35, 1568.0, 13742.0, 36.0, 46.87, 1.0e-3};
            m.capGridCathode = 16.0e-12; m.capGridPlate = 2.3e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** KT77. JJ's EL34 substitute -- same socket and much the same job, but
            noticeably stiffer: nearly twice the KT66's transconductance at the
            same operating point, and it holds together longer before it gives up.

            Fitted to the JJ datasheet; see the note above. */
        static PentodeModel kt77() noexcept
        {
            PentodeModel m{11.5, 1.35, 629.4, 4449.7, 37.4, 39.39, 1.0e-3};
            m.capGridCathode = 16.5e-12; m.capGridPlate = 1.0e-12; m.capPlateCathode = 9.0e-12;
            return m;
        }

        /** 6V6S. The small American bottle -- the Champ, the Princeton, the
            Deluxe. Half a 6L6's dissipation and rather less than half its
            stiffness, which is why the amps built on it break up early and
            stay musical doing it.

            Fitted to the JJ 6V6S datasheet at Ua = Ug2 = 250 V, Ug1 -12.5 V:
            Ia 45 mA, Ig2 5 mA and Ra 50k reproduced exactly, and S 4.10 mA/V.

            Two caveats, both of which follow the pattern set by the valves
            above rather than being peculiar to this one.

            S is **not on the JJ sheet**, which quotes no transconductance;
            4.1 mA/V is RCA's figure for the 6V6GT at this exact operating
            point. MU is likewise not stated -- 9.8 is the valve's published
            triode-connected amplification factor, which is what a pentode's
            grid-to-screen mu means (see the KT66 note above).

            What makes those two more than assertions is the datasheet's
            *other* column. Its push-pull entry -- Ug1 -15 V, 70 mA for the
            pair -- was held out of the fit entirely, and this model puts
            70.6 mA through it, 0.9% out. A wrong MU would not land there.

            Screen current is the weak spot, and it is Koren's rather than the
            fit's: Ig2 goes as E1^EX and Ia as E1^EX * atan(Vp/KVB), so their
            *ratio* is fixed by construction. The real valve's screen current
            falls away faster than its plate current as the bias goes colder --
            the sheet's two columns give Ia/Ig2 of 9 and 14 -- so at -15 V this
            model draws 7.8 mA for the pair against a quoted 5. It is right
            where it was fitted and runs high as the valve is biased colder,
            which matters for screen dissipation and not much else. */
        static PentodeModel u6v6() noexcept
        {
            PentodeModel m{9.8, 1.35, 2109.7, 13453.7, 45.03, 40.22, 1.0e-3};
            m.capGridCathode = 9.0e-12; m.capGridPlate = 0.7e-12; m.capPlateCathode = 8.5e-12;
            return m;
        }
    };

    //==========================================================================
    /**
        A pentode. Three ports: (control grid, cathode), (screen, cathode) and
        (plate, cathode).

        Wire it as a triode instead -- screen strapped to the plate -- and you
        get the "triode mode" switch some amps have; there's no separate device
        needed for that, just tie the two nodes together in the netlist.
    */
    struct Pentode
    {
        NodeIndex plate, screen, grid, cathode;
        PentodeModel model;

        double vGridLast = 0.0;
        double vScreenLast = 0.0;
        double vPlateLast = 0.0;
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const Pentode&) noexcept { return 3; }

    inline void fillPorts(const Pentode& p, Port* ports) noexcept
    {
        ports[0] = {p.grid, p.cathode};
        ports[1] = {p.screen, p.cathode};
        ports[2] = {p.plate, p.cathode};
    }

    inline bool limitPortVoltages(Pentode& p, double* v) noexcept
    {
        const double grid = limitStep(v[0], p.vGridLast, maxGridStep);
        const double screen = limitStep(v[1], p.vScreenLast, maxPlateStep);
        const double plate = limitStep(v[2], p.vPlateLast, maxPlateStep);

        const bool acted = std::abs(grid - v[0]) > 0.0
                        || std::abs(screen - v[1]) > 0.0
                        || std::abs(plate - v[2]) > 0.0;

        p.vGridLast = grid;
        p.vScreenLast = screen;
        p.vPlateLast = plate;
        v[0] = grid;
        v[1] = screen;
        v[2] = plate;
        return acted;
    }

    inline void linearise(const Pentode& p, const double* v, DeviceLinearisation& out) noexcept
    {
        const PentodeModel& model = p.model;
        const double vGrid = v[0];
        const double vScreen = v[1];
        const double vPlate = v[2];

        constexpr int n = 3;
        for (int i = 0; i < n * n; ++i)
            out.jacobian[i] = 0.0;

        // Control-grid conduction, as for a triode.
        evaluateSpaceCharge(vGrid, model.gridPerveance, out.current[0], out.jacobian[0]);

        // The screen does the accelerating, so it appears where a triode's plate
        // would. At or below the cathode it pulls nothing across and the valve
        // is off.
        //
        // This gate used to sit at 1 V, which cut the model off while it was
        // still plainly conducting: an EL34 at Ug1 = 0 and Ua = 250 draws
        // 0.18 mA with its screen at 1 V, and the gate stepped that to zero.
        // Newton meets that step during the operating-point solve, which starts
        // with every node at zero and walks the screen up through it.
        //
        // Below the gate the model is genuinely continuous for Ug1 <= 0 -- which
        // is every valve in normal operation -- because E1 carries a leading
        // factor of Vg2 and goes to zero with it. It is *not* continuous for a
        // positive grid: there E1 tends to Ug1 rather than to zero, so a step
        // survives wherever the gate is put. Reaching that needs the grid above
        // the cathode while the screen is at it, which is a valve being
        // destroyed rather than played.
        //
        // A hair above zero rather than at it, and that hair is load-bearing:
        // `vGrid / vScreen` overflows to infinity for a subnormal screen
        // voltage, and the infinity * 0 that then lands in E1 below is a NaN
        // rather than a current.
        constexpr double screenFloor = 1.0e-6;

        if (vScreen <= screenFloor)
        {
            out.current[1] = gmin * vScreen;
            out.current[2] = gmin * vPlate;
            out.jacobian[4] = gmin;
            out.jacobian[8] = gmin;
            return;
        }

        const double a = 1.0 / model.mu + vGrid / vScreen;

        double s = 0.0;
        double slope = 0.0;
        softplusWithSlope(model.kp * a, s, slope);

        const double e1 = (vScreen / model.kp) * s;

        if (e1 <= 0.0)
        {
            out.current[1] = gmin * vScreen;
            out.current[2] = gmin * vPlate;
            out.jacobian[4] = gmin;
            out.jacobian[8] = gmin;
            return;
        }

        // As in Triode.h: e1^(ex-1) = e1^ex / e1, with e1 strictly positive.
        const double shape = fastOrExactPow(e1, model.ex);
        const double dShapeDe1 = model.ex * shape / e1;

        // dE1 with respect to the two grids.
        const double de1dGrid = slope;
        const double de1dScreen = s / model.kp - slope * vGrid / vScreen;

        // The plate's arctangent knee: nearly flat once well past KVB, which is
        // what makes the plate curves of a pentode look the way they do.
        //
        // Floored at zero, because Koren's arctangent is a fit over the region a
        // pentode is *used* in and says nothing sensible below it. Left alone it
        // goes negative with the plate, and so does the plate current -- the
        // valve sourcing current out of its own plate, which is not a thing a
        // plate can do. It only ever collects electrons; drive it below the
        // cathode and it simply stops collecting, while the screen carries on.
        // Measured before this floor, an EL34 with the grid at +5 V reported
        // -285 mA at Vpk = -100 V.
        //
        // Not a corner case. Real output stages undershoot their plates on
        // inductive kick from the transformer -- which is exactly why some amps
        // fit flyback diodes across the primary -- so a hard-driven push-pull
        // stage visits this region every cycle. It is also where Newton wanders
        // during the operating-point solve, and a large negative current there
        // drives the iteration away from the root rather than towards it.
        //
        // Continuous in value (atan 0 = 0) with a kink in the slope at exactly
        // Vpk = 0, where the plate current is zero anyway. Screen current is
        // untouched: it does not depend on the plate, which is the whole point
        // of a screen grid.
        const double kneeRatio = vPlate / model.kvb;
        const double knee = kneeRatio > 0.0 ? std::atan(kneeRatio) : 0.0;
        const double dKneeDPlate = kneeRatio > 0.0
                                     ? (1.0 / model.kvb) / (1.0 + kneeRatio * kneeRatio)
                                     : 0.0;

        const double screenCurrent = 2.0 * shape / model.kg2;
        const double plateCurrent = 2.0 * shape * knee / model.kg1;

        out.current[1] = screenCurrent + gmin * vScreen;
        out.current[2] = plateCurrent + gmin * vPlate;

        // Screen current row. It genuinely doesn't depend on the plate.
        out.jacobian[3] = 2.0 * dShapeDe1 * de1dGrid / model.kg2;
        out.jacobian[4] = 2.0 * dShapeDe1 * de1dScreen / model.kg2 + gmin;
        out.jacobian[5] = 0.0;

        // Plate current row.
        out.jacobian[6] = 2.0 * dShapeDe1 * de1dGrid * knee / model.kg1;
        out.jacobian[7] = 2.0 * dShapeDe1 * de1dScreen * knee / model.kg1;
        out.jacobian[8] = 2.0 * shape * dKneeDPlate / model.kg1 + gmin;
    }
} // namespace CircuitComponents
