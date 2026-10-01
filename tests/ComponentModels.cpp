// The parts added with the MOSFET: every new model held to the datasheet it was
// fitted to, the device maths that came with them held to its own derivatives,
// and each new part type followed from the drawing to the engine.
//
// Datasheet figures are checked through the engine's own equations -- a
// device's linearise(), or a Circuit solved to its bias point -- rather than an
// independent formula, so what is measured is the model the solver runs.

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

    /** Terminal currents of an NPN at Vbe, with its collector held `vce` above
        its emitter, from the device's own linearisation. */
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

    /** The same through a real Circuit, which is the only way to reach a
        Darlington: addTransistor() builds the pair. The collector is held by a
        supply and the base by a swept source, and both currents are read off
        the sources. */
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

    /** The voltage across a diode -- series resistance, guard ring and all,
        since those are wired by addDiode() -- at a forward current. */
    double diodeVoltageAt(const DiodeModel& model, double current)
    {
        Circuit circuit;
        const auto source = circuit.addVoltageSource("a", "gnd", 0.5);
        circuit.addDiode("a", "gnd", model);
        circuit.addResistor("in", "gnd", 1000.0);
        circuit.setInputNode("in");
        circuit.setOutputNode("a");
        circuit.prepare(sampleRate);

        double lo = 0.0, hi = 1.5;

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
    // Philips's typicals, reprinted on the Comset BC107-109 sheet: hFE at 10 uA
    // and at 2 mA, Vce 5 V. Each grade was refitted to exactly these, because
    // Philips's own extracted cards hold the gain flat to 10 uA.
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

TEST_CASE("The 2N5088 and 2N5089 reproduce Fairchild's card and sheet", "[bjt][model]")
{
    // The datasheet's own SPICE card, run through the engine. Its minima:
    // 2N5088 300 / 350 / 300 at 0.1 / 1 / 10 mA (900 max at 0.1 mA), 2N5089
    // 400 / 450 / 400 (1200 max).
    struct Point { double ic, card, minimum; };

    const Point p5088[] = { { 100.0e-6, 487.3, 300.0 }, { 1.0e-3, 649.9, 350.0 }, { 10.0e-3, 554.0, 300.0 } };
    const Point p5089[] = { { 100.0e-6, 650.9, 400.0 }, { 1.0e-3, 869.9, 450.0 }, { 10.0e-3, 741.8, 400.0 } };

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

    // Two devices on the netlist: four ports where one transistor has two.
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

    // Guaranteed 5000 at 10 mA; a typical part clears it by a mile.
    CHECK(circuitNpnAt(m, 10.0e-3).hfe > 5000.0);
}

TEST_CASE("A Darlington's two halves move together on a model swap", "[bjt][darlington]")
{
    // Swapping the card on the pair has to land both halves on it: the result
    // must be the pair that would have been built with the new card.
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

    // And a single transistor handed a Darlington's card stays single: its
    // neighbour in the list is left alone.
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

    // Different cards, different base currents -- the second kept the 2N3904.
    CHECK(std::abs(two.getSourceCurrent(b1) - two.getSourceCurrent(b2)) > 1.0e-9);
}

TEST_CASE("The high-injection knee turns the gain over and keeps its Jacobian exact", "[bjt][model]")
{
    auto withKnee = BjtModel::npn2N5088();
    auto without = withKnee;
    without.forwardKneeCurrent = 0.0;

    // Below the knee it barely matters; past it, it is most of the story.
    CHECK(npnAt(withKnee, 100.0e-6).hfe == Catch::Approx(npnAt(without, 100.0e-6).hfe).epsilon(0.02));
    CHECK(npnAt(withKnee, 30.0e-3).hfe < 0.75 * npnAt(without, 30.0e-3).hfe);

    // Newton relies on the derivatives being those of what is stamped.
    Bjt t {};
    t.model = withKnee;

    for (const double vbe : { 0.3, 0.55, 0.65, 0.72, 0.8 })
        for (const double vce : { 0.05, 0.3, 5.0 })
            checkJacobian(t, vbe, vbe - vce);
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

    // Softer than a 1N4148: lower at a microamp-ish current, and a wider
    // voltage span over the same three decades.
    const auto sig = DiodeModel::d1n4148();
    CHECK(diodeVoltageAt(m, 10.0e-6) < diodeVoltageAt(sig, 10.0e-6));
    CHECK(diodeVoltageAt(m, 10.0e-3) - diodeVoltageAt(m, 10.0e-6)
          > diodeVoltageAt(sig, 10.0e-3) - diodeVoltageAt(sig, 10.0e-6));
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

    // Two junctions and a resistor: the Schottky behind its 35 ohms, and the
    // guard ring across the lot.
    Circuit circuit;
    circuit.addDiode("a", "gnd", m);
    circuit.addResistor("in", "a", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("a");
    circuit.prepare(sampleRate);
    CHECK(circuit.getPortCount() == 2);
}

TEST_CASE("A diode stack scales its series resistance and guard ring", "[diode]")
{
    // n composite diodes in series drop n times one at the same current --
    // resistance, ring and all.
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
    // Forward at +5 V through 1k, then yanked to -5 V. A diode with a transit
    // time has TT * If of charge to give back before it blocks; one without
    // has only its depletion capacitance's.
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

    // Without its depletion capacitance, which a step this sharp through a
    // resistor this small would set ringing at Nyquist -- the trapezoidal
    // rule's answer to a time constant far shorter than a sample, and true of
    // every small capacitor in the engine. It is the transit time on test.
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

    // At rest the two are the same diode.
    CHECK(ifSlow == Catch::Approx(ifInstant).epsilon(1.0e-5));
}

TEST_CASE("A 1N4001 clipper sounds different from a charge-free one", "[diode][charge]")
{
    // The stored charge is the audible part of a rectifier in a clipper: at
    // 5 kHz it carries a real fraction of the junction's current.
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

TEST_CASE("The MOSFET models reproduce their datasheets", "[mosfet][model]")
{
    // Vgs(th) is the datasheet's gate voltage for 1 mA with the drain strapped
    // to the gate: 2.1 V typ (2N7000), 2.0 (BS170), and the BS250's from
    // Philips's model, 2.53.
    CHECK(thresholdAt(MosfetModel::n2N7000(), 1.0e-3) == Catch::Approx(2.10).margin(0.01));
    CHECK(thresholdAt(MosfetModel::nBS170(), 1.0e-3) == Catch::Approx(2.00).margin(0.01));
    CHECK(thresholdAt(MosfetModel::pBS250(), 1.0e-3) == Catch::Approx(2.53).margin(0.01));

    // Typical transconductance at 200 mA, Vds 10 V. The BS250's is the one
    // figure its sheet gives and is fitted exactly; the N-channel parts are
    // fitted to their whole output curves and land under the typical.
    CHECK(transconductanceAt(MosfetModel::pBS250(), 0.2, 10.0) == Catch::Approx(0.150).epsilon(0.02));
    CHECK(transconductanceAt(MosfetModel::n2N7000(), 0.2, 10.0) == Catch::Approx(0.320).epsilon(0.2));
    CHECK(transconductanceAt(MosfetModel::nBS170(), 0.25, 10.0) == Catch::Approx(0.200).epsilon(0.25));

    // Typical output curves, in saturation.
    const auto n7000 = MosfetModel::n2N7000();
    CHECK(drainCurrent(n7000, 3.0, 5.0) == Catch::Approx(0.070).epsilon(0.2));
    CHECK(drainCurrent(n7000, 4.0, 5.0) == Catch::Approx(0.395).epsilon(0.2));
    CHECK(drainCurrent(n7000, 5.0, 5.0) == Catch::Approx(0.820).epsilon(0.1));

    const auto bs170 = MosfetModel::nBS170();
    CHECK(drainCurrent(bs170, 4.0, 10.0) == Catch::Approx(0.115).epsilon(0.25));
    CHECK(drainCurrent(bs170, 5.0, 10.0) == Catch::Approx(0.300).epsilon(0.05));
    CHECK(drainCurrent(bs170, 6.0, 10.0) == Catch::Approx(0.580).epsilon(0.15));
    CHECK(drainCurrent(bs170, 8.0, 10.0) == Catch::Approx(1.210).epsilon(0.1));

    // And the two N-channel parts really are different parts.
    CHECK(transconductanceAt(bs170, 1.0e-3, 5.0) < 0.8 * transconductanceAt(n7000, 1.0e-3, 5.0));
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
    // The channel alone: the body diode is deliberately one-sided, and by
    // -0.5 V it is already carrying tens of microamps.
    auto m = MosfetModel::n2N7000();
    m.bodyDiodeSaturationCurrent = 0.0;

    // Swapping drain and source negates the current.
    for (const double vgs : { 1.8, 2.2, 3.0 })
        for (const double vds : { 0.01, 0.1, 0.5 })
        {
            // gmin is the only term left that is not the channel.
            INFO("Vgs " << vgs << ", Vds " << vds);
            CHECK(drainCurrent(m, vgs - vds, -vds)
                  == Catch::Approx(-drainCurrent(m, vgs, vds)).epsilon(1.0e-6).margin(1.0e-11));
        }

    // The slope just either side of zero agrees, as the JFET's does.
    auto slope = [&m](double vds) { return (drainCurrent(m, 2.5, vds + 1.0e-9) - drainCurrent(m, 2.5, vds - 1.0e-9)) / 2.0e-9; };
    CHECK(slope(-1.0e-6) == Catch::Approx(slope(1.0e-6)).epsilon(1.0e-3));
}

TEST_CASE("The MOSFET Jacobian is the derivative of what it stamps", "[mosfet][model]")
{
    for (const auto& model : { MosfetModel::n2N7000(), MosfetModel::nBS170(), MosfetModel::pBS250() })
    {
        Mosfet m {};
        m.model = model;
        const double p = model.polaritySign();

        // Subthreshold, moderate and strong inversion; triode, saturation,
        // reverse, and the body diode conducting.
        for (const double vgs : { 0.5, 1.9, 2.1, 2.4, 4.0 })
            for (const double vds : { -0.7, -0.2, 0.05, 0.3, 5.0 })
                checkJacobian(m, p * vgs, p * vds);
    }
}

TEST_CASE("A MOSFET's body diode conducts when the drain is pulled below the source", "[mosfet][model]")
{
    const auto m = MosfetModel::n2N7000();

    // Gate at the source, drain 0.6 V below: the channel is off and the body
    // diode forward-biased -- current out of the drain, milliamps of it.
    const double id = drainCurrent(m, 0.0, -0.6);
    CHECK(id < -0.5e-3);
    CHECK(id > -5.0e-3);

    // And a P-channel part's is the other way round.
    CHECK(drainCurrent(MosfetModel::pBS250(), 0.0, 0.6) > 0.1e-3);
}

namespace
{
    /** A drain-feedback common-source stage: Rd to the rail, the gate held at
        half the drain by two 1M resistors, the signal onto the gate through a
        capacitor. Biases itself whatever the threshold. */
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

    // Vd = 9 - 10k Id with Vg = Vd / 2: the gate sits a little over its
    // threshold, so the drain at about 4.1 V.
    const double vd = circuit.getNodeVoltage("d");
    INFO("drain at " << vd << " V");
    CHECK(vd > 3.6);
    CHECK(vd < 4.6);

    // The gate is insulated, so the divider is exactly a divider.
    CHECK(circuit.getNodeVoltage("g") == Catch::Approx(0.5 * vd).epsilon(1.0e-6));

    // About gm * 10k, inverting: gm near 8.5 mS at half a milliamp.
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

    // The drain hangs below the rail by as much as the N stage's sits above
    // ground, give or take the BS250's higher threshold.
    const double vd = circuit.getNodeVoltage("d");
    INFO("drain at " << vd << " V");
    CHECK(vd > 3.6);
    CHECK(vd < 5.4);

    circuit.setOutputOffsetToOperatingPoint();
    CHECK(amplitudeOf(circuit, 1000.0, 1.0e-3f, sampleRate, 0.5) > 20.0e-3f);
}

TEST_CASE("DK and full Newton agree on a MOSFET stage", "[circuit][mosfet][dk]")
{
    auto dk = makeMosfetStage(MosfetModel::nBS170(), Circuit::SolverStrategy::DiscreteK);
    auto full = makeMosfetStage(MosfetModel::nBS170(), Circuit::SolverStrategy::FullNewton);

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
    /** An LM386 with in+ driven through `sourceOhms` (zero for directly), in-
        grounded, an unloaded output, and `between18` wired across pins 1 and 8:
        nothing, a capacitor, or a resistor and capacitor in series. */
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

    // 2 * 15k / 1.5k, plus one from the + input: 21, which is the 26.4 dB the
    // datasheet plots. With 10 uF across: 201. With 1.2k in series with it,
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
    // At a rate high enough that the trapezoidal rule's warping is a couple
    // of percent at 60 kHz. Datasheet conditions: 6 V.
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

    // 0.6 V short of each rail by the model, soft as the op-amp's clamp is.
    // The datasheet's 7.8 V peak to peak on 9 V, give or take the softness.
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

    /** A self-biasing stage around one three-terminal active part, drawn: a
        load from the rail to the output pin, a feedback resistor from the
        output pin to the control pin and a second from there to ground, and
        the input coupled onto the control pin through 100k -- enough source
        resistance for the Miller capacitance to show. Every BJT and MOSFET in
        the lists biases in it.

        Pins are joined to named nodes rather than to each other: a label names
        its net, which spares routing wires past other parts' pins. Returns the
        settled amplitude at the output. */
    float probeStage(ElementType type, int modelIndex, const BuildOptions& options = {},
                     double frequency = 1000.0)
    {
        Schematic schematic;

        const auto part = schematic.addElement(type, 0, 0);
        schematic.findElement(part)->modelIndex = modelIndex;

        auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

        // A node label `dx` squares along from a pin, wired to it -- or sitting
        // on it, when dx is zero.
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

        // Input, coupling capacitor lying flat beside it, output off its label.
        const auto input = schematic.addElement(ElementType::Input, -40, 60);
        const auto coupling = schematic.addElement(ElementType::Capacitor, -30, 60);
        schematic.findElement(coupling)->value = 1.0e-6;
        schematic.findElement(coupling)->orientation = 1;
        schematic.addWire(pin(input, 0), pin(coupling, 1));
        label(pin(coupling, 0), "src", 4);

        const auto output = schematic.addElement(ElementType::Output, 0, 80);
        label(pin(output, 0), "outn", -4);

        auto result = buildCircuits(schematic, sampleRate, 1, options);
        INFO(result.error);
        REQUIRE(result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE(circuit.foundOperatingPoint());
        circuit.setOutputOffsetToOperatingPoint();

        const float peak = amplitudeOf(circuit, frequency, 1.0e-3f, sampleRate, 0.2);
        REQUIRE(std::isfinite(peak));
        return peak;
    }
}

TEST_CASE("Every transistor and MOSFET model in the palette reaches the engine", "[plugin][schematic]")
{
    // The builder's switches end in `default:`, so a model the table lists and
    // the switch forgot silently builds as model 0. Each is probed in a stage
    // and has to come out differently from the head of its list.
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

TEST_CASE("The junction capacitance option reaches the MOSFET", "[plugin][schematic]")
{
    // Driven through 100k, the gate-drain capacitance times the stage's gain
    // is a lowpass near the top of the audio band -- near rather than inside,
    // since the drain feedback that biases the stage also lowers the gate's
    // impedance. Off, it goes.
    BuildOptions with, without;
    without.transistorJunctionCapacitance = false;

    const float on = probeStage(ElementType::Mosfet, 0, with, 10000.0);
    const float off = probeStage(ElementType::Mosfet, 0, without, 10000.0);

    INFO("10 kHz: " << on << " with the capacitances, " << off << " without");
    CHECK(on < 0.9f * off);

    // And at a low frequency they agree: it is only the capacitance.
    CHECK(probeStage(ElementType::Mosfet, 0, with, 100.0)
          == Catch::Approx(probeStage(ElementType::Mosfet, 0, without, 100.0)).epsilon(0.02));
}

TEST_CASE("A drawn LM386 builds, amplifies, and leaves its gain pins open quietly", "[plugin][schematic][poweramp]")
{
    Schematic schematic;

    const auto amp = schematic.addElement(ElementType::PowerAmp, 0, 0);
    schematic.findElement(amp)->value = 9.0;

    auto pin = [&schematic](int id, int index) { return schematic.findElement(id)->getPinPosition(index); };

    // Terminals placed on their pins by their own offsets, as the diode probe
    // does: in on in+, ground on in-, out on the output.
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

    // Pins 1 and 8 unconnected is gain 20, not a mistake.
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
