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
            Ip  = 2 * E1^EX / KG1 * atan(Vpk / KVB) * (1 + Vpk / VA)
            Ig2 = 2 * E1^EX / KG2

        The last factor is not Koren's. His arctangent does two jobs -- the
        knee, and the slope of the curves above it, which is the plate
        resistance -- and one parameter cannot do both: fitting Ri at the
        operating point pins KVB near 50 V on every valve, which makes the knee
        far softer than any datasheet draws it. The plate-slope voltage VA takes
        the slope, the way a transistor's Early voltage does, and leaves KVB to
        set the knee alone. Zero means none, which is Koren's form exactly.

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

        /** VA, volts: how much plate current still rises with plate voltage
            above the knee. Ip picks up a factor 1 + Vpk/VA. Zero means none --
            Koren's form, where the arctangent sets the slope as well as the
            knee. See the class comment.

            After gridPerveance, not beside KVB where it belongs, because the
            models below are built by aggregate initialisation in field order:
            put it earlier and every valve's grid perveance lands here. */
        double plateSlopeVoltage = 0.0;

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
        // Models, all fitted here to the JJ datasheets. Method and the reason
        // EX is held at 1.35 are in the note further down; the KT88, at the
        // end, is the one whose sheet says enough to fit EX as well.
        //
        // Koren's published sets were badly out on plate resistance -- they gave
        // 40k, 98k and 237k where the datasheets say 15k, 22.5k and 40k, so the
        // plate curves were two to six times too flat and the power stage far
        // stiffer than the real valve.
        //
        // All but the 6V6S now carry the plate-slope term, refitted with the
        // operating point held exactly and the knee and slope fitted to the
        // sheet's plate family -- digitised from the vector paths in
        // docs/specs/tubes, every curve sampled at 5 V steps down to 10 V
        // where it rises steadily into its knee and from 30 V where it dips
        // (secondary emission, which nothing here models). The rms figures
        // quoted are all measured the same way, every 10 V from 30 V up, so
        // they compare across valves; the older figures some notes used to
        // quote came from a different sampling and do not.

        /** EL34. The British power valve -- Marshall, Hiwatt, Orange.

            Fitted at Ua 250 V, Ug2 265 V, Ug1 -13.5 V: Ia 100 mA, Ig2 14.9 mA,
            S 11 mA/V, Ri 15k, mu(g2/g1) 11 -- all four reproduced exactly.

            Against the plate family (Ug2 250 V, Ug1 0 to -25 V): 7.8 mA rms,
            where Koren's form alone managed 17.7. The knee is at 13 V rather
            than 59, so the top curves reach their plateau where the sheet
            draws it -- 245 mA at Ua 150 on Ug1 0 V against the old 236 and
            the sheet's 264 -- at the cost of overshooting the -15 V curve's
            knee (54 mA at Ua 50 against the sheet's 40). */
        static PentodeModel el34() noexcept
        {
            PentodeModel m{11.0, 1.35, 898.37653, 3450.37128, 55.7232413, 13.3393007, 1.0e-3};
            m.plateSlopeVoltage = 1649.6343;
            m.capGridCathode = 15.5e-12; m.capGridPlate = 1.3e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** 6L6GC. The American one -- Fender, Mesa. Stiffer than an EL34 and
            cleaner for longer before it gives up.

            Fitted at Ua = Ug2 = 250 V, Ug1 -14 V to Ia 72 mA, Ig2 5 mA and
            Ra 22.5k from JJ's sheet, and S 6 mA/V, which JJ omits and Ei's and
            Coolaudio's 6L6GC sheets both give at the same point -- all four
            exact.

            JJ's plate family is at Ug2 400 V, which is where a Fender actually
            runs its screens, and it is what sets MU and EX here: both were
            Koren's guesses before, unverified, and freeing them takes the
            family from 14.0 mA rms to 9.2. MU comes out at 12.5 and EX at 1.19,
            near the KT88's 1.16 -- another beam tetrode. MU is the model's
            parameter rather than the valve's mu; the softplus spreads the
            cutoff, as it does on the KT88.

            Held out: JJ's push-pull column, Ua = Ug2 270 V, Ug1 -17.5 V, puts
            67 mA through each valve, and this model 66.0. Its screen current
            there is 4.5 mA against 5.5 -- Koren's fixed plate-to-screen ratio,
            the same on the old fit. */
        static PentodeModel u6l6gc() noexcept
        {
            PentodeModel m{12.4698915, 1.19186071, 666.13387, 5889.28417, 28.8416111, 23.1373379, 1.0e-3};
            m.plateSlopeVoltage = 2459.84194;
            m.capGridCathode = 12.5e-12; m.capGridPlate = 1.5e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** EL84. The small British bottle -- Vox AC30, and every small combo
            that breaks up early and politely.

            Fitted at Ua = Ug2 = 250 V, Ug1 -7.3 V: Ia 48 mA, Ig2 5.5 mA and
            Ri 40k exactly, S 11.06 against the quoted 11.3 mA/V.

            Against the plate family (Ug2 250 V, Ug1 0 to -10 V): 6.8 mA rms,
            where Koren's form alone managed 16.2, with the knee at 6.3 V
            rather than 47. KP is held where it was: left free it runs off
            towards infinity chasing the quoted S, which the form cannot reach,
            and a cutoff that sharp is a corner Newton has to turn. */
        static PentodeModel el84() noexcept
        {
            PentodeModel m{19.0, 1.35, 790.522564, 3954.74437, 1280.5, 6.27723688, 1.0e-3};
            m.plateSlopeVoltage = 1943.49294;
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

            Fitted to the JJ datasheet; see the note above. The plate-slope term
            takes its family -- at Ug2 400 V, curves starting from 50 V -- from
            27.3 mA rms to 23.5, and puts the knee where the sheet starts:
            188 mA at Ua 50 on Ug1 -15 V against the sheet's 176 and the old
            135.

            The rest of that 23.5 is not the plate's. The sheet's low curves at
            400 V sit 15-25 mA under the model -- 67 mA on Ug1 -35 V where the
            model draws 93 -- which is how the current scales with screen
            voltage, not plate. Freeing MU and EX buys 17.8 mA rms, but only at
            MU 50, which a -50 V curve still conducting at 400 V rules out; and
            the sheet's triode-connected Ri of 1.3k cannot be met alongside the
            family at all (the model gives 1.07k). Koren's grid law does not
            describe this valve across both screen voltages, so MU and EX stay
            where the 250 V figures put them. */
        static PentodeModel kt66() noexcept
        {
            PentodeModel m{7.8, 1.35, 1933.52701, 13741.093, 36.06928, 10.1143468, 1.0e-3};
            m.plateSlopeVoltage = 2146.31153;
            m.capGridCathode = 16.0e-12; m.capGridPlate = 2.3e-12; m.capPlateCathode = 10.0e-12;
            return m;
        }

        /** KT77. JJ's EL34 substitute -- same socket and much the same job, but
            noticeably stiffer: nearly twice the KT66's transconductance at the
            same operating point, and it holds together longer before it gives up.

            Fitted to the JJ datasheet; see the note above. The one valve the
            plate-slope term barely moves: VA comes out at 16.7 kV, so its knee
            was already near right in Koren's form, and its family goes from
            8.9 mA rms to 8.2. */
        static PentodeModel kt77() noexcept
        {
            PentodeModel m{11.5, 1.35, 647.6862, 4448.7261, 37.3666483, 34.3147436, 1.0e-3};
            m.plateSlopeVoltage = 16672.6438;
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
            which matters for screen dissipation and not much else.

            The one pentode still in Koren's form, because its sheet has no
            pentode plate family to fit a plate slope to -- only triode-connected
            curves, which cannot separate the knee from the slope (a fit drives
            KVB to zero and leaves VA wherever it started). Those curves are
            still worth having: held out, Ug1 0 to -40 V, screen strapped to
            plate, they land within 4.1 mA rms of this model. */
        static PentodeModel u6v6() noexcept
        {
            PentodeModel m{9.8, 1.35, 2109.7, 13453.7, 45.03, 40.22, 1.0e-3};
            m.capGridCathode = 9.0e-12; m.capGridPlate = 0.7e-12; m.capPlateCathode = 8.5e-12;
            return m;
        }

        /** KT88. The big beam tetrode -- the SVT, the Marshall Major, and the
            100 W hi-fi amps. The most current of any valve here at the same
            voltages, and the most transconductance, a shade over the EL34's.

            Fitted to the JJ datasheet at Ua = Ug2 = 250 V, Ug1 -15 V, where it
            reproduces every figure the sheet prints:

                pentode   Ia 140 mA  Ig2 7 mA   S 11.5 mA/V  Ri 12k     exact
                triode    Ik 147 mA             S 12.08      Ri 670     sheet: 147, 12, 670

            The sheet calls Ig2 "max. 7 mA". Its triode-connected column, at the
            same voltages, puts Ia + Ig2 at 147 -- and strapping the screen to
            the plate changes no voltage there -- so 7 is the typical too.

            Two things differ from the fits above, both because this sheet says
            more than theirs do.

            **EX is not 1.35.** With Ia, Ig2, S and Ri fixed, the triode-connected
            Ri depends on EX alone -- 624 ohms at 1.35, against the sheet's 670 --
            so the sheet pins it, at 1.164. The KT77's EX was held at 1.35
            because a free EX tracked its plate curves worse; this one is not
            free, and it tracks them better (below).

            **MU is not the sheet's 8.** That 8 is the triode-mode figure
            (S x Ri = 12 x 0.67): with the plate's own 12k in parallel, the
            triode figures imply a grid-to-screen mu of 8.5, and the model's is
            8.57 at the operating point. MU the *parameter* only trades against
            KP once EX is fixed -- the cutoff's sharpness against its position --
            and is set, with the knee and the plate slope, from the sheet's
            plate characteristics, which are at Ug2 = 300 V:

                Ug1      -5    -10    -15    -20    -25    -30    -35    -40
                sheet   351    270    195    146    109     74     39     13    Ia at Ua 250, mA
                model   355    279    210    150    101     64     38     21

            10.6 mA rms over the family from Ua 30 V, against 18.8 in Koren's
            form. The plate-slope term is what fixed the knee: at Ua 50 V,
            Ug1 -15 the sheet shows 162 mA, this model 165, and Koren's
            arctangent alone 117. What it cannot fix is the low curves above
            300 V, which keep climbing on the sheet (Ug1 -30 V: 74 mA at 250,
            87 at 400) and here barely move (64 and 69) -- one slope for the
            whole family cannot give the coldest curves a steeper one. The sheet's ultralinear chart reads
            10-20% above the model, but that chart disagrees with the plate
            characteristics by 8% where the two coincide, and labels its screen
            axis 355 V where the taps put 335; it was not used. */
        static PentodeModel kt88() noexcept
        {
            PentodeModel m{10.2299523, 1.16445896, 405.44122, 4742.07606, 34.6757806, 13.5741785, 1.0e-3};
            m.plateSlopeVoltage = 1960.19476;
            m.capGridCathode = 16.5e-12; m.capGridPlate = 2.3e-12; m.capPlateCathode = 10.0e-12;
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
        double plate = limitStep(v[2], p.vPlateLast, maxPlateStep);

        // Not across the cathode in one step from a plate that is conducting.
        // Below it the plate current is floored at zero and has no slope, so a
        // Newton step that lands there has nothing to come back on -- and a
        // starved plate, behind a big load with the screen taking the cathode
        // current, biases a fraction of a volt above it, where the knee is at
        // its steepest. A sharp knee overshoots into that flat region from a
        // plate still volts away; the KT66 in a 100k stage did, and settled at
        // -149 V. Stopping just short of the corner lands on the steep side
        // instead. A solution that really is below the cathode -- a transformer
        // primary kicking back -- is still reached, one iteration later.
        constexpr double conducting = 1.0;
        constexpr double justAbove = 1.0e-3;

        if (p.vPlateLast > conducting && plate < 0.0)
            plate = justAbove;

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
        const double arctangent = kneeRatio > 0.0 ? std::atan(kneeRatio) : 0.0;
        const double dArctangent = kneeRatio > 0.0
                                     ? (1.0 / model.kvb) / (1.0 + kneeRatio * kneeRatio)
                                     : 0.0;

        // The plate slope, on the same side of zero as the knee: below the
        // cathode there is no current for it to scale.
        const double slopeGain = model.plateSlopeVoltage > 0.0 && vPlate > 0.0
                                   ? 1.0 / model.plateSlopeVoltage
                                   : 0.0;
        const double plateSlope = 1.0 + slopeGain * vPlate;

        const double knee = arctangent * plateSlope;
        const double dKneeDPlate = dArctangent * plateSlope + arctangent * slopeGain;

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
