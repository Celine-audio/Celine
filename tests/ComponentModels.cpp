// Device models held to their datasheets, device maths held to its own derivatives,
// and each part type followed from the drawing to the engine. Datasheet figures are
// checked through the engine's own equations (linearise(), or a Circuit's bias point).

#include <CelineEngine/Circuits.h>
#include <Schematic/SchematicBuilder.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

namespace
{
    using namespace CircuitComponents;

    constexpr double sampleRate = 48000.0;

    //==========================================================================
    // Bipolar transistors

    struct BjtPoint
    {
        double vbe, hfe;
    };

    /** NPN terminal currents at Vbe, collector `vce` above the emitter, from the device's
        linearisation. */
    void npnCurrents(const Bjt& t, double vbe, double vce, double& collector, double& base)
    {
        DeviceLinearisation out {};
        const double v[2] = { vbe, vbe - vce };
        linearise(t, v, out);

        collector = -out.current[1];
        base = out.current[0] + out.current[1];
    }

    /** Vbe and hFE at a collector current, as a datasheet grades them. */
    BjtPoint npnAt(const BjtModel& model, double ic, double vce = 5.0)
    {
        Bjt t {};
        t.model = model;

        double lo = 0.0, hi = 1.2, collector = 0.0, base = 1.0;

        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            npnCurrents(t, mid, vce, collector, base);
            (collector < ic ? lo : hi) = mid;
        }

        npnCurrents(t, lo, vce, collector, base);
        return { lo, collector / base };
    }

    /** The same through a real Circuit (the only way to reach a Darlington). Collector on a
        supply, base on a swept source; currents read off the sources. */
    BjtPoint circuitNpnAt(const BjtModel& model, double ic, double vce = 5.0)
    {
        Circuit circuit;
        const auto supply = circuit.addVoltageSource("c", "gnd", vce);
        const auto drive = circuit.addVoltageSource("b", "gnd", 0.5);
        circuit.addTransistor("b", "c", "gnd", model);
        circuit.addResistor("in", "gnd", 1000.0);
        circuit.setInputNode("in");
        circuit.setOutputNode("c");
        circuit.prepare(sampleRate);

        double lo = 0.0, hi = 2.5;

        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            circuit.setVoltage(drive, mid);
            circuit.reset();
            (-circuit.getSourceCurrent(supply) < ic ? lo : hi) = mid;
        }

        circuit.setVoltage(drive, lo);
        circuit.reset();
        return { lo, -circuit.getSourceCurrent(supply) / -circuit.getSourceCurrent(drive) };
    }

    /** All three NPN terminal currents at (Vbe, Vbc): base and collector in, emitter out. */
    struct BjtTerminals
    {
        double base, collector, emitter;
    };

    BjtTerminals npnTerminals(const BjtModel& model, double vbe, double vbc)
    {
        Bjt t {};
        t.model = model;

        DeviceLinearisation out {};
        const double v[2] = { vbe, vbc };
        linearise(t, v, out);

        return { out.current[0] + out.current[1], -out.current[1], out.current[0] };
    }

    /** Bisects for the zero of a function that rises through it on [lo, hi]. */
    template <typename Function>
    double risingRoot(Function&& f, double lo, double hi)
    {
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (f(mid) < 0.0 ? lo : hi) = mid;
        }

        return 0.5 * (lo + hi);
    }

    /** Common-emitter h-parameters at a Vce and emitter current, as datasheets quote them:
        hie and hfe with Vce held, hoe with Ib held. */
    struct HParameters
    {
        double hie, hfe, hoe;
    };

    HParameters smallSignalAt(const BjtModel& model, double vce, double emitterCurrent)
    {
        const double vbe = risingRoot([&](double v) { return npnTerminals(model, v, v - vce).emitter - emitterCurrent; },
                                      -0.5, 1.5);
        const double h = 1.0e-6;

        const auto vbeUp = npnTerminals(model, vbe + h, vbe + h - vce);
        const auto vbeDown = npnTerminals(model, vbe - h, vbe - h - vce);
        const auto vceUp = npnTerminals(model, vbe, vbe - vce - h);
        const auto vceDown = npnTerminals(model, vbe, vbe - vce + h);

        const double gBase = (vbeUp.base - vbeDown.base) / (2.0 * h);
        const double gCollector = (vbeUp.collector - vbeDown.collector) / (2.0 * h);
        const double baseVsVce = (vceUp.base - vceDown.base) / (2.0 * h);
        const double collectorVsVce = (vceUp.collector - vceDown.collector) / (2.0 * h);

        const double hfe = gCollector / gBase;
        return { 1.0 / gBase, hfe, collectorVsVce - hfe * baseVsVce };
    }

    /** Vbe and Vce with collector and base currents both forced (the datasheet saturation
        test). */
    std::pair<double, double> saturationAt(const BjtModel& model, double ic, double ib)
    {
        const auto vbcFor = [&](double vbe)
        { return risingRoot([&](double vbc) { return ic - npnTerminals(model, vbe, vbc).collector; }, vbe - 20.0, vbe + 0.3); };

        // Start from the active-region Vbe.
        const double active = npnAt(model, ic).vbe;
        const double vbe = risingRoot([&](double v) { return npnTerminals(model, v, vbcFor(v)).base - ib; },
                                      active, active + 0.4);

        return { vbe, vbe - vbcFor(vbe) };
    }

    /** Collector-base cutoff current: emitter open, collector `vcb` above the base. */
    double icboAt(const BjtModel& model, double vcb)
    {
        const double vbe = risingRoot([&](double v) { return npnTerminals(model, v, -vcb).emitter; }, -2.0, 1.0);
        return npnTerminals(model, vbe, -vcb).collector;
    }

    /** Emitter-base cutoff current: collector open, emitter `veb` above the base. */
    double ieboAt(const BjtModel& model, double veb)
    {
        const double vbc = risingRoot([&](double v) { return -npnTerminals(model, -veb, v).collector; }, -2.0, 1.0);
        return -npnTerminals(model, -veb, vbc).emitter;
    }

    //==========================================================================
    // Pentodes

    struct PentodeCurrents
    {
        double plate, screen;
    };

    PentodeCurrents pentodeAt(const PentodeModel& model, double vg1, double vg2, double va)
    {
        Pentode p {};
        p.model = model;

        DeviceLinearisation out {};
        double v[3] = { vg1, vg2, va };
        linearise(p, v, out);
        return { out.current[2], out.current[1] };
    }

    //==========================================================================
    // MOSFETs

    /** Drain current into the drain at (Vgs, Vds), in circuit orientation. */
    double drainCurrent(const MosfetModel& model, double vgs, double vds)
    {
        Mosfet m {};
        m.model = model;

        DeviceLinearisation out {};
        const double v[2] = { vgs, vds };
        linearise(m, v, out);
        return out.current[1];
    }

    /** The gate voltage that draws `id` with the drain strapped to the gate --
        the datasheet's Vgs(th) test -- as a magnitude for either polarity. */
    double thresholdAt(const MosfetModel& model, double id)
    {
        const double p = model.polaritySign();
        double lo = 0.0, hi = 6.0;

        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (p * drainCurrent(model, p * mid, p * mid) < id ? lo : hi) = mid;
        }

        return lo;
    }

    /** Transconductance at a drain current and Vds, as a magnitude. */
    double transconductanceAt(const MosfetModel& model, double id, double vds)
    {
        const double p = model.polaritySign();
        double lo = 0.0, hi = 12.0;

        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (p * drainCurrent(model, p * mid, p * vds) < id ? lo : hi) = mid;
        }

        const double h = 1.0e-6;
        return (drainCurrent(model, p * (lo + h), p * vds) - drainCurrent(model, p * (lo - h), p * vds))
             / (2.0 * h) * p;
    }

    //==========================================================================
    // Diodes

    /** The voltage across a diode at a forward current, including the series resistance
        and guard ring addDiode() wires. */
    double diodeVoltageAt(const DiodeModel& model, double current)
    {
        Circuit circuit;
        const auto source = circuit.addVoltageSource("a", "gnd", 0.5);
        circuit.addDiode("a", "gnd", model);
        circuit.addResistor("in", "gnd", 1000.0);
        circuit.setInputNode("in");
        circuit.setOutputNode("a");
        circuit.prepare(sampleRate);

        // Up to 4 V, to bracket an LED's drop.
        double lo = 0.0, hi = 4.0;

        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            circuit.setVoltage(source, mid);
            circuit.reset();
            (-circuit.getSourceCurrent(source) < current ? lo : hi) = mid;
        }

        return lo;
    }

    /** Settled amplitude of a sine through a circuit, second half only. */
    float amplitudeOf(Circuit& circuit, double frequency, float amplitude, double rate, double seconds)
    {
        const auto numSamples = static_cast<int>(rate * seconds);
        const double step = 2.0 * std::numbers::pi * frequency / rate;
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float out = circuit.process(static_cast<float>(amplitude * std::sin(step * i)));

            if (i > numSamples / 2)
                peak = std::max(peak, std::abs(out));
        }

        return peak;
    }

    //==========================================================================
    /** A finite-difference check of a device's Jacobian at one point. */
    template <typename Device>
    void checkJacobian(Device device, double v0, double v1)
    {
        DeviceLinearisation at {};
        const double v[2] = { v0, v1 };
        linearise(device, v, at);

        for (int q = 0; q < 2; ++q)
        {
            const double h = 1.0e-7 * std::max(1.0, std::abs(v[q]));
            double up[2] = { v0, v1 }, down[2] = { v0, v1 };
            up[q] += h;
            down[q] -= h;

            DeviceLinearisation plus {}, minus {};
            linearise(device, up, plus);
            linearise(device, down, minus);

            for (int p = 0; p < 2; ++p)
            {
                const double numeric = (plus.current[p] - minus.current[p]) / (2.0 * h);
                const double analytic = at.jacobian[p * 2 + q];

                INFO("port " << p << " / voltage " << q << " at (" << v0 << ", " << v1 << "): analytic "
                             << analytic << ", numeric " << numeric);
                CHECK(analytic == Catch::Approx(numeric).epsilon(1.0e-4).margin(1.0e-9));
            }
        }
    }
} // namespace

//==============================================================================
// Bipolar transistors
//==============================================================================

TEST_CASE("The BC107-109 / BC547-550 grades reproduce their datasheet", "[bjt][model]")
{
    // Philips typicals (Comset BC107-109 sheet): hFE at 10 uA and 2 mA, Vce 5 V. Each
    // grade is fitted to these.
    struct Grade { BjtModel model; double at10uA, at2mA, low, high; };

    const Grade grades[] = {
        { BjtModel::npnBC108A(),  90.0, 180.0, 110.0, 220.0 },
        { BjtModel::npnBC108B(), 150.0, 290.0, 200.0, 450.0 },
        { BjtModel::npnBC109C(), 270.0, 520.0, 420.0, 800.0 },
    };

    for (const auto& g : grades)
    {
        const auto cold = npnAt(g.model, 10.0e-6);
        const auto hot = npnAt(g.model, 2.0e-3);

        INFO("hFE " << cold.hfe << " at 10 uA, " << hot.hfe << " at 2 mA, Vbe " << hot.vbe << " V");
        CHECK(cold.hfe == Catch::Approx(g.at10uA).epsilon(0.01));
        CHECK(hot.hfe == Catch::Approx(g.at2mA).epsilon(0.01));
        CHECK(hot.hfe > g.low);
        CHECK(hot.hfe < g.high);

        // Vbe 0.55 min, 0.65 typ, 0.70 max at 2 mA (Comset); onsemi 0.58 min.
        CHECK(hot.vbe > 0.58);
        CHECK(hot.vbe < 0.70);
    }

    // A thinner base buys the gain and costs output resistance.
    CHECK(BjtModel::npnBC108A().forwardEarlyVoltage > BjtModel::npnBC108B().forwardEarlyVoltage);
    CHECK(BjtModel::npnBC108B().forwardEarlyVoltage > BjtModel::npnBC109C().forwardEarlyVoltage);
}

TEST_CASE("The 2N3904 and 2N3906 land on Fairchild's typical curves", "[bjt][model]")
{
    // Fairchild's curves (docs/specs/transistors, 25 C): pulsed hFE and Vbe(on). 2N3904
    // at Vce 5 V, 2N3906 at Vce 1 V.
    struct Point { double ic, hfe, vbe; };

    const Point npn[] = { { 0.1e-3, 231.0, 0.608 }, { 1.0e-3, 228.0, 0.667 }, { 10.0e-3, 223.0, 0.727 }, { 100.0e-3, 159.0, -1.0 } };
    const Point pnp[] = { { 0.1e-3, 109.0, 0.600 }, { 1.0e-3, 126.0, 0.662 }, { 10.0e-3, 138.0, 0.726 }, { 100.0e-3, 88.0, -1.0 } };

    for (const auto& p : npn)
    {
        const auto at = npnAt(BjtModel::npnSilicon(), p.ic, 5.0);
        INFO("2N3904 at " << p.ic << " A: hFE " << at.hfe << ", Vbe " << at.vbe);
        CHECK(at.hfe == Catch::Approx(p.hfe).epsilon(0.02));

        if (p.vbe > 0.0)
            CHECK(at.vbe == Catch::Approx(p.vbe).margin(0.003));
    }

    // A PNP is the NPN with every sign flipped.
    auto mirrored = BjtModel::pnpSilicon();
    mirrored.polarity = BjtModel::Polarity::NPN;

    for (const auto& p : pnp)
    {
        const auto at = npnAt(mirrored, p.ic, 1.0);
        INFO("2N3906 at " << p.ic << " A: hFE " << at.hfe << ", Vbe " << at.vbe);
        CHECK(at.hfe == Catch::Approx(p.hfe).epsilon(0.04));

        if (p.vbe > 0.0)
            CHECK(at.vbe == Catch::Approx(p.vbe).margin(0.005));
    }
}

TEST_CASE("The 2N5088 and 2N5089 reproduce Fairchild's card and sheet", "[bjt][model]")
{
    // The datasheet's SPICE card through the engine (matches ngspice). Minima: 2N5088
    // 300 / 350 / 300 at 0.1 / 1 / 10 mA (900 max at 0.1 mA); 2N5089 400 / 450 / 400
    // (1200 max).
    struct Point { double ic, card, minimum; };

    const Point p5088[] = { { 100.0e-6, 483.7, 300.0 }, { 1.0e-3, 643.9, 350.0 }, { 10.0e-3, 546.2, 300.0 } };
    const Point p5089[] = { { 100.0e-6, 646.0, 400.0 }, { 1.0e-3, 861.8, 450.0 }, { 10.0e-3, 731.4, 400.0 } };

    for (const auto& p : p5088)
    {
        const auto at = npnAt(BjtModel::npn2N5088(), p.ic);
        INFO("2N5088 at " << p.ic << " A: hFE " << at.hfe);
        CHECK(at.hfe == Catch::Approx(p.card).epsilon(0.01));
        CHECK(at.hfe > p.minimum);
    }

    for (const auto& p : p5089)
    {
        const auto at = npnAt(BjtModel::npn2N5089(), p.ic);
        INFO("2N5089 at " << p.ic << " A: hFE " << at.hfe);
        CHECK(at.hfe == Catch::Approx(p.card).epsilon(0.01));
        CHECK(at.hfe > p.minimum);
    }

    CHECK(npnAt(BjtModel::npn2N5088(), 100.0e-6).hfe < 900.0);
    CHECK(npnAt(BjtModel::npn2N5089(), 100.0e-6).hfe < 1200.0);

    // Vbe(on) 0.8 V max at 10 mA.
    CHECK(npnAt(BjtModel::npn2N5088(), 10.0e-3).vbe < 0.8);

    // The 15 mA knee is what turns the gain over by 10 mA.
    CHECK(npnAt(BjtModel::npn2N5088(), 10.0e-3).hfe < npnAt(BjtModel::npn2N5088(), 1.0e-3).hfe);
}

TEST_CASE("The MPSA18 reproduces onsemi's typicals", "[bjt][model]")
{
    const auto m = BjtModel::npnMPSA18();
    const std::pair<double, double> typicals[] = { { 10.0e-6, 580.0 }, { 100.0e-6, 850.0 },
                                                   { 1.0e-3, 1100.0 }, { 10.0e-3, 1150.0 } };

    for (const auto& [ic, hfe] : typicals)
    {
        INFO("at " << ic << " A");
        CHECK(npnAt(m, ic).hfe == Catch::Approx(hfe).epsilon(0.01));
    }

    // VBE(on) 0.60 V typ at 1 mA.
    CHECK(npnAt(m, 1.0e-3).vbe == Catch::Approx(0.60).margin(0.005));
}

TEST_CASE("The MPSA13 is two transistors and reproduces onsemi's curves", "[bjt][model][darlington]")
{
    const auto m = BjtModel::npnMPSA13();
    REQUIRE(m.darlington);

    // A Darlington is two devices: four ports.
    Circuit circuit;
    circuit.addTransistor("b", "c", "gnd", m);
    circuit.addResistor("in", "b", 1.0e6);
    circuit.addResistor("c", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("c");
    circuit.prepare(sampleRate);
    CHECK(circuit.getPortCount() == 4);

    // onsemi's typical curves, Vce 5 V: hFE 27k / 37k / 59k and VBE(on)
    // 1.12 / 1.15 / 1.30 V at 5 / 10 / 100 mA.
    struct Point { double ic, hfe, vbe; };
    const Point points[] = { { 5.0e-3, 27.0e3, 1.12 }, { 10.0e-3, 37.0e3, 1.15 }, { 100.0e-3, 59.0e3, 1.30 } };

    for (const auto& p : points)
    {
        const auto at = circuitNpnAt(m, p.ic);
        INFO("at " << p.ic << " A: hFE " << at.hfe << ", Vbe " << at.vbe);
        CHECK(at.hfe == Catch::Approx(p.hfe).epsilon(0.06));
        CHECK(at.vbe == Catch::Approx(p.vbe).margin(0.015));
    }

    // Guaranteed 5000 at 10 mA.
    CHECK(circuitNpnAt(m, 10.0e-3).hfe > 5000.0);
}

TEST_CASE("A Darlington's two halves move together on a model swap", "[bjt][darlington]")
{
    // Swapping the card on a pair must update both halves.
    auto build = [](const BjtModel& model, Circuit& circuit, ComponentId& collector, ComponentId& base)
    {
        const auto id = circuit.addTransistor("b", "c", "gnd", model);
        collector = circuit.addVoltageSource("c", "gnd", 5.0);
        base = circuit.addVoltageSource("b", "gnd", 1.15);
        circuit.addResistor("in", "gnd", 1000.0);
        circuit.setInputNode("in");
        circuit.setOutputNode("c");
        circuit.prepare(sampleRate);
        return id;
    };

    auto weaker = BjtModel::npnMPSA13();
    weaker.forwardBeta *= 0.25;
    weaker.saturationCurrent *= 2.0;

    Circuit swapped, fresh;
    ComponentId swappedC = -1, swappedB = -1, freshC = -1, freshB = -1;

    const auto pair = build(BjtModel::npnMPSA13(), swapped, swappedC, swappedB);
    swapped.setTransistorModel(pair, weaker);
    swapped.reset();

    build(weaker, fresh, freshC, freshB);

    CHECK(swapped.getSourceCurrent(swappedC) == Catch::Approx(fresh.getSourceCurrent(freshC)).epsilon(1.0e-9));
    CHECK(swapped.getSourceCurrent(swappedB) == Catch::Approx(fresh.getSourceCurrent(freshB)).epsilon(1.0e-9));

    // A single transistor given a Darlington card stays single; its neighbour is
    // untouched.
    Circuit two;
    const auto first = two.addTransistor("b1", "c", "gnd", BjtModel::npnSilicon());
    two.addTransistor("b2", "c", "gnd", BjtModel::npnSilicon());
    two.setTransistorModel(first, BjtModel::npnMPSA13());
    two.setTransistorModel(first, BjtModel::npnBC108B());

    const auto b1 = two.addVoltageSource("b1", "gnd", 0.6);
    const auto b2 = two.addVoltageSource("b2", "gnd", 0.6);
    two.addVoltageSource("c", "gnd", 5.0);
    two.addResistor("in", "gnd", 1000.0);
    two.setInputNode("in");
    two.setOutputNode("c");
    two.prepare(sampleRate);

    // Different cards, different base currents.
    CHECK(std::abs(two.getSourceCurrent(b1) - two.getSourceCurrent(b2)) > 1.0e-9);
}

TEST_CASE("The high-injection knee turns the gain over and keeps its Jacobian exact", "[bjt][model]")
{
    auto withKnee = BjtModel::npn2N5088();
    auto without = withKnee;
    without.forwardKneeCurrent = 0.0;

    // The knee barely matters below it.
    CHECK(npnAt(withKnee, 100.0e-6).hfe == Catch::Approx(npnAt(without, 100.0e-6).hfe).epsilon(0.02));
    CHECK(npnAt(withKnee, 30.0e-3).hfe < 0.75 * npnAt(without, 30.0e-3).hfe);

    // Newton relies on the derivatives matching what is stamped.
    Bjt t {};
    t.model = withKnee;

    for (const double vbe : { 0.3, 0.55, 0.65, 0.72, 0.8 })
        for (const double vce : { 0.05, 0.3, 5.0 })
            checkJacobian(t, vbe, vbe - vce);
}

TEST_CASE("The NTE102 and NTE103 reproduce NTE's sheet", "[bjt][model][germanium]")
{
    // Every typical on the sheet, at the sheet's own test conditions.
    const auto m = BjtModel::npnNTE103();

    // hFE 80 at Vce 0.15 V, 12 mA, and 90 at 0.2 V, 24 mA (30 and 24 min).
    CHECK(npnAt(m, 12.0e-3, 0.15).hfe == Catch::Approx(80.0).epsilon(0.02));
    CHECK(npnAt(m, 24.0e-3, 0.20).hfe == Catch::Approx(90.0).epsilon(0.02));

    // h-parameters at Vce 6 V, Ie 1 mA: hfe 135, hie 3.6k, hoe 50 uS. hie gets more room:
    // the sheet's includes base spreading resistance the model lacks.
    const auto h = smallSignalAt(m, 6.0, 1.0e-3);
    INFO("hfe " << h.hfe << ", hie " << h.hie << ", hoe " << h.hoe);
    CHECK(h.hfe == Catch::Approx(135.0).epsilon(0.01));
    CHECK(h.hoe == Catch::Approx(50.0e-6).epsilon(0.01));
    CHECK(h.hie == Catch::Approx(3600.0).epsilon(0.05));

    // Saturation: Vce(sat) 0.09 typ at both points (0.15 / 0.20 max), Vbe(sat)
    // 0.27 and 0.30 typ (0.35 / 0.40 max).
    const auto [vbe12, vce12] = saturationAt(m, 12.0e-3, 0.4e-3);
    const auto [vbe24, vce24] = saturationAt(m, 24.0e-3, 1.0e-3);
    INFO("sat: " << vbe12 << " / " << vce12 << " V at 12 mA, " << vbe24 << " / " << vce24 << " V at 24 mA");
    CHECK(vce12 == Catch::Approx(0.09).margin(0.003));
    CHECK(vce24 == Catch::Approx(0.09).margin(0.003));
    CHECK(vbe12 == Catch::Approx(0.27).margin(0.003));
    CHECK(vbe24 == Catch::Approx(0.30).margin(0.003));

    // Cutoff: ICBO 0.8 uA at Vcb 12 V, IEBO 0.5 uA at Veb 2.5 V, from the ideal junctions.
    CHECK(icboAt(m, 12.0) == Catch::Approx(0.8e-6).epsilon(0.01));
    CHECK(ieboAt(m, 2.5) == Catch::Approx(0.5e-6).epsilon(0.01));
    CHECK(m.baseCollectorLeakage == 0.0);

    // Germanium: a fifth of a volt where silicon needs two thirds.
    CHECK(npnAt(m, 1.0e-3, 6.0).vbe < 0.2);

    // The PNP is the same card in the other polarity, current for current.
    Bjt npn {}, pnp {};
    npn.model = m;
    pnp.model = BjtModel::pnpNTE102();
    REQUIRE(pnp.model.polarity == BjtModel::Polarity::PNP);

    for (const double vbe : { 0.1, 0.2, 0.3 })
    {
        DeviceLinearisation n {}, p {};
        const double vn[2] = { vbe, vbe - 6.0 };
        const double vp[2] = { -vbe, 6.0 - vbe };
        linearise(npn, vn, n);
        linearise(pnp, vp, p);

        CHECK(p.current[0] == Catch::Approx(-n.current[0]).epsilon(1.0e-12));
        CHECK(p.current[1] == Catch::Approx(-n.current[1]).epsilon(1.0e-12));
    }
}

TEST_CASE("The 2SC2240 grades reproduce Toshiba's sheet", "[bjt][model]")
{
    const auto gr = BjtModel::npn2SC2240GR();
    const auto bl = BjtModel::npn2SC2240BL();

    // The typical specimen's hFE curve at Vce 6 V, 25 C.
    const std::pair<double, double> curve[] = { { 0.3e-3, 272.0 }, { 1.0e-3, 300.0 },
                                                { 3.0e-3, 313.0 }, { 10.0e-3, 316.0 } };

    for (const auto& [ic, hfe] : curve)
    {
        INFO("GR at " << ic << " A: hFE " << npnAt(gr, ic, 6.0).hfe);
        CHECK(npnAt(gr, ic, 6.0).hfe == Catch::Approx(hfe).epsilon(0.03));
    }

    // VBE 0.65 V typ at Vce 6 V, 2 mA.
    CHECK(npnAt(gr, 2.0e-3, 6.0).vbe == Catch::Approx(0.65).margin(0.002));

    // The sorts, at the point they are sorted at: GR 200-400, BL 350-700, the
    // BL at the geometric middle of its window.
    const double grGain = npnAt(gr, 2.0e-3, 6.0).hfe;
    const double blGain = npnAt(bl, 2.0e-3, 6.0).hfe;
    CHECK(grGain > 200.0);
    CHECK(grGain < 400.0);
    CHECK(blGain == Catch::Approx(495.0).epsilon(0.005));

    // Vce(sat) at Ic / Ib = 10: 0.078 V at 1 mA and 0.063 V at 10 mA.
    CHECK(saturationAt(gr, 1.0e-3, 0.1e-3).second == Catch::Approx(0.078).margin(0.004));
    CHECK(saturationAt(gr, 10.0e-3, 1.0e-3).second == Catch::Approx(0.063).margin(0.004));

    // The sheet's hie is hfe Vt / Ic (NF = 1, no visible base resistance).
    const auto h = smallSignalAt(gr, 6.0, 1.0e-3);
    CHECK(h.hie == Catch::Approx(h.hfe * gr.thermalVoltage / 1.0e-3).epsilon(0.02));

    // Leakage under the sheet's 0.1 uA maxima, at its test voltages.
    CHECK(icboAt(gr, 120.0) < 0.1e-6);
    CHECK(ieboAt(gr, 5.0) < 0.1e-6);

    // A thinner base: more gain, less Vbe, less output resistance.
    CHECK(npnAt(bl, 2.0e-3, 6.0).vbe < npnAt(gr, 2.0e-3, 6.0).vbe);
    CHECK(bl.forwardEarlyVoltage < gr.forwardEarlyVoltage);
}

//==============================================================================
// Valves
//==============================================================================

TEST_CASE("The KT88 reproduces the JJ sheet", "[valve][model]")
{
    const auto m = PentodeModel::kt88();
    const double h = 1.0e-3;

    // Ua = Ug2 = 250 V, Ug1 -15 V: Ia 140 mA, Ig2 7 mA, S 11.5 mA/V, Ri 12k.
    const auto at = pentodeAt(m, -15.0, 250.0, 250.0);
    const double s = (pentodeAt(m, -15.0 + h, 250.0, 250.0).plate - pentodeAt(m, -15.0 - h, 250.0, 250.0).plate) / (2.0 * h);
    const double ri = 2.0 * h / (pentodeAt(m, -15.0, 250.0, 250.0 + h).plate - pentodeAt(m, -15.0, 250.0, 250.0 - h).plate);

    CHECK(at.plate == Catch::Approx(140.0e-3).epsilon(1.0e-4));
    CHECK(at.screen == Catch::Approx(7.0e-3).epsilon(1.0e-4));
    CHECK(s == Catch::Approx(11.5e-3).epsilon(1.0e-4));
    CHECK(ri == Catch::Approx(12000.0).epsilon(1.0e-4));

    // Triode-connected, same voltages: Ia+g2 147 mA, S 12 mA/V, Ri 670.
    const auto cathode = [&m](double vg1, double v)
    {
        const auto c = pentodeAt(m, vg1, v, v);
        return c.plate + c.screen;
    };

    const double triodeS = (cathode(-15.0 + h, 250.0) - cathode(-15.0 - h, 250.0)) / (2.0 * h);
    const double triodeRi = 2.0 * h / (cathode(-15.0, 250.0 + h) - cathode(-15.0, 250.0 - h));

    CHECK(cathode(-15.0, 250.0) == Catch::Approx(147.0e-3).epsilon(1.0e-4));
    CHECK(triodeRi == Catch::Approx(670.0).epsilon(1.0e-3));
    CHECK(triodeS == Catch::Approx(12.0e-3).epsilon(0.01));

    // Plate characteristics at Ug2 300 V, Ua 250 V, read off the sheet.
    const std::pair<double, double> family[] = { { -5.0, 351.0e-3 }, { -10.0, 269.9e-3 }, { -15.0, 194.9e-3 },
                                                 { -20.0, 145.8e-3 }, { -25.0, 108.6e-3 }, { -30.0, 73.8e-3 },
                                                 { -35.0, 38.7e-3 } };
    double squares = 0.0;

    for (const auto& [vg1, sheet] : family)
    {
        const double model = pentodeAt(m, vg1, 300.0, 250.0).plate;
        INFO("Ug1 " << vg1 << ": sheet " << sheet * 1e3 << " mA, model " << model * 1e3);
        CHECK(model == Catch::Approx(sheet).epsilon(0.15));
        squares += (model - sheet) * (model - sheet);
    }

    CHECK(std::sqrt(squares / std::size(family)) < 10.0e-3);
}

namespace
{
    /** A plate-characteristics point: grid volts, plate volts, plate mA. */
    struct SheetPoint
    {
        double ug1, ua, ia;
    };

    struct PlateFamily
    {
        const char* name;
        PentodeModel model;
        double screen;           // the sheet's Ug2 for the whole family
        double boundRms;         // mA
        double korenFormRms;      // mA, the model before the plate-slope term
        std::vector<SheetPoint> points;
    };
}

TEST_CASE("Every pentode lands on its sheet's plate family", "[valve][model]")
{
    // Digitised from docs/specs/tubes at 50-400 V. Beam tetrode curves are cut above the
    // dip (secondary emission isn't modelled). Each valve's rms before the plate-slope
    // term is recorded beside its bound, so a regression shows.
    const std::vector<PlateFamily> families = {
        { "EL34", PentodeModel::el34(), 250.0, 9.0, 22.31,
          { { 0, 50, 217.5 }, { 0, 100, 254.3 }, { 0, 200, 271.2 }, { -5, 50, 147.5 },
            { -5, 100, 178.6 }, { -5, 200, 195.4 }, { -5, 300, 204.0 }, { -10, 50, 87.8 },
            { -10, 100, 110.4 }, { -10, 200, 127.8 }, { -10, 300, 133.2 }, { -10, 400, 140.3 },
            { -15, 50, 39.8 }, { -15, 100, 55.4 }, { -15, 200, 68.1 }, { -15, 300, 73.0 },
            { -15, 400, 78.4 }, { -20, 50, 14.1 }, { -20, 100, 23.4 }, { -20, 200, 30.8 },
            { -20, 300, 33.3 }, { -20, 400, 35.8 }, { -25, 50, 4.2 }, { -25, 100, 7.4 },
            { -25, 200, 12.2 }, { -25, 300, 13.2 }, { -25, 400, 14.3 } } },
        { "6L6GC", PentodeModel::u6l6gc(), 400.0, 10.5, 17.39,
          { { 0, 50, 214.5 }, { 0, 100, 304.2 }, { 0, 200, 325.1 }, { 0, 300, 335.1 }, { 0, 400, 339.7 },
            { -4, 100, 261.8 }, { -4, 200, 278.5 }, { -4, 300, 287.9 }, { -4, 400, 293.9 },
            { -8, 50, 201.6 }, { -8, 100, 226.1 }, { -8, 200, 235.2 }, { -8, 300, 243.4 },
            { -8, 400, 250.4 }, { -12, 50, 173.9 }, { -12, 100, 189.6 }, { -12, 200, 202.8 },
            { -12, 300, 210.4 }, { -12, 400, 215.2 }, { -16, 50, 142.5 }, { -16, 100, 153.6 },
            { -16, 200, 166.2 }, { -16, 300, 173.7 }, { -16, 400, 178.4 }, { -20, 50, 113.0 },
            { -20, 100, 124.1 }, { -20, 200, 135.7 }, { -20, 300, 144.3 }, { -20, 400, 149.8 },
            { -25, 50, 84.6 }, { -25, 100, 88.6 }, { -25, 200, 102.4 }, { -25, 300, 110.6 },
            { -25, 400, 115.8 }, { -30, 50, 57.3 }, { -30, 100, 62.4 }, { -30, 200, 74.8 },
            { -30, 300, 81.6 }, { -30, 400, 85.9 }, { -35, 50, 39.1 }, { -35, 100, 43.0 },
            { -35, 200, 51.4 }, { -35, 300, 58.7 }, { -35, 400, 64.2 }, { -40, 50, 25.2 },
            { -40, 100, 27.1 }, { -40, 200, 33.4 }, { -40, 300, 39.8 }, { -40, 400, 44.4 },
            { -45, 50, 15.6 }, { -45, 100, 16.5 }, { -45, 200, 21.2 }, { -45, 300, 25.3 },
            { -45, 400, 28.9 }, { -50, 50, 9.1 }, { -50, 100, 10.4 }, { -50, 200, 13.7 },
            { -50, 300, 17.1 }, { -50, 400, 20.2 }, { -60, 50, 3.2 }, { -60, 100, 3.1 },
            { -60, 200, 3.3 }, { -60, 300, 5.1 }, { -60, 400, 6.8 } } },
        { "EL84", PentodeModel::el84(), 250.0, 7.0, 18.97,
          { { 0, 50, 132.0 }, { 0, 100, 145.2 }, { -2, 50, 104.8 }, { -2, 100, 114.0 },
            { -2, 200, 120.8 }, { -4, 50, 76.0 }, { -4, 100, 85.2 }, { -4, 200, 90.8 }, { -6, 50, 52.0 },
            { -6, 100, 59.2 }, { -6, 200, 64.0 }, { -8, 50, 32.4 }, { -8, 100, 37.2 }, { -8, 200, 40.8 },
            { -10, 50, 14.8 }, { -10, 100, 19.6 }, { -10, 200, 22.0 } } },
        { "KT66", PentodeModel::kt66(), 400.0, 22.0, 36.67,
          { { 0, 50, 300.5 }, { 0, 100, 373.2 }, { -5, 50, 250.2 }, { -5, 100, 306.9 },
            { -5, 200, 335.9 }, { -5, 300, 351.9 }, { -5, 400, 359.9 }, { -10, 50, 221.7 },
            { -10, 100, 253.9 }, { -10, 200, 275.8 }, { -10, 300, 288.0 }, { -10, 400, 293.6 },
            { -15, 50, 175.5 }, { -15, 100, 197.8 }, { -15, 200, 213.7 }, { -15, 300, 226.5 },
            { -15, 400, 232.7 }, { -20, 200, 168.4 }, { -20, 300, 176.6 }, { -20, 400, 178.5 },
            { -25, 200, 126.0 }, { -25, 300, 130.8 }, { -25, 400, 132.4 }, { -35, 300, 67.0 },
            { -35, 400, 71.6 }, { -50, 400, 18.7 } } },
        { "KT77", PentodeModel::kt77(), 250.0, 9.5, 9.86,
          { { 0, 50, 176.8 }, { -2.5, 50, 169.3 }, { -2.5, 100, 229.3 }, { -2.5, 200, 246.6 },
            { -5, 50, 155.0 }, { -5, 100, 193.4 }, { -5, 200, 208.5 }, { -7.5, 50, 133.7 },
            { -7.5, 100, 161.3 }, { -7.5, 200, 173.2 }, { -10, 50, 113.0 }, { -10, 100, 130.5 },
            { -10, 200, 142.3 }, { -10, 300, 148.2 }, { -12.5, 50, 87.2 }, { -12.5, 100, 103.0 },
            { -12.5, 200, 114.8 }, { -12.5, 300, 120.0 }, { -12.5, 400, 123.3 }, { -15, 50, 69.6 },
            { -15, 100, 80.7 }, { -15, 200, 89.9 }, { -15, 300, 93.5 }, { -15, 400, 96.4 },
            { -17.5, 50, 48.6 }, { -17.5, 100, 59.2 }, { -17.5, 200, 68.4 }, { -17.5, 300, 72.9 },
            { -17.5, 400, 75.3 }, { -20, 50, 31.9 }, { -20, 100, 39.4 }, { -20, 200, 50.0 },
            { -20, 300, 52.4 }, { -20, 400, 54.1 }, { -22.5, 50, 16.3 }, { -22.5, 100, 23.1 },
            { -22.5, 200, 33.6 }, { -22.5, 300, 35.2 }, { -22.5, 400, 36.6 }, { -25, 50, 7.2 },
            { -25, 100, 12.1 }, { -25, 200, 19.4 }, { -25, 300, 21.3 }, { -25, 400, 21.6 } } },
        { "KT88", PentodeModel::kt88(), 300.0, 11.0, 25.44,
          { { -5, 50, 301.1 }, { -5, 100, 317.1 }, { -5, 200, 340.9 }, { -5, 300, 359.8 },
            { -10, 50, 229.3 }, { -10, 100, 245.3 }, { -10, 200, 263.0 }, { -10, 300, 276.3 },
            { -10, 400, 289.2 }, { -15, 50, 162.1 }, { -15, 100, 175.1 }, { -15, 200, 189.4 },
            { -15, 300, 199.8 }, { -15, 400, 208.6 }, { -20, 50, 114.2 }, { -20, 100, 127.8 },
            { -20, 200, 140.8 }, { -20, 300, 150.1 }, { -20, 400, 157.5 }, { -25, 50, 81.1 },
            { -25, 100, 89.6 }, { -25, 200, 103.3 }, { -25, 300, 113.3 }, { -25, 400, 121.5 },
            { -30, 50, 46.8 }, { -30, 100, 54.1 }, { -30, 200, 68.5 }, { -30, 300, 78.6 },
            { -30, 400, 86.8 }, { -35, 100, 23.0 }, { -35, 200, 33.4 }, { -35, 300, 44.0 },
            { -35, 400, 54.1 }, { -40, 100, 3.0 }, { -40, 200, 9.5 }, { -40, 300, 15.2 },
            { -40, 400, 18.7 } } },
    };

    for (const auto& family : families)
    {
        double squares = 0.0;

        for (const auto& [ug1, ua, ia] : family.points)
            squares += std::pow(pentodeAt(family.model, ug1, family.screen, ua).plate * 1.0e3 - ia, 2.0);

        const double rms = std::sqrt(squares / static_cast<double>(family.points.size()));
        INFO(family.name << ": " << rms << " mA rms against the sheet, " << family.korenFormRms
                         << " in Koren's form");
        CHECK(rms < family.boundRms);
        CHECK(rms < family.korenFormRms);
    }
}

TEST_CASE("The refitted pentodes keep their datasheet operating points", "[valve][model]")
{
    // Held exactly by the plate-slope refit.
    struct Point
    {
        const char* name;
        PentodeModel model;
        double ua, ug2, ug1;
        double ia, ig2, s, ri; // mA, mA, mA/V, kohm; 0 where the sheet is silent
    };

    const Point points[] = {
        { "EL34", PentodeModel::el34(), 250.0, 265.0, -13.5, 100.0, 14.9, 11.0, 15.0 },
        { "6L6GC", PentodeModel::u6l6gc(), 250.0, 250.0, -14.0, 72.0, 5.0, 6.0, 22.5 },
        { "EL84", PentodeModel::el84(), 250.0, 250.0, -7.3, 48.0, 5.5, 0.0, 40.0 },
        { "KT66", PentodeModel::kt66(), 250.0, 250.0, -15.0, 85.0, 7.0, 6.0, 22.5 },
        { "KT77", PentodeModel::kt77(), 250.0, 250.0, -13.5, 100.0, 10.0, 10.5, 23.0 },
        { "6V6S", PentodeModel::u6v6(), 250.0, 250.0, -12.5, 45.0, 5.0, 4.1, 50.0 },
    };

    const double h = 1.0e-3;

    for (const auto& p : points)
    {
        INFO(p.name);
        const auto at = pentodeAt(p.model, p.ug1, p.ug2, p.ua);
        CHECK(at.plate * 1.0e3 == Catch::Approx(p.ia).epsilon(1.0e-3));
        CHECK(at.screen * 1.0e3 == Catch::Approx(p.ig2).epsilon(1.0e-3));

        const double s = (pentodeAt(p.model, p.ug1 + h, p.ug2, p.ua).plate
                          - pentodeAt(p.model, p.ug1 - h, p.ug2, p.ua).plate) / (2.0 * h);
        const double ri = 2.0 * h / (pentodeAt(p.model, p.ug1, p.ug2, p.ua + h).plate
                                     - pentodeAt(p.model, p.ug1, p.ug2, p.ua - h).plate);

        if (p.s > 0.0)
            CHECK(s * 1.0e3 == Catch::Approx(p.s).epsilon(1.0e-3));

        CHECK(ri / 1.0e3 == Catch::Approx(p.ri).epsilon(1.0e-3));
    }

    // The EL84's quoted S is out of the form's reach: 11.06 against 11.3.
    const double s84 = (pentodeAt(PentodeModel::el84(), -7.3 + h, 250.0, 250.0).plate
                        - pentodeAt(PentodeModel::el84(), -7.3 - h, 250.0, 250.0).plate) / (2.0 * h);
    CHECK(s84 * 1.0e3 == Catch::Approx(11.3).epsilon(0.03));
}

TEST_CASE("Two pentode checks no fit ever saw", "[valve][model]")
{
    // The 6L6GC's push-pull column on JJ's sheet: Ua = Ug2 270 V, Ug1 -17.5 V,
    // 134 mA for the pair.
    const auto pushPull = pentodeAt(PentodeModel::u6l6gc(), -17.5, 270.0, 270.0);
    CHECK(pushPull.plate * 1.0e3 == Catch::Approx(67.0).epsilon(0.03));

    // The 6V6S sheet only has a triode-connected family, so it reads cathode current.
    const SheetPoint triode[] = {
        { 0, 50, 15.9 }, { 0, 100, 36.4 }, { 0, 150, 61.9 }, { 0, 200, 92.1 }, { 0, 250, 126.3 },
        { -10, 100, 5.8 }, { -10, 150, 20.5 }, { -10, 200, 40.5 }, { -10, 250, 64.8 },
        { -10, 300, 92.5 }, { -20, 200, 9.3 }, { -20, 250, 24.4 }, { -20, 300, 44.5 },
        { -20, 350, 67.9 }, { -20, 400, 93.7 }, { -30, 250, 3.8 }, { -30, 300, 13.2 },
        { -30, 350, 27.8 }, { -30, 400, 48.7 }, { -30, 450, 76.0 }, { -40, 350, 6.1 },
        { -40, 400, 14.9 }, { -40, 450, 31.7 }
    };

    double squares = 0.0;

    for (const auto& [ug1, ua, ia] : triode)
    {
        const auto c = pentodeAt(PentodeModel::u6v6(), ug1, ua, ua);
        squares += std::pow((c.plate + c.screen) * 1.0e3 - ia, 2.0);
    }

    const double rms = std::sqrt(squares / static_cast<double>(std::size(triode)));
    INFO("6V6S triode-connected: " << rms << " mA rms");
    CHECK(rms < 4.5);
}

TEST_CASE("The plate-slope term's derivative is the derivative of its current", "[valve][model]")
{
    // The Jacobian must match the current, through, across and below the knee.
    for (const auto& model : { PentodeModel::el34(), PentodeModel::el84(), PentodeModel::kt88(),
                               PentodeModel::u6l6gc() })
    {
        for (const double vp : { -5.0, 0.5, 3.0, 12.0, 40.0, 250.0, 480.0 })
        {
            Pentode p {};
            p.model = model;
            DeviceLinearisation out {};
            double v[3] = { -10.0, 250.0, vp };
            linearise(p, v, out);

            const double h = 1.0e-4;
            const auto plateAt = [&](double g1, double g2, double a)
            { return pentodeAt(model, g1, g2, a).plate; };

            INFO("Vp " << vp);
            CHECK(out.jacobian[8] == Catch::Approx((plateAt(-10.0, 250.0, vp + h) - plateAt(-10.0, 250.0, vp - h)) / (2.0 * h))
                                         .epsilon(1.0e-5).margin(1.0e-12));
            CHECK(out.jacobian[6] == Catch::Approx((plateAt(-10.0 + h, 250.0, vp) - plateAt(-10.0 - h, 250.0, vp)) / (2.0 * h))
                                         .epsilon(1.0e-5).margin(1.0e-12));
            CHECK(out.jacobian[7] == Catch::Approx((plateAt(-10.0, 250.0 + h, vp) - plateAt(-10.0, 250.0 - h, vp)) / (2.0 * h))
                                         .epsilon(1.0e-5).margin(1.0e-12));
        }
    }
}

//==============================================================================
// Diodes
//==============================================================================

TEST_CASE("The 1N4001 reproduces its card and the Diodes Inc curve", "[diode][model]")
{
    const auto m = DiodeModel::d1n4001();

    // Motorola's card through the engine.
    CHECK(diodeVoltageAt(m, 100.0e-6) == Catch::Approx(0.455).margin(0.003));
    CHECK(diodeVoltageAt(m, 1.0e-3) == Catch::Approx(0.573).margin(0.003));

    // The Diodes Inc typical curve (docs/specs): 0.70 V at 10 mA.
    CHECK(diodeVoltageAt(m, 10.0e-3) == Catch::Approx(0.70).margin(0.015));

    // Softer than a 1N4148: lower at small currents, wider span over three decades.
    const auto sig = DiodeModel::d1n4148();
    CHECK(diodeVoltageAt(m, 10.0e-6) < diodeVoltageAt(sig, 10.0e-6));
    CHECK(diodeVoltageAt(m, 10.0e-3) - diodeVoltageAt(m, 10.0e-6)
          > diodeVoltageAt(sig, 10.0e-3) - diodeVoltageAt(sig, 10.0e-6));
}

TEST_CASE("The 1N4148, 1N5817 and LEDs land on their makers' curves", "[diode][model]")
{
    // Makers' typical forward curves at 25 C (docs/specs/diodes): Vishay 1N4148 and
    // 1N5817, Kingbright WP7113 red (GaAsP), green (GaP) and blue (InGaN).
    struct Point { double current, volts, margin; };

    const auto check = [](const char* name, const DiodeModel& model, std::initializer_list<Point> points)
    {
        for (const auto& p : points)
        {
            const double v = diodeVoltageAt(model, p.current);
            INFO(name << " at " << p.current << " A: " << v << " V, sheet " << p.volts);
            CHECK(v == Catch::Approx(p.volts).margin(p.margin));
        }
    };

    check("1N4148", DiodeModel::d1n4148(),
          { { 10.0e-6, 0.380, 0.005 }, { 100.0e-6, 0.497, 0.005 }, { 1.0e-3, 0.609, 0.005 },
            { 10.0e-3, 0.729, 0.005 }, { 100.0e-3, 0.844, 0.005 } });

    // Vishay's curve starts at 10 mA; below that the fit is extrapolation.
    check("1N5817", DiodeModel::schottky(), { { 10.0e-3, 0.172, 0.003 }, { 100.0e-3, 0.250, 0.003 }, { 1.0, 0.392, 0.003 } });

    check("WP7113ID", DiodeModel::redLed(), { { 1.0e-3, 1.645, 0.02 }, { 5.0e-3, 1.802, 0.02 }, { 10.0e-3, 1.903, 0.02 } });
    check("WP7113GD", DiodeModel::greenLed(), { { 1.0e-3, 1.857, 0.02 }, { 5.0e-3, 1.942, 0.02 }, { 10.0e-3, 2.033, 0.02 } });
    check("WP7113QBC", DiodeModel::blueLed(), { { 1.0e-3, 2.571, 0.03 }, { 5.0e-3, 2.806, 0.03 }, { 10.0e-3, 3.000, 0.03 } });
}

TEST_CASE("The Zeners land on Vishay's BZX55 sheet", "[diode][model][zener]")
{
    // docs/specs/diodes/BZX55-Vishay.PDF: the forward curve (Fig. 6, 25 C) and the
    // capacitance (Fig. 5, at 2 V reverse) digitised from the vector paths; the
    // breakdown against the table, which gives limits only. Below 5 V Fig. 5 leaves the
    // chart straight up at 4.92 V, to 175 pF, and the low parts take that.
    struct Part { double vz, capacitancePf, impedanceAt5mA, impedanceAt1mA, leakageVolts, leakageAmps; };

    for (const auto& part : { Part { 3.9, 174.7, 85.0, 600.0, 1.0, 2.0e-6 }, Part { 4.3, 174.7, 75.0, 600.0, 1.0, 1.0e-6 },
                              Part { 5.1, 104.4, 35.0, 550.0, 1.0, 0.1e-6 }, Part { 9.1, 46.1, 10.0, 50.0, 6.8, 0.1e-6 } })
    {
        const auto m = DiodeModel::zener(part.vz);
        INFO("BZX55C" << part.vz);

        // Fig. 6 is one curve for the whole series.
        for (const auto& [current, volts] : { std::pair { 1.0e-6, 0.520 }, { 10.0e-6, 0.589 }, { 100.0e-6, 0.654 },
                                               { 1.0e-3, 0.716 }, { 10.0e-3, 0.786 } })
        {
            const double v = diodeVoltageAt(m, current);
            INFO("forward at " << current << " A: " << v << " V, sheet " << volts);
            CHECK(v == Catch::Approx(volts).margin(0.004));
        }

        CHECK(m.junctionCapacitance == Catch::Approx(part.capacitancePf * 1.0e-12).margin(1.5e-12));
        CHECK(m.transitTime == Catch::Approx(50.1e-9)); // Diodes Inc's BZT52 card

        // Reverse, straight from the device: the voltage where it carries a current,
        // and its slope there.
        Diode d {};
        d.model = m;
        const auto reverseAt = [&](double current, double& resistance)
        {
            double lo = -part.vz - 2.0, hi = -part.vz + 2.0, i = 0.0, g = 0.0;
            for (int k = 0; k < 100; ++k)
            {
                const double mid = 0.5 * (lo + hi);
                evaluateDiode(d, mid, i, g);
                (-i > current ? lo : hi) = mid;
            }
            evaluateDiode(d, lo, i, g);
            resistance = 1.0 / g;
            return -lo;
        };

        double r5 = 0.0, r1 = 0.0;
        CHECK(reverseAt(5.0e-3, r5) == Catch::Approx(part.vz).margin(0.005)); // nominal at IZT
        reverseAt(1.0e-3, r1);
        INFO("ZZ " << r5 << " ohms, ZZK " << r1 << " ohms");
        CHECK(r5 < part.impedanceAt5mA);
        CHECK(r1 < part.impedanceAt1mA);

        double i = 0.0, g = 0.0;
        evaluateDiode(d, -part.leakageVolts, i, g);
        CHECK(-i < part.leakageAmps);
    }

    // Tunnelling below about 6 V, avalanche above: the low-voltage part's knee is the
    // softer, as the table's knee impedances say.
    CHECK(DiodeModel::zener(5.1).breakdownEmission > DiodeModel::zener(9.1).breakdownEmission);
}

TEST_CASE("The BAT41 reproduces ST's typical curve", "[diode][model]")
{
    // Figure 1 of the ST sheet (docs/specs), 25 C, and VF 0.4 V typ at 1 mA.
    const auto m = DiodeModel::bat41();
    const std::pair<double, double> curve[] = { { 10.0e-6, 0.20 }, { 100.0e-6, 0.27 }, { 1.0e-3, 0.39 },
                                                { 10.0e-3, 0.69 }, { 100.0e-3, 0.90 } };

    for (const auto& [current, volts] : curve)
    {
        INFO("at " << current << " A");
        CHECK(diodeVoltageAt(m, current) == Catch::Approx(volts).margin(0.012));
    }

    // 1 V max at 200 mA.
    CHECK(diodeVoltageAt(m, 200.0e-3) < 1.0);

    // The Schottky behind its 35 ohms, with the guard ring across both.
    Circuit circuit;
    circuit.addDiode("a", "gnd", m);
    circuit.addResistor("in", "a", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("a");
    circuit.prepare(sampleRate);
    CHECK(circuit.getPortCount() == 2);
}

TEST_CASE("The BAT41 sits inside General Semiconductor's sheet", "[diode][model]")
{
    // docs/specs/diodes/BAT41.PDF: table only, same figures as ST's. 0.40 V typ at 1 mA.
    const auto m = DiodeModel::bat41();

    CHECK(diodeVoltageAt(m, 1.0e-3) == Catch::Approx(0.40).margin(0.015));
    CHECK(diodeVoltageAt(m, 1.0e-3) < 0.45);
    CHECK(diodeVoltageAt(m, 200.0e-3) < 1.0);

    // 100 nA max at 50 V reverse, 25 C. Leakage is the two saturation currents (not
    // fitted).
    CHECK(m.saturationCurrent + m.guardRingSaturationCurrent < 100.0e-9);
}

TEST_CASE("The BAT46 reproduces ST's typical curve", "[diode][model]")
{
    // ST's sheet (docs/specs/diodes/BAT46-ST.PDF), Fig. 1-1 (to 20 mA) and 1-2 (above),
    // 25 C. Weighted to 20 mA and below.
    const auto m = DiodeModel::bat46();
    const std::pair<double, double> low[] = { { 0.1e-3, 0.188 }, { 0.2e-3, 0.208 }, { 0.5e-3, 0.235 },
                                              { 1.0e-3, 0.259 }, { 2.0e-3, 0.287 }, { 3.0e-3, 0.307 },
                                              { 5.0e-3, 0.340 }, { 8.0e-3, 0.380 }, { 10.0e-3, 0.405 },
                                              { 15.0e-3, 0.462 }, { 20.0e-3, 0.515 } };

    for (const auto& [current, volts] : low)
    {
        INFO("at " << current * 1e3 << " mA");
        CHECK(diodeVoltageAt(m, current) == Catch::Approx(volts).margin(0.003));
    }

    // Above 20 mA the guard ring's unmodelled resistance shows: 16 mV high at 100 mA,
    // 24 mV low at 500 mA.
    const std::pair<double, double> high[] = { { 50.0e-3, 0.685 }, { 100.0e-3, 0.756 },
                                               { 200.0e-3, 0.820 }, { 500.0e-3, 0.914 } };

    for (const auto& [current, volts] : high)
    {
        INFO("at " << current * 1e3 << " mA");
        CHECK(diodeVoltageAt(m, current) == Catch::Approx(volts).margin(0.03));
    }

    // General Semiconductor's sheet (docs/specs/diodes) gives maxima only.
    CHECK(diodeVoltageAt(m, 0.1e-3) < 0.25);
    CHECK(diodeVoltageAt(m, 10.0e-3) < 0.45);
    CHECK(diodeVoltageAt(m, 250.0e-3) < 1.0);

    // Leakage (not fitted): ST Fig. 2 reads about 0.28 uA at 10 V; 0.5 max at 1.5 V.
    const double leakage = m.saturationCurrent + m.guardRingSaturationCurrent;
    CHECK(leakage > 0.1e-6);
    CHECK(leakage < 0.5e-6);
}

TEST_CASE("A BAT46 clips lowest of the small-signal diodes", "[diode][model]")
{
    // 0.26 V at 1 mA: under a BAT41 and germanium; only the 1N5817 (1 A die) goes lower.
    const double bat46 = diodeVoltageAt(DiodeModel::bat46(), 1.0e-3);
    CHECK(bat46 < diodeVoltageAt(DiodeModel::bat41(), 1.0e-3) - 0.1);
    CHECK(bat46 < diodeVoltageAt(DiodeModel::germanium(), 1.0e-3));
    CHECK(bat46 > diodeVoltageAt(DiodeModel::schottky(), 1.0e-3));

    // But a sharper knee than germanium: less voltage across three decades.
    const auto span = [](const DiodeModel& model)
    { return diodeVoltageAt(model, 1.0e-3) - diodeVoltageAt(model, 1.0e-6); };

    CHECK(span(DiodeModel::bat46()) < span(DiodeModel::germanium()));
}

TEST_CASE("A diode stack scales its series resistance and guard ring", "[diode]")
{
    // n diodes in series drop n times one.
    Circuit circuit;
    const auto source = circuit.addVoltageSource("a", "gnd", 0.5);
    circuit.addDiode("a", "gnd", DiodeModel::bat41(), 2);
    circuit.addResistor("in", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("a");
    circuit.prepare(sampleRate);

    const double one = diodeVoltageAt(DiodeModel::bat41(), 5.0e-3);
    circuit.setVoltage(source, 2.0 * one);
    circuit.reset();

    CHECK(-circuit.getSourceCurrent(source) == Catch::Approx(5.0e-3).epsilon(0.002));
}

TEST_CASE("A slow diode gives back its stored charge when it turns off", "[diode][model][charge]")
{
    // Forward at +5 V through 1k, then switched to -5 V. A diode with transit time gives
    // back TT * If of charge before blocking.
    auto recovered = [](const DiodeModel& model, double& forwardCurrent)
    {
        Circuit circuit;
        circuit.addResistor("in", "a", 1000.0);
        circuit.addDiode("a", "gnd", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("a");
        circuit.prepare(sampleRate);

        float va = 0.0f;

        for (int i = 0; i < 2400; ++i)
            va = circuit.process(5.0f);

        forwardCurrent = (5.0 - va) / 1000.0;

        double charge = 0.0;

        for (int i = 0; i < 480; ++i)
        {
            va = circuit.process(-5.0f);
            const double diodeCurrent = (-5.0 - va) / 1000.0;

            if (diodeCurrent < 0.0)
                charge -= diodeCurrent / sampleRate;
        }

        return charge;
    };

    // Without depletion capacitance, which would ring at Nyquist on a step this sharp:
    // this tests transit time only.
    auto slow = DiodeModel::d1n4001();
    slow.junctionCapacitance = 0.0;
    auto instant = slow;
    instant.transitTime = 0.0;

    double ifSlow = 0.0, ifInstant = 0.0;
    const double qSlow = recovered(slow, ifSlow);
    const double qInstant = recovered(instant, ifInstant);

    INFO("recovered " << qSlow * 1.0e9 << " nC against TT*If " << slow.transitTime * ifSlow * 1.0e9
                      << " nC; without the transit time " << qInstant * 1.0e9 << " nC");

    // Backward Euler conserves the charge exactly.
    CHECK(qSlow == Catch::Approx(slow.transitTime * ifSlow).epsilon(0.1));
    CHECK(qInstant < 0.05 * qSlow);

    // At rest they are the same diode.
    CHECK(ifSlow == Catch::Approx(ifInstant).epsilon(1.0e-5));
}

TEST_CASE("A 1N4001 clipper sounds different from a charge-free one", "[diode][charge]")
{
    // Stored charge matters in a clipper: at 5 kHz it carries a real fraction of the
    // current.
    auto clip = [](const DiodeModel& model)
    {
        Circuit circuit;
        circuit.addResistor("in", "out", 10000.0);
        circuit.addDiode("out", "gnd", model);
        circuit.addDiode("gnd", "out", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);

        std::vector<float> out;
        const double step = 2.0 * std::numbers::pi * 5000.0 / sampleRate;

        for (int i = 0; i < 4800; ++i)
            out.push_back(circuit.process(static_cast<float>(2.0 * std::sin(step * i))));

        CHECK(circuit.getNonConvergenceCount() == 0);
        return out;
    };

    auto instant = DiodeModel::d1n4001();
    instant.transitTime = 0.0;

    const auto a = clip(DiodeModel::d1n4001());
    const auto b = clip(instant);

    double difference = 0.0, level = 0.0;

    for (size_t i = 2400; i < a.size(); ++i)
    {
        difference += (a[i] - b[i]) * (a[i] - b[i]);
        level += b[i] * b[i];
    }

    CHECK(std::sqrt(difference / level) > 0.01);
}

TEST_CASE("A BAT41 clips between a germanium diode and a 1N4148", "[diode][model]")
{
    const double bat = diodeVoltageAt(DiodeModel::bat41(), 1.0e-3);
    CHECK(bat > diodeVoltageAt(DiodeModel::germanium(), 1.0e-3));
    CHECK(bat < diodeVoltageAt(DiodeModel::d1n4148(), 1.0e-3));
}

//==============================================================================
// MOSFETs
//==============================================================================

namespace
{
    /** Vds that carries `id` at `vgs` (on-resistance is this over id), as a magnitude. */
    double drainVoltageFor(const MosfetModel& model, double vgs, double id)
    {
        const double p = model.polaritySign();
        double lo = 0.0, hi = 60.0;

        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (p * drainCurrent(model, p * vgs, p * mid) < id ? lo : hi) = mid;
        }

        return lo;
    }

    /** Body diode forward voltage at `current`, gate at the source. */
    double bodyDiodeVoltageAt(const MosfetModel& model, double current)
    {
        const double p = model.polaritySign();
        double lo = 0.0, hi = 2.0;

        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (-p * drainCurrent(model, 0.0, -p * mid) < current ? lo : hi) = mid;
        }

        return lo;
    }
}

TEST_CASE("The JFETs are InterFET's measured models", "[jfet][model]")
{
    // InterFET's SPICE models in ngspice-47, drain 10 V up: Idss, and the gate voltage
    // where drain current falls to 1 uA.
    struct Reference { const char* name; JfetModel model; double idss, cutoff; };

    const Reference references[] = {
        { "J201", JfetModel::j201(), 0.393e-3, -0.650 },
        { "2N5457", JfetModel::n2n5457(), 2.124e-3, -1.220 },
        { "2N5485", JfetModel::n2n5485(), 6.137e-3, -2.130 },
        { "2N5460", JfetModel::p2n5460(), 2.715e-3, 2.110 },
    };

    for (const auto& r : references)
    {
        const double p = r.model.polaritySign();
        Jfet j {};
        j.model = r.model;

        const auto drain = [&](double vgs)
        {
            DeviceLinearisation out {};
            double v[2] = { vgs, p * 9.96 }; // ngspice's Vds, through its 10 ohm drain resistor
            linearise(j, v, out);
            return p * out.current[1];
        };

        INFO(r.name << ": Idss " << drain(0.0) * 1e3 << " mA");
        CHECK(drain(0.0) == Catch::Approx(r.idss).epsilon(0.003));

        // 1 uA at the cutoff, within the sweep's 10 mV step.
        CHECK(drain(r.cutoff - p * 0.01) < 1.0e-6);
        CHECK(drain(r.cutoff + p * 0.01) > 1.0e-6);
    }
}

TEST_CASE("The 2N7000 reproduces Vishay's sheet", "[mosfet][model]")
{
    // Vishay Siliconix 70226, typical column and curves, from the PDF's vector paths.
    const auto m = MosfetModel::n2N7000();

    // Vgs(th), drain strapped to gate: 2.1 V at 1 mA, 2.0 V at 0.25 mA (fixes n).
    CHECK(thresholdAt(m, 1.0e-3) == Catch::Approx(2.10).margin(0.005));
    CHECK(thresholdAt(m, 0.25e-3) == Catch::Approx(2.00).margin(0.005));

    // Output characteristics, Vds 4.5 V, Vgs 3 to 7 V.
    const std::pair<double, double> output[] = { { 3.0, 0.072 }, { 4.0, 0.241 }, { 5.0, 0.500 },
                                                 { 6.0, 0.789 }, { 7.0, 1.078 } };

    for (const auto& [vgs, id] : output)
    {
        INFO("Vgs " << vgs << ": " << drainCurrent(m, vgs, 4.5) << " A");
        CHECK(drainCurrent(m, vgs, 4.5) == Catch::Approx(id).epsilon(0.07));
    }

    // Also in the triode region, where source resistance shows.
    CHECK(drainCurrent(m, 5.0, 1.0) == Catch::Approx(0.296).epsilon(0.1));
    CHECK(drainCurrent(m, 7.0, 1.0) == Catch::Approx(0.386).epsilon(0.05));

    // rDS(on): 2.21 ohm at Vgs 10 V, 0.1 A; 3.2 at Vgs 5 V, 50 mA.
    CHECK(drainVoltageFor(m, 10.0, 0.1) / 0.1 == Catch::Approx(2.21).epsilon(0.1));
    CHECK(drainVoltageFor(m, 5.0, 0.05) / 0.05 == Catch::Approx(3.20).epsilon(0.02));

    // gfs 100 mS minimum for both numbers, at Vds 10 V and 0.2 A.
    CHECK(transconductanceAt(m, 0.2, 10.0) > 0.100);

    // gos 0.5 mS typ at Vds 5 V, 50 mA.
    const double vgs50 = [&m]
    {
        double lo = 0.0, hi = 10.0;
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (drainCurrent(m, mid, 5.0) < 0.05 ? lo : hi) = mid;
        }
        return lo;
    }();
    const double gos = (drainCurrent(m, vgs50, 5.0 + 1.0e-4) - drainCurrent(m, vgs50, 5.0 - 1.0e-4)) / 2.0e-4;
    CHECK(gos == Catch::Approx(0.5e-3).epsilon(0.05));

    // Source-drain diode, 25 C curve: 0.597 / 0.666 / 0.754 V at 1 / 10 / 100 mA.
    CHECK(bodyDiodeVoltageAt(m, 1.0e-3) == Catch::Approx(0.597).margin(0.008));
    CHECK(bodyDiodeVoltageAt(m, 10.0e-3) == Catch::Approx(0.666).margin(0.008));
    CHECK(bodyDiodeVoltageAt(m, 100.0e-3) == Catch::Approx(0.754).margin(0.008));

    // Capacitance curve at Vdg 2.4 V, gate at the source: Ciss 26.5, Crss 6.3.
    CHECK(m.capGateDrain == Catch::Approx(6.3e-12).epsilon(0.02));
    CHECK(m.capGateSource + m.capGateDrain == Catch::Approx(26.5e-12).epsilon(0.02));
}

TEST_CASE("The BS250 reproduces Vishay's sheet", "[mosfet][model]")
{
    // Vishay Siliconix 70209 (TP0610L/T, VP0610L/T, BS250), typical curves only (the
    // typical column is the TP0610L's).
    const auto m = MosfetModel::pBS250();
    REQUIRE(m.channel == MosfetModel::Channel::P);

    auto magnitude = [&m](double vgs, double vds) { return -drainCurrent(m, -vgs, -vds); };

    // Transfer characteristic, 25 C, from where the line can be read.
    const std::pair<double, double> transfer[] = { { 4.0, 0.0645 }, { 4.5, 0.156 }, { 5.0, 0.276 },
                                                   { 6.0, 0.573 }, { 7.0, 0.879 } };

    for (const auto& [vgs, id] : transfer)
    {
        INFO("-Vgs " << vgs << ": " << magnitude(vgs, 10.0) << " A");
        CHECK(magnitude(vgs, 10.0) == Catch::Approx(id).epsilon(0.22));
    }

    // Output characteristics at -Vds 4.5 V: the model is 7-20% under mid-family, on it at
    // the ends.
    const std::pair<double, double> output[] = { { 4.0, 0.064 }, { 5.0, 0.270 }, { 6.0, 0.551 },
                                                 { 7.0, 0.796 }, { 8.0, 0.943 } };

    for (const auto& [vgs, id] : output)
    {
        INFO("-Vgs " << vgs << ": " << magnitude(vgs, 4.5) << " A");
        CHECK(magnitude(vgs, 4.5) == Catch::Approx(id).epsilon(0.21));
    }

    // rDS(on) at -Vgs 10 V: 2.88 ohm at 0.2 A off the curve (14 max).
    CHECK(drainVoltageFor(m, 10.0, 0.2) / 0.2 == Catch::Approx(2.88).epsilon(0.06));
    CHECK(drainVoltageFor(m, 10.0, 0.2) / 0.2 < 14.0);

    // Inside the BS250's threshold window, -1 to -3.5 V, where 1 mA flows.
    CHECK(thresholdAt(m, 1.0e-3) > 1.0);
    CHECK(thresholdAt(m, 1.0e-3) < 3.5);

    // Source-drain diode: 0.644 / 0.717 / 0.831 V at 1 / 10 / 100 mA.
    CHECK(bodyDiodeVoltageAt(m, 1.0e-3) == Catch::Approx(0.644).margin(0.012));
    CHECK(bodyDiodeVoltageAt(m, 10.0e-3) == Catch::Approx(0.717).margin(0.012));
    CHECK(bodyDiodeVoltageAt(m, 100.0e-3) == Catch::Approx(0.831).margin(0.012));

    // Capacitance curve at Vdg 1.5 V: Ciss 26.0, Crss 14.2.
    CHECK(m.capGateDrain == Catch::Approx(14.2e-12).epsilon(0.02));
    CHECK(m.capGateSource + m.capGateDrain == Catch::Approx(26.0e-12).epsilon(0.02));
}

TEST_CASE("A MOSFET's source resistance solves its own equation", "[mosfet][model]")
{
    // The stamped current must solve I = f(Vgs - I Rs, Vds - I Rs) for the bare channel f,
    // in every region and direction.
    auto withRs = MosfetModel::n2N7000();
    auto bare = withRs;
    bare.sourceResistance = 0.0;

    for (const double vgs : { 1.5, 2.1, 3.0, 6.0, 12.0 })
        for (const double vds : { -0.5, -0.05, 0.02, 0.4, 5.0 })
        {
            double i = 0.0, g = 0.0, d = 0.0;
            evaluateMosfetTerminals(withRs, vgs, vds, i, g, d);

            const double rs = withRs.sourceResistance;
            double f = 0.0;
            evaluateMosfetChannelEitherWay(bare, vgs - i * rs, vds - i * rs, f, g, d);

            INFO("Vgs " << vgs << ", Vds " << vds << ": " << i << " A against " << f);
            CHECK(i == Catch::Approx(f).epsilon(1.0e-10).margin(1.0e-15));
        }

    // Zero resistance changes nothing, bit for bit.
    for (const double vgs : { 1.9, 2.4, 5.0 })
        for (const double vds : { -0.3, 0.1, 5.0 })
        {
            double a = 0.0, ag = 0.0, ad = 0.0, b = 0.0, bg = 0.0, bd = 0.0;
            evaluateMosfetTerminals(bare, vgs, vds, a, ag, ad);
            evaluateMosfetChannelEitherWay(bare, vgs, vds, b, bg, bd);
            CHECK(juce::exactlyEqual(a, b));
            CHECK(juce::exactlyEqual(ag, bg));
            CHECK(juce::exactlyEqual(ad, bd));
        }

    // At 1 mA Rs barely matters; at half an amp it removes a third of the current.
    CHECK(drainCurrent(withRs, 2.1, 5.0) == Catch::Approx(drainCurrent(bare, 2.1, 5.0)).epsilon(0.03));
    CHECK(drainCurrent(withRs, 5.0, 5.0) < 0.75 * drainCurrent(bare, 5.0, 5.0));
}

TEST_CASE("A MOSFET is off at zero gate voltage and exponential below threshold", "[mosfet][model]")
{
    const auto m = MosfetModel::n2N7000();

    CHECK(std::abs(drainCurrent(m, 0.0, 5.0)) < 1.0e-9);

    // Below threshold a decade of current costs n * Vt * ln(10), about 89 mV.
    const double decade = thresholdAt(m, 10.0e-6) - thresholdAt(m, 1.0e-6);
    CHECK(decade == Catch::Approx(m.subthresholdSlope * m.thermalVoltage * std::log(10.0)).epsilon(0.05));
}

TEST_CASE("A MOSFET channel is symmetric and smooth through Vds = 0", "[mosfet][model]")
{
    // The channel alone: the body diode and source resistance are one-sided, so removed
    // here (source resistance is checked below).
    auto m = MosfetModel::n2N7000();
    m.bodyDiodeSaturationCurrent = 0.0;
    m.sourceResistance = 0.0;

    // Swapping drain and source negates the current.
    for (const double vgs : { 1.8, 2.2, 3.0 })
        for (const double vds : { 0.01, 0.1, 0.5 })
        {
            // gmin is the only term left that is not the channel.
            INFO("Vgs " << vgs << ", Vds " << vds);
            CHECK(drainCurrent(m, vgs - vds, -vds)
                  == Catch::Approx(-drainCurrent(m, vgs, vds)).epsilon(1.0e-6).margin(1.0e-11));
        }

    // The slope either side of zero agrees.
    auto slope = [&m](double vds) { return (drainCurrent(m, 2.5, vds + 1.0e-9) - drainCurrent(m, 2.5, vds - 1.0e-9)) / 2.0e-9; };
    CHECK(slope(-1.0e-6) == Catch::Approx(slope(1.0e-6)).epsilon(1.0e-3));

    // With the source resistance back, still smooth through zero.
    auto full = MosfetModel::n2N7000();
    full.bodyDiodeSaturationCurrent = 0.0;
    auto fullSlope = [&full](double vds) { return (drainCurrent(full, 2.5, vds + 1.0e-9) - drainCurrent(full, 2.5, vds - 1.0e-9)) / 2.0e-9; };
    CHECK(fullSlope(-1.0e-6) == Catch::Approx(fullSlope(1.0e-6)).epsilon(1.0e-3));
}

TEST_CASE("The MOSFET Jacobian is the derivative of what it stamps", "[mosfet][model]")
{
    for (const auto& model : { MosfetModel::n2N7000(), MosfetModel::pBS250() })
    {
        Mosfet m {};
        m.model = model;
        const double p = model.polaritySign();

        // Subthreshold, moderate and strong inversion; triode, saturation, reverse, and body
        // diode conduction. Up to 10 V, where the source resistance's implicit slopes matter
        // most.
        for (const double vgs : { 0.5, 1.9, 2.1, 2.4, 3.3, 4.0, 6.0, 10.0 })
            for (const double vds : { -0.7, -0.2, 0.05, 0.3, 5.0 })
                checkJacobian(m, p * vgs, p * vds);
    }
}

TEST_CASE("A MOSFET's body diode conducts when the drain is pulled below the source", "[mosfet][model]")
{
    const auto m = MosfetModel::n2N7000();

    // Gate at source, drain 0.6 V below: channel off, body diode conducting milliamps out
    // of the drain.
    const double id = drainCurrent(m, 0.0, -0.6);
    CHECK(id < -0.5e-3);
    CHECK(id > -5.0e-3);

    // A P-channel part's is the other way round.
    CHECK(drainCurrent(MosfetModel::pBS250(), 0.0, 0.6) > 0.1e-3);
}

namespace
{
    /** Drain-feedback common-source stage: Rd to the rail, gate at half the drain via two
        1M resistors, signal onto the gate through a capacitor. Self-biasing. */
    Circuit makeMosfetStage(const MosfetModel& model, Circuit::SolverStrategy strategy)
    {
        const bool p = model.channel == MosfetModel::Channel::P;

        Circuit circuit;
        circuit.addVoltageSource("rail", "gnd", 9.0);
        circuit.addResistor(p ? "gnd" : "rail", "d", 10000.0);
        circuit.addResistor("d", "g", 1.0e6);
        circuit.addResistor("g", p ? "rail" : "gnd", 1.0e6);
        circuit.addCapacitor("in", "g", 1.0e-6);
        circuit.addMosfet("d", "g", p ? "rail" : "gnd", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("d");
        circuit.setSolverStrategy(strategy);
        circuit.prepare(sampleRate);
        return circuit;
    }
}

TEST_CASE("A MOSFET stage biases itself and amplifies", "[circuit][mosfet]")
{
    auto circuit = makeMosfetStage(MosfetModel::n2N7000(), Circuit::SolverStrategy::Auto);
    REQUIRE(circuit.foundOperatingPoint());

    // Vd = 9 - 10k Id with Vg = Vd / 2: drain at about 4.1 V.
    const double vd = circuit.getNodeVoltage("d");
    INFO("drain at " << vd << " V");
    CHECK(vd > 3.6);
    CHECK(vd < 4.6);

    // The gate is insulated, so the divider is exactly a divider.
    CHECK(circuit.getNodeVoltage("g") == Catch::Approx(0.5 * vd).epsilon(1.0e-6));

    // About gm * 10k, inverting: gm near 6.5 mS at 0.5 mA (with n fitted to 2.0).
    // Measured 63.8.
    circuit.setOutputOffsetToOperatingPoint();
    const float gain = amplitudeOf(circuit, 1000.0, 1.0e-3f, sampleRate, 0.5) / 1.0e-3f;
    INFO("gain " << gain);
    CHECK(gain > 50.0f);
    CHECK(gain < 110.0f);
    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("A P-channel MOSFET stage mirrors an N-channel one", "[circuit][mosfet]")
{
    auto circuit = makeMosfetStage(MosfetModel::pBS250(), Circuit::SolverStrategy::Auto);
    REQUIRE(circuit.foundOperatingPoint());

    // The P-channel stage: the BS250 needs about 3.1 V of drive at 0.3 mA (the 2N7000
    // 2.05 V), so the drain sits about 6.2 V under the rail, at about 2.8 V.
    const double vd = circuit.getNodeVoltage("d");
    INFO("drain at " << vd << " V");
    CHECK(vd > 2.3);
    CHECK(vd < 3.3);

    circuit.setOutputOffsetToOperatingPoint();
    CHECK(amplitudeOf(circuit, 1000.0, 1.0e-3f, sampleRate, 0.5) > 20.0e-3f);
}

TEST_CASE("DK and full Newton agree on a MOSFET stage", "[circuit][mosfet][dk]")
{
    auto dk = makeMosfetStage(MosfetModel::n2N7000(), Circuit::SolverStrategy::DiscreteK);
    auto full = makeMosfetStage(MosfetModel::n2N7000(), Circuit::SolverStrategy::FullNewton);

    const double step = 2.0 * std::numbers::pi * 700.0 / sampleRate;
    double worst = 0.0;

    for (int i = 0; i < 2400; ++i)
    {
        const auto in = static_cast<float>(1.5 * std::sin(step * i)); // well into clipping
        worst = std::max(worst, (double) std::abs(dk.process(in) - full.process(in)));
    }

    CHECK(worst < 1.0e-4);
}

//==============================================================================
// The LM386
//==============================================================================

namespace
{
    /** An LM386 with in+ driven through `sourceOhms` (zero for direct), in- grounded, output
        unloaded, and `between18` across pins 1 and 8: nothing, a capacitor, or R and C in
        series. */
    Circuit makeLm386(double supply, double rate, double sourceOhms, double gainOhms, double gainFarads,
                      double loadOhms = 0.0)
    {
        Circuit circuit;

        if (sourceOhms > 0.0)
            circuit.addResistor("in", "inp", sourceOhms);

        circuit.addPowerAmp("U1", sourceOhms > 0.0 ? "inp" : "in", "gnd", "out", "g1", "g8",
                            PowerAmpModel::lm386().withSupply(supply));

        if (gainFarads > 0.0)
        {
            if (gainOhms > 0.0)
            {
                circuit.addResistor("g1", "gmid", gainOhms);
                circuit.addCapacitor("gmid", "g8", gainFarads);
            }
            else
            {
                circuit.addCapacitor("g1", "g8", gainFarads);
            }
        }

        if (loadOhms > 0.0)
        {
            circuit.addCapacitor("out", "spk", 220.0e-6);
            circuit.addResistor("spk", "gnd", loadOhms);
        }

        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(rate);
        return circuit;
    }
}

TEST_CASE("An LM386 rests just above half its supply", "[circuit][poweramp]")
{
    // (Vs + 2 Vbe) / 2 with the model's 1.1 V: 5.05 V on 9 V, 3.55 on 6.
    for (const double supply : { 6.0, 9.0, 12.0 })
    {
        auto circuit = makeLm386(supply, sampleRate, 0.0, 0.0, 0.0);
        REQUIRE(circuit.foundOperatingPoint());
        CHECK(circuit.getNodeVoltage("out") == Catch::Approx(0.5 * (supply + 1.1)).margin(0.02));
    }
}

TEST_CASE("An LM386's gain is set by pins 1 and 8", "[circuit][poweramp]")
{
    auto gainOf = [](double gainOhms, double gainFarads)
    {
        auto circuit = makeLm386(9.0, sampleRate, 0.0, gainOhms, gainFarads);
        circuit.setOutputOffsetToOperatingPoint();
        CHECK(circuit.getNonConvergenceCount() == 0);
        return amplitudeOf(circuit, 1000.0, 2.0e-3f, sampleRate, 0.3) / 2.0e-3f;
    };

    // 2 * 15k / 1.5k + 1 = 21 (26.4 dB). 10 uF across: 201. With 1.2k in series:
    // 2 * 15k / (150 + 1.35k || 1.2k) + 1 = 39.2.
    CHECK(gainOf(0.0, 0.0) == Catch::Approx(21.0).epsilon(0.02));
    CHECK(gainOf(0.0, 10.0e-6) == Catch::Approx(201.0).epsilon(0.02));
    CHECK(gainOf(1200.0, 10.0e-6) == Catch::Approx(39.2).epsilon(0.03));
}

TEST_CASE("An LM386's inputs are 50k to ground", "[circuit][poweramp]")
{
    // Driven through 50k, the input divides in half.
    auto circuit = makeLm386(9.0, sampleRate, 50000.0, 0.0, 0.0);
    circuit.setOutputOffsetToOperatingPoint();
    CHECK(amplitudeOf(circuit, 1000.0, 2.0e-3f, sampleRate, 0.3) / 2.0e-3f == Catch::Approx(10.5).epsilon(0.03));
}

TEST_CASE("An LM386 has the datasheet's bandwidth", "[circuit][poweramp]")
{
    // A rate where trapezoidal warping is a couple of percent at 60 kHz. Datasheet
    // conditions: 6 V.
    constexpr double rate = 768000.0;

    auto responseAt = [](double gainFarads, double frequency)
    {
        auto circuit = makeLm386(6.0, rate, 0.0, 0.0, gainFarads);
        circuit.setOutputOffsetToOperatingPoint();
        return amplitudeOf(circuit, frequency, 1.0e-3f, rate, 0.02);
    };

    const auto db = [](float ratio) { return 20.0 * std::log10((double) ratio); };

    // Gain 200: -3 dB at 60 kHz, and within half a decibel at 20 kHz.
    const float ref200 = responseAt(10.0e-6, 1000.0);
    CHECK(db(responseAt(10.0e-6, 60000.0) / ref200) == Catch::Approx(-3.0).margin(0.5));
    CHECK(db(responseAt(10.0e-6, 20000.0) / ref200) > -0.6);

    // Gain 20: flat through the audio band.
    const float ref20 = responseAt(0.0, 1000.0);
    CHECK(db(responseAt(0.0, 20000.0) / ref20) > -0.1);
}

TEST_CASE("An LM386 clips short of its rails, and harder into a speaker", "[circuit][poweramp]")
{
    auto swing = [](double loadOhms, double& lowest, double& highest)
    {
        auto circuit = makeLm386(9.0, sampleRate, 0.0, 0.0, 0.0, loadOhms);
        const double step = 2.0 * std::numbers::pi * 200.0 / sampleRate;
        lowest = 1.0e9;
        highest = -1.0e9;

        for (int i = 0; i < 24000; ++i)
        {
            circuit.process(static_cast<float>(0.6 * std::sin(step * i)));

            if (i > 12000)
            {
                const double v = circuit.getNodeVoltage("out");
                lowest = std::min(lowest, v);
                highest = std::max(highest, v);
            }
        }

        CHECK(circuit.getNonConvergenceCount() == 0);
    };

    double low = 0.0, high = 0.0;
    swing(0.0, low, high);
    INFO("unloaded: " << low << " to " << high);

    // 0.6 V short of each rail, soft-clamped: about the datasheet's 7.8 V p-p on 9 V.
    CHECK(high > 8.0);
    CHECK(high < 8.8);
    CHECK(low > 0.2);
    CHECK(low < 1.0);
    CHECK(high - low == Catch::Approx(7.8).margin(0.5));

    double lowLoaded = 0.0, highLoaded = 0.0;
    swing(8.0, lowLoaded, highLoaded);
    INFO("into 8 ohms: " << lowLoaded << " to " << highLoaded);

    // 6.0 V peak to peak into 8 ohms on the datasheet.
    CHECK(highLoaded - lowLoaded == Catch::Approx(6.0).margin(0.6));
}

//==============================================================================
// From the drawing
//==============================================================================

namespace
{
    using namespace SchematicModel;

    /** A self-biasing stage around one three-terminal part, drawn: load from rail to
        output, feedback from output to control pin and from there to ground, input through
        100k (so Miller capacitance shows). Pins join named nodes rather than wires. Returns
        the settled output amplitude. */
    float probeStage(ElementType type, int modelIndex, double frequency = 1000.0)
    {
        Schematic schematic;

        const auto part = schematic.addElement(type, 0, 0);
        schematic.findElement(part)->modelIndex = modelIndex;

        auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

        // A node label `dx` squares from a pin, wired to it (or on it when dx is zero).
        auto label = [&schematic](juce::Point<int> at, const char* name, int dx)
        {
            const auto id = schematic.addElement(ElementType::Node, at.x + dx - 2, at.y);
            schematic.findElement(id)->label = name;

            if (dx != 0)
                schematic.addWire(at, { at.x + dx, at.y });
        };

        // BJT pins are base, collector, emitter; a MOSFET's drain, gate, source.
        const bool bjt = type == ElementType::Transistor;
        label(pin(part, bjt ? 0 : 1), "ctl", -4);
        label(pin(part, bjt ? 1 : 0), "outn", 4);
        label(pin(part, 2), "com", 4);

        const auto ground = schematic.addElement(ElementType::Ground, 50, 22);
        label(pin(ground, 0), "com", 0);

        const auto supply = schematic.addElement(ElementType::VoltageSource, 60, 40);
        schematic.findElement(supply)->value = 9.0;
        label(pin(supply, 0), "rail", 4);
        label(pin(supply, 1), "com", 4);

        auto resistor = [&](int x, const char* top, const char* bottom, double ohms)
        {
            const auto r = schematic.addElement(ElementType::Resistor, x, 40);
            schematic.findElement(r)->value = ohms;
            label(pin(r, 0), top, -4);
            label(pin(r, 1), bottom, -4);
        };

        resistor(10, "rail", "outn", 10000.0);
        resistor(20, "outn", "ctl", bjt ? 470000.0 : 1.0e6);
        resistor(30, "ctl", "com", bjt ? 1.0e9 : 1.0e6);
        resistor(40, "src", "ctl", 100000.0);

        // Input, coupling capacitor flat beside it, output off its label.
        const auto input = schematic.addElement(ElementType::Input, -40, 60);
        const auto coupling = schematic.addElement(ElementType::Capacitor, -30, 60);
        schematic.findElement(coupling)->value = 1.0e-6;
        schematic.findElement(coupling)->orientation = 1;
        schematic.addWire(pin(input, 0), pin(coupling, 1));
        label(pin(coupling, 0), "src", 4);

        const auto output = schematic.addElement(ElementType::Output, 0, 80);
        label(pin(output, 0), "outn", -4);

        auto result = buildCircuits(schematic, sampleRate, 1);
        INFO(result.error);
        REQUIRE(result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE(circuit.foundOperatingPoint());
        circuit.setOutputOffsetToOperatingPoint();

        const float peak = amplitudeOf(circuit, frequency, 1.0e-3f, sampleRate, 0.2);
        REQUIRE(std::isfinite(peak));
        return peak;
    }

    /** A drawn non-inverting stage, gain 101, single 9 V supply, input at ground (output
        rests on the low clamp). Driven at 10 kHz into clipping, so both GBW and headroom
        show. Returns the settled output amplitude. */
    float probeOpAmpStage(int modelIndex)
    {
        Schematic schematic;

        const auto amp = schematic.addElement(ElementType::OpAmp, 0, 0);
        schematic.findElement(amp)->modelIndex = modelIndex;
        schematic.findElement(amp)->value = 9.0;

        auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

        auto label = [&schematic](juce::Point<int> at, const char* name, int dx)
        {
            const auto id = schematic.addElement(ElementType::Node, at.x + dx - 2, at.y);
            schematic.findElement(id)->label = name;

            if (dx != 0)
                schematic.addWire(at, { at.x + dx, at.y });
        };

        // In+, in-, out.
        label(pin(amp, 0), "src", -4);
        label(pin(amp, 1), "fb", -4);
        label(pin(amp, 2), "amp", 4);

        const auto ground = schematic.addElement(ElementType::Ground, 50, 22);
        label(pin(ground, 0), "com", 0);

        auto resistor = [&](int x, const char* top, const char* bottom, double ohms)
        {
            const auto r = schematic.addElement(ElementType::Resistor, x, 40);
            schematic.findElement(r)->value = ohms;
            label(pin(r, 0), top, -4);
            label(pin(r, 1), bottom, -4);
        };

        resistor(10, "amp", "fb", 100000.0);
        resistor(20, "fb", "com", 1000.0);

        const auto input = schematic.addElement(ElementType::Input, -40, 60);
        label(pin(input, 0), "src", 4);

        const auto output = schematic.addElement(ElementType::Output, 0, 80);
        label(pin(output, 0), "amp", -4);

        auto result = buildCircuits(schematic, sampleRate, 1);
        INFO(result.error);
        REQUIRE(result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE(circuit.foundOperatingPoint());
        circuit.setOutputOffsetToOperatingPoint();

        const float peak = amplitudeOf(circuit, 10000.0, 0.05f, sampleRate, 0.2);
        REQUIRE(std::isfinite(peak));
        return peak;
    }
}

TEST_CASE("Every transistor and MOSFET model in the palette reaches the engine", "[plugin][schematic]")
{
    // The builder's switches end in `default:`, so a forgotten model builds as model 0.
    // Each must differ from the head of its list.
    for (const auto type : { ElementType::Transistor, ElementType::Mosfet })
    {
        const auto choices = getModelChoices(type);
        REQUIRE(choices.size() > 1);

        const float first = probeStage(type, 0);

        for (int i = 1; i < choices.size(); ++i)
        {
            INFO(getElementInfo(type).name << " model " << i << " (" << choices[i] << ") behaves as model 0");
            CHECK(std::abs(probeStage(type, i) - first) > 1.0e-6f);
        }
    }
}

TEST_CASE("Every op-amp model in the palette reaches the engine", "[plugin][schematic]")
{
    // Same for op-amps (a forgotten index builds as the TL072). The ideal one takes a
    // different call and is included to reach that branch.
    const auto choices = getModelChoices(ElementType::OpAmp);
    REQUIRE(choices.size() > 1);

    const float first = probeOpAmpStage(0);

    for (int i = 1; i < choices.size(); ++i)
    {
        INFO("op-amp model " << i << " (" << choices[i] << ") behaves as model 0");
        CHECK(std::abs(probeOpAmpStage(i) - first) > 1.0e-6f);
    }
}

TEST_CASE("An amplifier with too low a supply is refused by name", "[plugin][schematic]")
{
    // Below the model's minimum supply, the clamps overlap and pin the output: refused,
    // naming the part.
    const auto stage = [](ElementType type, double supply)
    {
        Schematic schematic;

        const auto amp = schematic.addElement(type, 0, 0);
        schematic.findElement(amp)->value = supply;

        auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

        auto label = [&schematic](juce::Point<int> at, const char* name, int dx)
        {
            const auto id = schematic.addElement(ElementType::Node, at.x + dx - 2, at.y);
            schematic.findElement(id)->label = name;

            if (dx != 0)
                schematic.addWire(at, { at.x + dx, at.y });
        };

        label(pin(amp, 0), "src", -4);
        label(pin(amp, 1), "com", -4);
        label(pin(amp, 2), "amp", 4);

        const auto ground = schematic.addElement(ElementType::Ground, 50, 22);
        label(pin(ground, 0), "com", 0);

        const auto input = schematic.addElement(ElementType::Input, -40, 60);
        label(pin(input, 0), "src", 4);

        const auto output = schematic.addElement(ElementType::Output, 0, 80);
        label(pin(output, 0), "amp", -4);

        return std::make_pair(buildCircuits(schematic, sampleRate, 1), amp);
    };

    for (const auto type : { ElementType::OpAmp, ElementType::PowerAmp })
    {
        INFO(getElementInfo(type).name);

        // Not a structured binding: the Linux CI compiler won't let a lambda capture one.
        const auto built = stage(type, 2.0);
        const auto& starved = built.first;
        const int id = built.second;
        CHECK_FALSE(starved.isValid());

        const auto named = std::find_if(starved.diagnostics.begin(), starved.diagnostics.end(),
                                        [id](const Diagnostic& d)
                                        { return d.isError() && d.elementId == id && d.text.contains("Supply too low"); });
        CHECK(named != starved.diagnostics.end());

        CHECK(stage(type, 9.0).first.isValid());
    }
}

TEST_CASE("A drawn LM386 builds, amplifies, and leaves its gain pins open quietly", "[plugin][schematic][poweramp]")
{
    Schematic schematic;

    const auto amp = schematic.addElement(ElementType::PowerAmp, 0, 0);
    schematic.findElement(amp)->value = 9.0;

    auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

    // Terminals placed on their pins by offset: in on in+, ground on in-, out on output.
    const auto place = [&](ElementType type, juce::Point<int> onto)
    {
        const auto id = schematic.addElement(type, 0, 0);
        const auto offset = schematic.findElement(id)->getPinPosition(0);
        schematic.findElement(id)->x = onto.x - offset.x;
        schematic.findElement(id)->y = onto.y - offset.y;
        return id;
    };

    place(ElementType::Input, pin(amp, 0));
    place(ElementType::Ground, pin(amp, 1));
    place(ElementType::Output, pin(amp, 2));

    auto result = buildCircuits(schematic, sampleRate, 1);
    INFO(result.error);
    REQUIRE(result.isValid());

    // Pins 1 and 8 open is gain 20, not a mistake.
    for (const auto& diagnostic : result.diagnostics)
    {
        INFO(diagnostic.text);
        CHECK(! diagnostic.text.contains("touches nothing else"));
    }

    auto& circuit = *result.circuits[0];
    REQUIRE(circuit.foundOperatingPoint());
    circuit.setOutputOffsetToOperatingPoint();

    CHECK(amplitudeOf(circuit, 1000.0, 2.0e-3f, sampleRate, 0.3) / 2.0e-3f == Catch::Approx(21.0).epsilon(0.03));
}

//==============================================================================
// Op-amp supplies

namespace
{
    using namespace SchematicModel;

    /** A drawn non-inverting stage, gain 101 (100k over 1k), input through 10u biased by
        1M. The 1M and 1k go to "com" (ground) for a split supply, or "vg", a source
        `virtualGround` volts above ground, for single supply. Returns the circuit
        unprepared, so tests can read node "out" directly. */
    std::unique_ptr<Circuit> drawSupplyStage(double plus, double minus, double virtualGround)
    {
        Schematic schematic;

        auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

        auto label = [&schematic](juce::Point<int> at, const char* name, int dx)
        {
            const auto id = schematic.addElement(ElementType::Node, at.x + dx - 2, at.y);
            schematic.findElement(id)->label = name;

            if (dx != 0)
                schematic.addWire(at, { at.x + dx, at.y });
        };

        auto twoPin = [&](ElementType type, int x, const char* top, const char* bottom, double value)
        {
            const auto id = schematic.addElement(type, x, 40);
            schematic.findElement(id)->value = value;
            label(pin(id, 0), top, -4);
            label(pin(id, 1), bottom, -4);
            return id;
        };

        const auto amp = schematic.addElement(ElementType::OpAmp, 0, 0);
        schematic.findElement(amp)->value = plus;
        schematic.findElement(amp)->valueB = minus;

        label(pin(amp, 0), "bias", -4);
        label(pin(amp, 1), "fb", -4);
        label(pin(amp, 2), "amp", 4);

        const auto ground = schematic.addElement(ElementType::Ground, 50, 22);
        label(pin(ground, 0), "com", 0);

        const char* reference = virtualGround > 0.0 ? "vg" : "com";

        if (virtualGround > 0.0)
            twoPin(ElementType::VoltageSource, 0, "vg", "com", virtualGround);

        twoPin(ElementType::Capacitor, 10, "src", "bias", 10.0e-6);
        twoPin(ElementType::Resistor, 20, "bias", reference, 1.0e6);
        twoPin(ElementType::Resistor, 30, "amp", "fb", 100000.0);
        twoPin(ElementType::Resistor, 40, "fb", reference, 1000.0);

        const auto input = schematic.addElement(ElementType::Input, -40, 60);
        label(pin(input, 0), "src", 4);

        const auto output = schematic.addElement(ElementType::Output, 0, 80);
        label(pin(output, 0), "amp", -4);

        auto result = buildCircuits(schematic, sampleRate, 1);
        INFO(result.error);
        REQUIRE(result.isValid());
        REQUIRE(result.circuits[0]->foundOperatingPoint());
        return std::move(result.circuits[0]);
    }

    /** The output node voltage per sample under a 100 Hz sine. */
    std::vector<double> driveSupplyStage(Circuit& circuit, float amplitude)
    {
        std::vector<double> out;

        for (int n = 0; n < 4800; ++n)
        {
            circuit.process(amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 100.0 * n / sampleRate)));
            out.push_back(circuit.getNodeVoltage("out"));
        }

        return out;
    }
}

TEST_CASE("A drawn op-amp runs on a split supply", "[plugin][schematic][opamp]")
{
    // On +15/-15 with ground references: rests at ground, clips at both rails.
    auto split = drawSupplyStage(15.0, -15.0, 0.0);
    CHECK(std::abs(split->getNodeVoltage("out")) < 0.05);

    const auto swing = driveSupplyStage(*split, 0.3f);
    const auto [low, high] = std::minmax_element(swing.begin(), swing.end());

    const auto model = OpAmpModel::tl072().withRails(15.0, -15.0);
    INFO("clips at " << *high << " and " << *low << " V");
    CHECK(*high == Catch::Approx(15.0 - model.positiveHeadroom()).margin(0.5));
    CHECK(*low == Catch::Approx(-15.0 + model.negativeHeadroom()).margin(0.5));

    // The single-supply equivalent (30 V with a 15 V virtual ground) matches 15 V up, to
    // within 1.5 mV: gmin to ground leaks 15 pA through the 1M bias, times 101. Only on the
    // linear part; the clipped part is set by the rails.
    auto shifted = drawSupplyStage(30.0, 0.0, 15.0);
    const auto workaround = driveSupplyStage(*shifted, 0.3f);

    double worst = 0.0;
    for (size_t i = 0; i < swing.size(); ++i)
        worst = std::max(worst, std::abs(swing[i] - (workaround[i] - 15.0)));

    INFO("the two drawings part by " << worst << " V");
    CHECK(worst < 2.0e-3);
}

TEST_CASE("An op-amp's negative supply is ground unless it is set", "[plugin][schematic][opamp]")
{
    // A new op-amp, and one from a sheet predating the field, are single-supply: zero
    // means ground, not unset.
    Schematic sheet;
    const auto id = sheet.addElement(ElementType::OpAmp, 0, 0);
    CHECK(juce::exactlyEqual(sheet.findElement(id)->valueB, 0.0));
    CHECK(sheet.findElement(id)->isSecondValueRail());

    sheet.findElement(id)->value = 15.0;
    sheet.findElement(id)->valueB = -15.0;

    Schematic reloaded;
    reloaded.restoreFromValueTree(sheet.toValueTree());
    CHECK(juce::exactlyEqual(reloaded.getElements()[0].valueB, -15.0));

    auto older = sheet.toValueTree();
    older.getChild(0).removeProperty("valueB", nullptr);
    Schematic old;
    old.restoreFromValueTree(older);
    CHECK(juce::exactlyEqual(old.getElements()[0].valueB, 0.0));

    // A transformer still reads an absent secondary as 1.
    Schematic transformer;
    transformer.addElement(ElementType::Transformer, 0, 0);
    auto tree = transformer.toValueTree();
    tree.getChild(0).removeProperty("valueB", nullptr);
    transformer.restoreFromValueTree(tree);
    CHECK(juce::exactlyEqual(transformer.getElements()[0].valueB, 1.0));

    // The field is the negative supply, so "15" means 15 V below ground.
    CHECK(juce::exactlyEqual(Element::railBelowGround(15.0), -15.0));
    CHECK(juce::exactlyEqual(Element::railBelowGround(-15.0), -15.0));
    CHECK(juce::exactlyEqual(Element::railBelowGround(0.0), 0.0));
}

TEST_CASE("A negative supply above ground is refused by name", "[plugin][schematic][opamp]")
{
    // A positive negative-rail only comes from a hand-edited file; refused, naming the
    // part.
    Schematic sheet;
    const auto amp = sheet.addElement(ElementType::OpAmp, 0, 0);
    sheet.findElement(amp)->value = 15.0;
    sheet.findElement(amp)->valueB = 5.0;
    sheet.addElement(ElementType::Ground, 20, 20);
    sheet.addElement(ElementType::Input, -20, 0);
    sheet.addElement(ElementType::Output, 20, 0);

    const auto result = buildCircuits(sheet, sampleRate, 1);
    CHECK_FALSE(result.isValid());

    const auto named = std::find_if(result.diagnostics.begin(), result.diagnostics.end(), [amp](const Diagnostic& d)
                                    { return d.isError() && d.elementId == amp && d.text.contains("above ground"); });
    CHECK(named != result.diagnostics.end());
}
