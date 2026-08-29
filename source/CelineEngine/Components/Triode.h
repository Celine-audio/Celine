#pragma once

#include "Ports.h"
#include "SpaceCharge.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        Triode parameters, in Norman Koren's formulation -- the model nearly
        every valve amp simulation is built on.

        Koren stopped deriving from physics and fitted the published plate
        curves instead. The result is not a physical model but a shape that
        matches measured valves across the region they are used in, including
        the soft knee near cutoff that physics-based models get wrong and where
        a guitar amp spends most of its time.

            E1 = (Vpk/KP) * softplus(KP * (1/MU + Vgk / sqrt(KVB + Vpk^2)))
            Ip = 2 * E1^EX / KG1

        Only MU has physical meaning -- the amplification factor, a real valve
        spec. EX sits near 1.4, close to ideal space charge's 3/2; KG1 scales the
        current, KP sets how sharply the valve cuts off, KVB shapes the
        low-plate-voltage knee.
    */
    struct TriodeModel
    {
        double mu = 100.0;   // amplification factor
        double ex = 1.4;     // current-law exponent
        double kg1 = 1060.0; // current scaling
        double kp = 600.0;   // cutoff sharpness
        double kvb = 300.0;  // knee shaping

        /** Perveance of the grid-to-cathode path once the grid goes positive.

            Published grid-current data barely exists, so this is a plausible
            figure rather than a fitted one -- but being non-zero matters far
            more than its value. A hard-driven stage pushes its grid positive,
            the grid draws current, and the coupling capacitor feeding it charges
            up and shifts the bias: blocking distortion, the splutter of an
            overdriven amp, which does not happen at all without it.

            Koren only -- the Dempwolf-Zolzer models have a fitted grid
            current. */
        double gridPerveance = 1.0e-3;

        //======================================================================
        /** Which set of equations describes this valve.

            Koren fits the published plate curves and is what nearly every amp
            simulation uses. Dempwolf and Zolzer (DAFx-11) measured real valves
            instead, on a fine mesh of grid and plate voltages, and fitted a
            physically-motivated form to what came back:

                Ik = G  * ( log(1 + exp(C  * (Va/MU + Vg))) / C  ) ^ GAMMA
                Ig = Gg * ( log(1 + exp(Cg * Vg))           / Cg ) ^ XI  + Ig0
                Ia = Ik - Ig

            Two things it buys: the exponent is free rather than Koren's fixed
            1.4, which is why Koren cannot fit a datasheet's operating point and
            its curve family at once; and the grid current is *measured*, where
            Koren has none and this project bolts a guessed law onto it.
            Ia = Ik - Ig also conserves cathode current.

            It costs two pow() calls against Koren's one, and has no
            low-plate-voltage knee term -- the paper is explicit that below about
            20 V its shape is wrong. */
        enum class Formulation
        {
            Koren,
            DempwolfZolzer,
        };

        Formulation formulation = Formulation::Koren;

        // Dempwolf-Zolzer parameters. MU above is shared; these are unused by
        // the Koren path and vice versa.
        double dzG = 2.242e-3;    // G,     cathode-current perveance
        double dzC = 3.40;        // C,     how sharply the cathode current knees
        double dzGamma = 1.26;    // GAMMA, the exponent Koren fixes at 1.4
        double dzGg = 6.177e-4;   // Gg,    grid perveance
        double dzCg = 9.901;      // Cg,    grid-current knee
        double dzXi = 1.314;      // XI,    grid-current exponent
        double dzIg0 = 8.025e-8;  // Ig0,   a constant the paper adds for stability

        //======================================================================
        /** Interelectrode capacitance, farads. Straight off the datasheet.

            Not a refinement. Grid-to-plate is the Miller capacitance: the grid
            presents Cgk + Cga*(1 + A), so at a gain of 53, 1.7 pF looks like
            93 pF. What that costs depends on what drives the grid, since the
            capacitance only matters against a source impedance. Measured on a
            12AX7 stage here, loss at 10 kHz relative to 1 kHz:

                68k  grid stopper    -0.7 dB     barely audible
                470k source          -7.2 dB     a different-sounding amp

            Driving the stage until its incremental gain falls fivefold moves the
            470k case to -5.7 dB and the 68k case not at all, so "an overdriven
            stage gets brighter" is real but small.

            addTriode() wires these itself. Zero leaves one out. */
        double capGridCathode = 1.6e-12;  // Cgk
        double capGridPlate = 1.7e-12;    // Cga -- the one Miller multiplies
        double capPlateCathode = 0.33e-12; // Cak

        //======================================================================
        // Models.
        //
        // Four of the Koren models here are fitted to the JJ datasheets, at the
        // operating point each sheet quotes. `ecc83ge()` is the exception and is
        // documented at its own definition -- it is the same type fitted to a
        // different and much larger body of evidence, and it deliberately makes
        // the opposite trade to the one described below.
        //
        //     ECC83S   250 V, -2.0 V    Ia  1.2 mA   S 1.60 mA/V   Ri 62.5k   mu 100
        //     ECC81    250 V, -2.0 V    Ia 10.0 mA   S 5.50 mA/V   Ri 11.0k   mu  60
        //     12AY7    250 V, -4.0 V    Ia  3.0 mA   S 1.75 mA/V   Ri 22.8k   mu  40
        //     ECC82    250 V, -8.5 V    Ia 10.5 mA   S 2.20 mA/V   Ri  7.7k   mu  17
        //
        // In every case Ia, S and Ri are reproduced exactly and the realised
        // gm*ra lands within 0.5 of the stated mu. Koren's own published sets
        // were 20-30% out on this point -- the ECC81 gave 12.8 mA, 7.2 mA/V and
        // 7.9k against 10, 5.5 and 11k -- and were also worse across the
        // published curve families, so this is not a trade.
        //
        // KVB comes out far larger than Koren's usual 300. That is not a typo:
        // it is what fitting Ri as well as Ia and gm demands, and it is what
        // makes the plate curves track the datasheet. Measured against the
        // published plate characteristics at Ug = -2 V, rms error drops from
        // 0.55 to 0.41 mA on the ECC83 and from 5.29 to 2.03 mA on the ECC81.
        //
        // One honest caveat: Koren's formulation cannot hit the datasheet's
        // three figures *and* the whole transfer curve at once. Fitting the
        // curve alone gets its rms to 0.02 mA but pulls gm 23% low. The stated
        // figures win here because they are the manufacturer's measurements and
        // the curves are read off a printed graph.

        /** 12AX7 / ECC83. The preamp valve -- nearly every guitar amp's first
            few stages. High MU, so a lot of gain from one stage.

            Fitted to the JJ ECC83S datasheet; see the note above. */
        static TriodeModel ecc83() noexcept
        {
            TriodeModel m;
            m.mu = 100.0; m.ex = 1.4; m.kg1 = 1270.1; m.kp = 771.3; m.kvb = 23706.6;
            m.capGridCathode = 1.6e-12; m.capGridPlate = 1.7e-12; m.capPlateCathode = 0.33e-12;
            return m;
        }

        /** 12AX7 / ECC83, fitted to General Electric's ET-T509B sheet (6-53)
            rather than to JJ's, and making the opposite trade.

            Koren's form cannot hit a datasheet's headline figures *and* its
            curve family at once. `ecc83()` takes the figures; this takes the
            curves, fitted to GE's resistance-coupled table (18 stages, Rp and Rs
            0.1-1.0 Meg at 90, 180 and 300 V) and its Eb = 100 V characteristics,
            which pin gm and ra separately rather than only their product.

            Measured against that table (`[ge]` in tests/CelineEngine.cpp),
            `ecc83()` is 32.5% rms out and the error tracks the supply: -4% at
            300 V, -23% at 180 V, -50% at 90 V. That is KVB's doing --
            `sqrt(KVB + Vp^2)` is the only term carrying the plate's influence on
            grid authority, and at KVB = 23706 its transition sits at 154 V, so
            ordinary preamp voltages fall on the wrong side of it.

            This set is 2.5% rms over the same stages with no supply bias, and gm
            and ra within 6% at Eb = 100 V. The price is the headline point,
            missed by about 6%. Withholding the 180 V rows from the fit still cut
            their error from 23.1% to 10.4%, so it generalises rather than
            memorising.

            Two caveats. KVB = 4793 is well above the 300-600 usually quoted, and
            EX = 1.16 just below the usual 1.2-1.6 -- but forcing KVB to 500
            costs the table 2.5% -> 7.4%, so the data wants it high. And no Koren
            set satisfies the stages, the curves, the headline point and absolute
            Ia together; that is a limit of the formulation.

            Reach for this one at low plate voltages, `ecc83()` when the published
            operating point has to be exact. */
        static TriodeModel ecc83ge() noexcept
        {
            TriodeModel m;
            m.mu = 103.1; m.ex = 1.161; m.kg1 = 1036.9; m.kp = 464.4; m.kvb = 4792.5;
            m.capGridCathode = 1.6e-12; m.capGridPlate = 1.7e-12; m.capPlateCathode = 0.33e-12;
            return m;
        }

        /** 12AT7 / ECC81. Medium MU, often a reverb or phase-inverter driver.

            Fitted to the JJ ECC81 datasheet; see the note above. */
        static TriodeModel ecc81() noexcept
        {
            TriodeModel m;
            m.mu = 61.75; m.ex = 1.4; m.kg1 = 629.1; m.kp = 1656.9; m.kvb = 16275.9;
            // JJ's ECC81 sheet is the one that lists no capacitances. These are
            // the figures it gives for the ECC83S and 12AY7, which share the
            // envelope and, on JJ's own sheets, the same numbers as each other.
            m.capGridCathode = 1.6e-12; m.capGridPlate = 1.7e-12; m.capPlateCathode = 0.33e-12;
            return m;
        }

        /** 12AY7 / 6072. The first stage of a tweed Bassman and most tweed-era
            Fenders. MU of 40 against a 12AX7's 100, so it takes far more signal
            before it breaks up -- which is exactly why Fender used it at the
            front of an amp meant to stay clean.

            Fitted to the JJ 12AY7/6072 datasheet at Ua 250 V, Ug -4 V:
            Ia 3.0 mA, S 1.75 mA/V, Ri 22.8k, all three exact, realised mu 39.9.

            Note MU was 44 here before, which is the figure usually quoted for
            this valve elsewhere. JJ's sheet says 40, and that is what their
            tube measures, so that is what this models. */
        static TriodeModel ecc12ay7() noexcept
        {
            TriodeModel m;
            m.mu = 40.0; m.ex = 1.4; m.kg1 = 2172.3; m.kp = 455.0; m.kvb = 2172.0;
            m.capGridCathode = 1.6e-12; m.capGridPlate = 1.7e-12; m.capPlateCathode = 0.33e-12;
            return m;
        }

        /** 12AU7 / ECC82. Low MU, high current -- cathode followers and phase
            inverters, where you want drive rather than gain.

            Fitted to the JJ ECC82 datasheet at Ua 250 V, Ug -8.5 V:
            Ia 10.5 mA, S 2.2 mA/V, Ri 7.7k, all three exact, realised mu 16.9. */
        static TriodeModel ecc82() noexcept
        {
            TriodeModel m;
            m.mu = 16.89; m.ex = 1.4; m.kg1 = 2625.1; m.kp = 1084.2; m.kvb = 3279.9;
            m.capGridCathode = 1.9e-12; m.capGridPlate = 1.63e-12; m.capPlateCathode = 1.9e-12;
            return m;
        }

        //======================================================================
        // Measured 12AX7s, from Table 1 of Dempwolf and Zolzer, "A
        // physically-motivated triode model for circuit simulations", DAFx-11.
        //
        // These are not a datasheet and not a type -- each one is a particular
        // bottle the authors put on a bench and swept, grid and plate current
        // together, over a fine mesh from Va 20-300 V and Vg -5 to +3 V. That
        // makes them the only models here whose *grid* current is measured
        // rather than assumed.
        //
        // Their spread is the interesting part and is left in deliberately: the
        // same nominal valve measures MU 103.2, 100.2 and 86.9. The paper notes
        // that two tubes of the same type and manufacturer can differ by 20%,
        // and here that is not a caveat in prose, it is three models you can
        // pick between.
        //
        // Note MU is stored in the shared field, so a stage's gain reads the
        // same way whichever formulation is in use.

        /** 12AX7, an RSD tube as measured in the paper. MU 103.2. */
        static TriodeModel measured12ax7RSD() noexcept
        {
            TriodeModel m;
            m.formulation = Formulation::DempwolfZolzer;
            m.mu = 103.2;
            m.dzG = 2.242e-3; m.dzC = 3.40; m.dzGamma = 1.26;
            m.dzGg = 6.177e-4; m.dzCg = 9.901; m.dzXi = 1.314; m.dzIg0 = 8.025e-8;
            // Section 5.6 of the paper, which takes them from the data sheets.
            m.capGridCathode = 2.3e-12; m.capGridPlate = 2.4e-12; m.capPlateCathode = 0.9e-12;
            return m;
        }

        /** A second RSD tube of the same type. MU 100.2 -- close to the first,
            which is what a good pair looks like. */
        static TriodeModel measured12ax7RSD2() noexcept
        {
            TriodeModel m;
            m.formulation = Formulation::DempwolfZolzer;
            m.mu = 100.2;
            m.dzG = 2.173e-3; m.dzC = 3.19; m.dzGamma = 1.28;
            m.dzGg = 5.911e-4; m.dzCg = 11.76; m.dzXi = 1.358; m.dzIg0 = 4.527e-8;
            // Section 5.6 of the paper, which takes them from the data sheets.
            m.capGridCathode = 2.3e-12; m.capGridPlate = 2.4e-12; m.capPlateCathode = 0.9e-12;
            return m;
        }

        /** An Electro-Harmonix 12AX7. MU 86.9 -- 16% below the RSD pair, and a
            noticeably lower perveance with it, so it draws less current for the
            same bias. This is what "12AX7" actually covers. */
        static TriodeModel measured12ax7EHX() noexcept
        {
            TriodeModel m;
            m.formulation = Formulation::DempwolfZolzer;
            m.mu = 86.9;
            m.dzG = 1.371e-3; m.dzC = 4.56; m.dzGamma = 1.349;
            m.dzGg = 3.263e-4; m.dzCg = 11.99; m.dzXi = 1.156; m.dzIg0 = 3.917e-8;
            // Section 5.6 of the paper, which takes them from the data sheets.
            m.capGridCathode = 2.3e-12; m.capGridPlate = 2.4e-12; m.capPlateCathode = 0.9e-12;
            return m;
        }
    };

    //==========================================================================
    /**
        A triode. Ports are (grid, cathode) and (plate, cathode), so the port
        voltages are Vgk and Vpk -- exactly what the model is written in.
    */
    struct Triode
    {
        NodeIndex plate, grid, cathode;
        TriodeModel model;

        double vGridLast = 0.0;
        double vPlateLast = 0.0;
    };

    //==========================================================================
    // Port interface -- see Ports.h.

    constexpr int portCount(const Triode&) noexcept { return 2; }

    inline void fillPorts(const Triode& t, Port* ports) noexcept
    {
        ports[0] = {t.grid, t.cathode};
        ports[1] = {t.plate, t.cathode};
    }

    inline bool limitPortVoltages(Triode& t, double* v) noexcept
    {
        const double grid = limitStep(v[0], t.vGridLast, maxGridStep);
        const double plate = limitStep(v[1], t.vPlateLast, maxPlateStep);

        const bool acted = std::abs(grid - v[0]) > 0.0 || std::abs(plate - v[1]) > 0.0;

        t.vGridLast = grid;
        t.vPlateLast = plate;
        v[0] = grid;
        v[1] = plate;
        return acted;
    }

    /** The Dempwolf-Zolzer form. Both currents are the same shape -- a softplus
        raised to a fitted power -- so both are evaluated by this. Returns the
        current and its slope with respect to `x`.

        `h^(p-1)` is `h^p / h`, so one pow() serves for both, exactly as in the
        Koren path. Below `tiny` the valve is off and the division would be 0/0. */
    inline void evaluateSoftPower(double x, double gain, double knee, double exponent, double& current,
                                  double& slope) noexcept
    {
        constexpr double tiny = 1.0e-30;

        double s = 0.0;
        double sigmoid = 0.0;
        softplusWithSlope(knee * x, s, sigmoid);

        const double h = s / knee;

        if (h <= tiny)
        {
            current = 0.0;
            slope = 0.0;
            return;
        }

        const double powered = fastOrExactPow(h, exponent);
        current = gain * powered;
        slope = gain * exponent * (powered / h) * sigmoid;
    }

    inline void lineariseDempwolfZolzer(const TriodeModel& model, double vGrid, double vPlate,
                                        DeviceLinearisation& out) noexcept
    {
        // Grid current, measured rather than assumed, and independent of the
        // plate -- which is what the paper's sweeps found for a 12AX7.
        double gridCurrent = 0.0;
        double dGriddVg = 0.0;
        evaluateSoftPower(vGrid, model.dzGg, model.dzCg, model.dzXi, gridCurrent, dGriddVg);
        gridCurrent += model.dzIg0;

        // Cathode current, driven by the effective voltage at the grid plane.
        double cathodeCurrent = 0.0;
        double dCathodedVg = 0.0;
        evaluateSoftPower(vPlate / model.mu + vGrid, model.dzG, model.dzC, model.dzGamma, cathodeCurrent,
                          dCathodedVg);

        // The plate gets whatever the grid didn't intercept. Deriving Ia this
        // way rather than independently is what makes Ik = Ia + Ig hold exactly.
        const double dCathodedVp = dCathodedVg / model.mu;

        // ...except that the subtraction can go negative, and a plate cannot
        // source current. Ig here depends on the grid *alone* -- no plate term at
        // all, which is what the paper's sweeps found -- while Ik collapses as the
        // plate falls. Drive the plate far enough below the cathode and Ig
        // overtakes Ik, at which point the difference turns negative and the model
        // has the plate emitting. Measured: the RSD 12AX7 crosses zero at about
        // Vpk = -133 V and reports -2.2 mA at -300 V with the grid at +3 V.
        //
        // Floored rather than resolved. The tidier repair would be to cap the
        // grid at what the cathode actually emits -- min(Ig, Ik) -- which keeps
        // Ik = Ia + Ig exactly and is better physics. It is not done here because
        // that also fires at *cutoff*, where Ig sits at the model's constant Ig0
        // (tens of nanoamps, added by the paper for numerical stability) while Ik
        // is nanoamps or less. Capping there would drop the standing grid current
        // roughly fortyfold, which is a change to bias drift in a region every
        // clipping preamp visits constantly -- an audible change to saved sheets,
        // made on an argument rather than a measurement.
        //
        // So: fix the defect that was filed, change nothing that was working. The
        // floor cannot fire while Ik > Ig, which is every normally-operating
        // valve, so no ordinary circuit moves at all. The cost is that Ik = Ia + Ig
        // no longer holds inside the clamped region -- a region the model was
        // already describing wrongly.
        const double plateCurrent = cathodeCurrent - gridCurrent;
        const bool plateConducting = plateCurrent > 0.0;

        out.current[0] = gridCurrent + gmin * vGrid;
        out.current[1] = (plateConducting ? plateCurrent : 0.0) + gmin * vPlate;

        out.jacobian[0] = dGriddVg + gmin;
        out.jacobian[1] = 0.0; // grid current doesn't see the plate
        out.jacobian[2] = plateConducting ? dCathodedVg - dGriddVg : 0.0;
        out.jacobian[3] = (plateConducting ? dCathodedVp : 0.0) + gmin;
    }

    inline void linearise(const Triode& t, const double* v, DeviceLinearisation& out) noexcept
    {
        const TriodeModel& model = t.model;
        const double vGrid = v[0];
        const double vPlate = v[1];

        if (model.formulation == TriodeModel::Formulation::DempwolfZolzer)
        {
            lineariseDempwolfZolzer(model, vGrid, vPlate, out);
            return;
        }

        // Grid conduction, independent of the plate to the accuracy we need.
        // Into locals rather than straight into the output, because the plate
        // current below is derived from it.
        double gridCurrent = 0.0;
        double dGriddVg = 0.0;
        evaluateSpaceCharge(vGrid, model.gridPerveance, gridCurrent, dGriddVg);

        out.current[0] = gridCurrent;
        out.jacobian[0] = dGriddVg;
        out.jacobian[1] = 0.0;

        // Koren's plate current. The chain runs
        //     root -> a -> e1 -> plate current
        // and each derivative below is one link of it.
        const double root = std::sqrt(model.kvb + vPlate * vPlate);
        const double a = 1.0 / model.mu + vGrid / root;

        double s = 0.0;
        double slope = 0.0;
        softplusWithSlope(model.kp * a, s, slope);

        const double e1 = (vPlate / model.kp) * s;

        // Below cutoff the fitted curve goes negative, where a fractional power
        // isn't defined. The valve is simply off there.
        if (e1 <= 0.0)
        {
            out.current[1] = gmin * vPlate;
            out.jacobian[2] = 0.0;
            out.jacobian[3] = gmin;
            return;
        }

        // e1^(ex-1) is just e1^ex / e1, and e1 is strictly positive here, so the
        // derivative comes free off the same pow(). Worth the line: pow with a
        // fractional exponent is the second most expensive call in the model.
        const double powered = fastOrExactPow(e1, model.ex);
        const double current = 2.0 * powered / model.kg1;
        const double dCurrentDe1 = 2.0 * model.ex * powered / (e1 * model.kg1);

        // dE1/dVgk and dE1/dVpk. The second picks up two terms: the plate scales
        // E1 directly, and it also moves the operating point through `root`.
        const double de1dGrid = vPlate * slope / root;
        const double de1dPlate = s / model.kp - vPlate * vPlate * vGrid * slope / (root * root * root);

        // The plate gets what the grid did not intercept.
        //
        // Koren's curve is fitted to plate current measured at a *negative*
        // grid, where the grid takes nothing and the plate therefore collects
        // the whole of what the cathode emits. Continued into positive grid it
        // is thus a cathode current, and the grid's share has to come out of it.
        // Adding grid current alongside it -- which is what this did -- has the
        // cathode emitting Ik + Ig, so a hard-driven stage drew more total
        // current than the curve it was fitted to.
        //
        // Same structure and same floor as lineariseDempwolfZolzer above, which
        // has always derived Ia this way. Two things make it safer here than
        // there: Koren has no Ig0 constant, so the floor cannot fire at cutoff
        // the way the note up there describes, and below a positive grid
        // evaluateSpaceCharge returns gmin*Vgk -- a picoamp against milliamps,
        // so every stage running with its grid below the cathode, which is every
        // stage in normal operation, is untouched to nine significant figures.
        const double plateCurrent = current - gridCurrent;
        const bool plateConducting = plateCurrent > 0.0;

        out.current[1] = (plateConducting ? plateCurrent : 0.0) + gmin * vPlate;
        out.jacobian[2] = plateConducting ? dCurrentDe1 * de1dGrid - dGriddVg : 0.0;
        out.jacobian[3] = (plateConducting ? dCurrentDe1 * de1dPlate : 0.0) + gmin;
    }
} // namespace CircuitComponents
