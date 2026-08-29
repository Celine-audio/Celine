#include <CelineEngine/Circuits.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <numbers>

//==============================================================================
// Allocation counting, so the realtime-safety claim is checked rather than
// asserted. Counting is off unless a test explicitly turns it on, so the rest
// of the suite (and Catch2 itself) is unaffected.
//==============================================================================

namespace
{
    std::atomic<bool> countingAllocations{false};
    std::atomic<int> allocationCount{0};

    void noteAllocation() noexcept
    {
        if (countingAllocations.load(std::memory_order_relaxed))
            allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace

void* operator new(std::size_t size)
{
    noteAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size)
{
    noteAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    constexpr double sampleRate = 48000.0;

    /** Runs a sine through a circuit and returns the peak output amplitude after
        letting the transient settle. */
    float measureResponse(Circuit& circuit, double frequency, float amplitude, double seconds = 0.05)
    {
        const auto numSamples = static_cast<int>(sampleRate * seconds);
        const double phaseStep = 2.0 * std::numbers::pi * frequency / sampleRate;

        // Discard the first half so we're measuring the steady state.
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto in = static_cast<float>(amplitude * std::sin(phaseStep * i));
            const float out = circuit.process(in);

            if (i > numSamples / 2)
                peak = std::max(peak, std::abs(out));
        }

        return peak;
    }

    /** The clipper topology with its parts left open.

        The tests below build their own rather than calling
        Circuits::makeDiodeClipper(), because that function is meant to be edited
        freely -- change the diode there and no test should start failing. These
        cover the engine; the factory covers taste. */
    Circuit makeClipperWithDiode(const Circuit::DiodeModel& model, int forwardSeriesCount, int reverseSeriesCount)
    {
        Circuit circuit;
        circuit.addResistor("in", "out", 1300.0);
        circuit.addDiode("out", "gnd", model, forwardSeriesCount);
        circuit.addDiode("gnd", "out", model, reverseSeriesCount);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        return circuit;
    }

    /** Settles the circuit at a constant input and returns the final output. */
    float measureDC(Circuit& circuit, float vIn, double seconds = 0.5)
    {
        const auto numSamples = static_cast<int>(sampleRate * seconds);
        float out = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            out = circuit.process(vIn);

        return out;
    }
} // namespace

//==============================================================================
// Linear components
//==============================================================================

TEST_CASE("RC lowpass rolls off at its cutoff", "[circuit][linear]")
{
    // 1k / 100nF -> fc = 1 / (2*pi*R*C) = 1591 Hz
    auto lowpass = Circuits::makeOnePoleLowpass(1000.0, 100.0e-9);
    lowpass.circuit.prepare(sampleRate);

    const double cutoff = 1.0 / (2.0 * std::numbers::pi * 1000.0 * 100.0e-9);

    const float atDC = measureResponse(lowpass.circuit, 20.0, 1.0f);
    const float atCutoff = measureResponse(lowpass.circuit, cutoff, 1.0f);

    CHECK(atDC == Catch::Approx(1.0f).margin(0.02));

    // -3 dB at the cutoff, within a couple of percent.
    CHECK(atCutoff == Catch::Approx(0.7071f).margin(0.02));

    // Two octaves up, |H| = 1/sqrt(1 + 4^2). The margin is wide enough to absorb
    // the frequency warping the trapezoidal rule introduces this far up the band
    // -- stay well clear of Nyquist here or that warping dominates the result.
    const float twoOctavesUp = measureResponse(lowpass.circuit, cutoff * 4.0, 1.0f);
    CHECK(twoOctavesUp == Catch::Approx(0.2425f).margin(0.02));
}

TEST_CASE("RC highpass rolls off at its cutoff", "[circuit][linear]")
{
    auto highpass = Circuits::makeOnePoleHighpass(1000.0, 100.0e-9);
    highpass.circuit.prepare(sampleRate);

    const double cutoff = 1.0 / (2.0 * std::numbers::pi * 1000.0 * 100.0e-9);

    CHECK(measureResponse(highpass.circuit, cutoff, 1.0f) == Catch::Approx(0.7071f).margin(0.02));
    CHECK(measureResponse(highpass.circuit, cutoff * 4.0, 1.0f) == Catch::Approx(0.9701f).margin(0.02));
    CHECK(measureResponse(highpass.circuit, cutoff / 8.0, 1.0f) == Catch::Approx(0.125f).margin(0.01));
}

TEST_CASE("A resistive divider solves exactly", "[circuit][linear]")
{
    Circuit circuit;
    circuit.addResistor("in", "out", 10000.0);
    circuit.addResistor("out", "gnd", 30000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // 30k / (10k + 30k) = 0.75
    CHECK(circuit.process(1.0f) == Catch::Approx(0.75f).epsilon(1.0e-5));
    CHECK(circuit.process(-2.0f) == Catch::Approx(-1.5f).epsilon(1.0e-5));
    CHECK(circuit.getNumUnknowns() == 1);
    CHECK_FALSE(circuit.isNonlinear());
}

TEST_CASE("An LR highpass rolls off at its cutoff", "[circuit][linear][inductor]")
{
    // 1k in series with a 100 mH inductor to ground. The inductor is a short at
    // DC and an open at high frequency, so the shunt node is a highpass with
    // fc = R / (2*pi*L) = 1591 Hz.
    Circuit circuit;
    circuit.addResistor("in", "out", 1000.0);
    circuit.addInductor("out", "gnd", 100.0e-3);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    const double cutoff = 1000.0 / (2.0 * std::numbers::pi * 100.0e-3);

    CHECK(measureResponse(circuit, cutoff, 1.0f) == Catch::Approx(0.7071f).margin(0.02));
    CHECK(measureResponse(circuit, cutoff * 4.0, 1.0f) == Catch::Approx(0.9701f).margin(0.02));
    CHECK(measureResponse(circuit, cutoff / 16.0, 1.0f) == Catch::Approx(0.0625f).margin(0.005));
}

//==============================================================================
// Diodes
//==============================================================================

TEST_CASE("A single diode satisfies the Shockley equation", "[circuit][diode]")
{
    // in --[1k]-- out --|>|-- gnd
    // At the solution the resistor current has to equal the diode current, which
    // is a check on the Newton solve rather than on any particular waveform.
    constexpr double seriesR = 1000.0;
    const auto model = Circuit::DiodeModel::silicon();

    Circuit circuit;
    circuit.addResistor("in", "out", seriesR);
    circuit.addDiode("out", "gnd");
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    REQUIRE(circuit.isNonlinear());

    for (const float vIn : {0.1f, 0.5f, 1.0f, 5.0f, 50.0f})
    {
        const double vOut = circuit.process(vIn);

        const double resistorCurrent = (vIn - vOut) / seriesR;
        const double diodeCurrent = model.saturationCurrent * (std::exp(vOut / model.scaleVoltage()) - 1.0)
                                  + CircuitComponents::gmin * vOut;

        // Relative agreement, since the currents span nanoamps to milliamps.
        // Newton stops once the node voltages settle to within its tolerance, so
        // KCL holds to about that tolerance rather than exactly -- the residual is
        // dominated by the lowest-current case, where the junction's conductance
        // is smallest and a given voltage error buys the least current.
        CHECK(diodeCurrent == Catch::Approx(resistorCurrent).epsilon(1.0e-4));
    }
}

TEST_CASE("A reverse-biased diode blocks", "[circuit][diode]")
{
    Circuit circuit;
    circuit.addResistor("in", "out", 1000.0);
    circuit.addResistor("out", "gnd", 1000000.0);
    circuit.addDiode("gnd", "out"); // cathode at the output: reverse biased for positive input
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // Blocking: the divider is essentially untouched.
    CHECK(measureDC(circuit, 1.0f, 0.01) == Catch::Approx(1.0f).margin(0.01f));

    // Conducting: clamped near the forward voltage.
    const float clamped = measureDC(circuit, -5.0f, 0.01);
    CHECK(clamped < -0.4f);
    CHECK(clamped > -0.9f);
}

TEST_CASE("A diode clipper clamps large signals and passes small ones", "[circuit][diode]")
{
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
    clipper.prepare(sampleRate);

    // Well below the forward voltage the stage is essentially linear.
    const float small = measureResponse(clipper, 1000.0, 0.01f);
    CHECK(small == Catch::Approx(0.01f).margin(0.001f));

    // Hard driven, the output stops following the input.
    const float loud = measureResponse(clipper, 1000.0, 10.0f);
    const float louder = measureResponse(clipper, 1000.0, 100.0f);

    CHECK(loud < 1.0f);
    CHECK(louder < 1.2f);
    CHECK(louder - loud < 0.15f); // a 20 dB input increase barely moves the output

    CHECK(clipper.getNonConvergenceCount() == 0);
}

TEST_CASE("Diode type changes the clipping threshold", "[circuit][diode]")
{
    auto measurePeak = [](const Circuit::DiodeModel& model, int seriesCount)
    {
        auto clipper = makeClipperWithDiode(model, seriesCount, seriesCount);
        clipper.prepare(sampleRate);
        return measureResponse(clipper, 1000.0, 10.0f);
    };

    const float germanium = measurePeak(Circuit::DiodeModel::germanium(), 1);
    const float silicon = measurePeak(Circuit::DiodeModel::silicon(), 1);
    const float led = measurePeak(Circuit::DiodeModel::led(), 1);
    const float twoSilicon = measurePeak(Circuit::DiodeModel::silicon(), 2);

    // Ordering follows forward voltage: Ge < Si < 2x Si < LED.
    CHECK(germanium < silicon);
    CHECK(silicon < twoSilicon);
    CHECK(twoSilicon < led);

    // And the absolute values land where the datasheets say they should.
    CHECK(silicon > 0.5f);
    CHECK(silicon < 0.9f);
    CHECK(led > 1.5f);
}

TEST_CASE("Asymmetric clipping is asymmetric", "[circuit][diode]")
{
    // Two diodes one way, one the other -- the classic asymmetric arrangement.
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 2, 1);
    clipper.prepare(sampleRate);

    const float positive = measureDC(clipper, 10.0f, 0.01);
    const float negative = measureDC(clipper, -10.0f, 0.01);

    // The two-diode side clips later, so the positive excursion is bigger.
    CHECK(positive > std::abs(negative) + 0.2f);
}

TEST_CASE("The nonlinear solve stays converged and finite under abuse", "[circuit][diode]")
{
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
    clipper.prepare(sampleRate);

    int worstIterationCount = 0;

    // Alternating full-scale steps: the worst case for a warm-started Newton,
    // since every sample starts from the opposite rail's solution.
    for (int i = 0; i < 20000; ++i)
    {
        const float in = (i % 2 == 0) ? 200.0f : -200.0f;
        const float out = clipper.process(in);

        REQUIRE(std::isfinite(out));
        worstIterationCount = std::max(worstIterationCount, clipper.getLastIterationCount());
    }

    CHECK(clipper.getNonConvergenceCount() == 0);
    INFO("worst-case Newton iterations: " << worstIterationCount);
    CHECK(worstIterationCount < 20);
}

TEST_CASE("Newton warm-starts to a low iteration count on musical signal", "[circuit][diode]")
{
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
    clipper.prepare(sampleRate);

    const double phaseStep = 2.0 * std::numbers::pi * 440.0 / sampleRate;
    long totalIterations = 0;
    constexpr int numSamples = 48000;

    for (int i = 0; i < numSamples; ++i)
    {
        clipper.process(static_cast<float>(2.0 * std::sin(phaseStep * i)));
        totalIterations += clipper.getLastIterationCount();
    }

    const double averageIterations = static_cast<double>(totalIterations) / numSamples;
    INFO("average Newton iterations: " << averageIterations);

    // The DK loop tests convergence at the top, so the count includes the final
    // pass that confirms it -- roughly three-and-a-half actual Newton steps plus
    // one. That last pass is what leaves the port currents evaluated at the port
    // voltages the solve exits with, which the node-voltage recovery depends on.
    CHECK(averageIterations < 5.0);
    CHECK(clipper.getNonConvergenceCount() == 0);
}

//==============================================================================
// Voltage sources
//==============================================================================

TEST_CASE("A voltage source holds its voltage and reports its current", "[circuit][source]")
{
    Circuit circuit;
    const auto supply = circuit.addVoltageSource("vcc", "gnd", 9.0);
    circuit.addResistor("vcc", "mid", 1000.0);
    circuit.addResistor("mid", "gnd", 1000.0);
    circuit.setInputNode("in"); // unused by this circuit, but the engine wants one
    circuit.setOutputNode("mid");
    circuit.prepare(sampleRate);

    // One row per unknown node (vcc, mid, in is known) plus one for the source.
    CHECK(circuit.getSystemSize() == circuit.getNumUnknowns() + 1);

    CHECK(circuit.process(0.0f) == Catch::Approx(4.5f).epsilon(1.0e-5));
    CHECK(circuit.getNodeVoltage("vcc") == Catch::Approx(9.0).epsilon(1.0e-9));

    // 9 V across 2k. Negative because the source is delivering, not absorbing.
    CHECK(circuit.getSourceCurrent(supply) == Catch::Approx(-4.5e-3).epsilon(1.0e-6));
}

TEST_CASE("A voltage source can float between two nodes", "[circuit][source]")
{
    // Nothing here is ground-referenced except through the load, so this only
    // solves if the source really does get its own branch-current unknown.
    Circuit circuit;
    circuit.addResistor("in", "a", 1000.0);
    circuit.addVoltageSource("a", "b", 2.0);
    circuit.addResistor("b", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("b");
    circuit.prepare(sampleRate);

    // Loop: (in - 2 V) split across two equal resistors, so V(b) = (in - 2)/2.
    CHECK(circuit.process(0.0f) == Catch::Approx(-1.0f).epsilon(1.0e-5));
    CHECK(circuit.process(4.0f) == Catch::Approx(1.0f).epsilon(1.0e-5));
    CHECK(circuit.getNodeVoltage("a") - circuit.getNodeVoltage("b") == Catch::Approx(2.0).epsilon(1.0e-9));
}

TEST_CASE("Supply voltage can be changed at runtime", "[circuit][source]")
{
    Circuit circuit;
    const auto supply = circuit.addVoltageSource("vcc", "gnd", 9.0);
    circuit.addResistor("vcc", "mid", 1000.0);
    circuit.addResistor("mid", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("mid");
    circuit.prepare(sampleRate);

    CHECK(circuit.process(0.0f) == Catch::Approx(4.5f).epsilon(1.0e-5));

    circuit.setVoltage(supply, 4.0); // a dying battery
    CHECK(circuit.process(0.0f) == Catch::Approx(2.0f).epsilon(1.0e-5));
}

//==============================================================================
// Transistors
//==============================================================================

TEST_CASE("A transistor stage finds a sane bias point", "[circuit][transistor]")
{
    auto booster = Circuits::makeTransistorBooster();
    booster.prepare(sampleRate);

    REQUIRE(booster.foundOperatingPoint());

    const double vBase = booster.getNodeVoltage("base");
    const double vEmitter = booster.getNodeVoltage("emitter");
    const double vCollector = booster.getNodeVoltage("collector");

    INFO("Vb = " << vBase << "  Ve = " << vEmitter << "  Vc = " << vCollector);

    // The divider sets the base near 9 * 22k/122k, pulled down slightly by base
    // current flowing through the 100k/22k Thevenin resistance.
    CHECK(vBase == Catch::Approx(1.58).margin(0.08));

    // One base-emitter drop below that -- silicon, so around 0.65 V.
    CHECK(vBase - vEmitter == Catch::Approx(0.66).margin(0.04));

    // And the collector lands partway up the supply, which is the whole point of
    // biasing: room to swing in both directions.
    CHECK(vCollector > 3.0);
    CHECK(vCollector < 6.0);

    // Emitter current through Re should match the collector current through Rc,
    // to within the base current the transistor keeps for itself.
    const double emitterCurrent = vEmitter / 1000.0;
    const double collectorCurrent = (9.0 - vCollector) / 4700.0;
    CHECK(collectorCurrent == Catch::Approx(emitterCurrent).epsilon(0.02));
}

TEST_CASE("A transistor stage amplifies", "[circuit][transistor]")
{
    auto booster = Circuits::makeTransistorBooster();
    booster.prepare(sampleRate);

    // Small enough to stay well inside the linear region.
    const float out = measureResponse(booster, 1000.0, 0.01f, 0.2);
    const double gain = out / 0.01;

    INFO("gain = " << gain);

    // Av = Rc / (Re + re'), where re' is about 26 mV over the emitter current.
    // With Rc = 4.7k, Re = 1k and Ie near 0.9 mA that's about 4.6.
    CHECK(gain == Catch::Approx(4.6).margin(0.5));
    CHECK(booster.getNonConvergenceCount() == 0);
}

TEST_CASE("A transistor stage clips when overdriven", "[circuit][transistor]")
{
    auto booster = Circuits::makeTransistorBooster();
    booster.prepare(sampleRate);

    // Way past what a 9 V rail can swing.
    const float loud = measureResponse(booster, 1000.0, 2.0f, 0.5);
    const float louder = measureResponse(booster, 1000.0, 20.0f, 0.5);

    INFO("loud = " << loud << "  louder = " << louder);

    // The rail is the hard limit: the collector can't go above the supply or
    // below the emitter, whatever you feed it.
    CHECK(loud < 9.0f);
    CHECK(louder < 9.0f);

    // Ten times the input buys well under twice the output. It isn't a flat
    // ceiling because the input coupling capacitor charges up as the
    // base-emitter junction clamps the drive, dragging the bias point with it --
    // that's blocking distortion, and it's what an overdriven stage really does.
    CHECK(louder < loud * 2.0f);
    CHECK(booster.getNonConvergenceCount() == 0);
}

TEST_CASE("A PNP stage mirrors an NPN one", "[circuit][transistor]")
{
    // Same topology, every polarity flipped: negative supply, PNP device. The
    // bias point should be the NPN one reflected about ground.
    auto build = [](Circuit::BjtModel model, double supplyVolts)
    {
        Circuit circuit;
        circuit.addVoltageSource("vcc", "gnd", supplyVolts);
        circuit.addResistor("vcc", "base", 100000.0);
        circuit.addResistor("base", "gnd", 22000.0);
        circuit.addResistor("vcc", "collector", 4700.0);
        circuit.addResistor("emitter", "gnd", 1000.0);
        circuit.addTransistor("base", "collector", "emitter", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("collector");
        circuit.prepare(sampleRate);
        return circuit;
    };

    auto npn = build(Circuit::BjtModel::npnSilicon(), 9.0);
    auto pnp = build(Circuit::BjtModel::pnpSilicon(), -9.0);

    REQUIRE(npn.foundOperatingPoint());
    REQUIRE(pnp.foundOperatingPoint());

    INFO("NPN Vc = " << npn.getNodeVoltage("collector") << "  PNP Vc = " << pnp.getNodeVoltage("collector"));

    // Not identical -- the 2N3906 isn't an exact mirror of the 2N3904 -- but the
    // PNP has to sit on the negative side of the rail, biased the same way up.
    CHECK(pnp.getNodeVoltage("collector") < 0.0);
    CHECK(pnp.getNodeVoltage("collector") > -9.0);
    CHECK(pnp.getNodeVoltage("emitter") - pnp.getNodeVoltage("base") == Catch::Approx(0.66).margin(0.06));
}

TEST_CASE("A germanium transistor turns on earlier than silicon", "[circuit][transistor]")
{
    auto vbeAt = [](Circuit::BjtModel model)
    {
        Circuit circuit;
        circuit.addVoltageSource("vcc", "gnd", 9.0);
        circuit.addResistor("vcc", "base", 470000.0); // feeds roughly 20 uA of base current
        circuit.addResistor("vcc", "collector", 4700.0);
        circuit.addTransistor("base", "collector", "gnd", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("collector");
        circuit.prepare(sampleRate);
        return circuit.getNodeVoltage("base");
    };

    const double silicon = vbeAt(Circuit::BjtModel::npnSilicon());
    const double germanium = vbeAt(Circuit::BjtModel::npnGermanium());

    INFO("silicon Vbe = " << silicon << "  germanium Vbe = " << germanium);

    CHECK(silicon > 0.55);
    CHECK(silicon < 0.85);
    CHECK(germanium < silicon - 0.2); // germanium conducts a few hundred mV earlier
    CHECK(germanium > 0.05);
}

//==============================================================================
// Engine behaviour
//==============================================================================

TEST_CASE("reset() makes processing repeatable", "[circuit]")
{
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
    clipper.prepare(sampleRate);

    std::vector<float> first, second;
    const double phaseStep = 2.0 * std::numbers::pi * 220.0 / sampleRate;

    for (int pass = 0; pass < 2; ++pass)
    {
        clipper.reset();
        auto& target = pass == 0 ? first : second;

        for (int i = 0; i < 512; ++i)
            target.push_back(clipper.process(static_cast<float>(3.0 * std::sin(phaseStep * i))));
    }

    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == Catch::Approx(second[i]));
}

TEST_CASE("The tone stack still responds to its controls", "[circuit][tonestack]")
{
    auto toneStack = Circuits::makeClassicToneStack();
    toneStack.circuit.prepare(sampleRate);

    toneStack.setControls(1.0f, 0.0f, 0.0f);
    const float bassUpAtLowFreq = measureResponse(toneStack.circuit, 100.0, 1.0f);

    toneStack.setControls(0.0f, 0.0f, 0.0f);
    const float bassDownAtLowFreq = measureResponse(toneStack.circuit, 100.0, 1.0f);

    CHECK(bassUpAtLowFreq > bassDownAtLowFreq);

    toneStack.setControls(0.0f, 0.0f, 1.0f);
    const float trebleUpAtHighFreq = measureResponse(toneStack.circuit, 5000.0, 1.0f);

    toneStack.setControls(0.0f, 0.0f, 0.0f);
    const float trebleDownAtHighFreq = measureResponse(toneStack.circuit, 5000.0, 1.0f);

    CHECK(trebleUpAtHighFreq > trebleDownAtHighFreq);
}

TEST_CASE("process() does not allocate", "[circuit][realtime]")
{
    // The whole point of sizing everything in prepare() is that the audio thread
    // never touches the allocator -- including when a knob move dirties the
    // matrix, and including the Newton path.
    auto clipper = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
    clipper.prepare(sampleRate);

    auto toneStack = Circuits::makeClassicToneStack();
    toneStack.circuit.prepare(sampleRate);

    // Voltage sources and transistors both add code to the per-sample path, so
    // this has to cover them too.
    auto booster = Circuits::makeTransistorBooster();
    booster.prepare(sampleRate);

    std::vector<float> block(512);
    const double phaseStep = 2.0 * std::numbers::pi * 440.0 / sampleRate;
    for (size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<float>(5.0 * std::sin(phaseStep * static_cast<double>(i)));

    allocationCount.store(0);
    countingAllocations.store(true);

    for (int pass = 0; pass < 8; ++pass)
    {
        const float knob = static_cast<float>(pass) / 8.0f;
        toneStack.setControls(knob, knob, knob);

        for (float sample : block)
        {
            toneStack.circuit.process(sample);
            clipper.process(sample);
            booster.process(sample);
        }
    }

    countingAllocations.store(false);

    CHECK(allocationCount.load() == 0);
}

TEST_CASE("Block processing matches sample-by-sample processing", "[circuit]")
{
    auto makeClipper = []
    {
        auto c = makeClipperWithDiode(Circuit::DiodeModel::silicon(), 1, 1);
        c.prepare(sampleRate);
        return c;
    };

    auto perSample = makeClipper();
    auto perBlock = makeClipper();

    std::vector<float> input(1024);
    const double phaseStep = 2.0 * std::numbers::pi * 330.0 / sampleRate;
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(4.0 * std::sin(phaseStep * static_cast<double>(i)));

    std::vector<float> blockOutput = input;
    perBlock.process(blockOutput.data(), static_cast<int>(blockOutput.size()));

    for (size_t i = 0; i < input.size(); ++i)
        REQUIRE(perSample.process(input[i]) == Catch::Approx(blockOutput[i]));
}

//==============================================================================
// Valves
//==============================================================================

TEST_CASE("A 12AX7 stage biases and amplifies like the textbook", "[circuit][valve]")
{
    auto triode = Circuits::makeTriodeStage();
    auto& stage = triode.circuit;
    stage.prepare(sampleRate);

    REQUIRE(stage.foundOperatingPoint());

    const double vPlate = stage.getNodeVoltage("plate");
    const double vCathode = stage.getNodeVoltage("cathode");

    INFO("Vplate = " << vPlate << "  Vcathode = " << vCathode);

    // Cathode bias: plate current through Rk lifts the cathode, which is the
    // same as holding the grid that far below it. A volt or two is right for a
    // 12AX7 with a 1k5 cathode resistor.
    CHECK(vCathode > 0.8);
    CHECK(vCathode < 2.5);

    // And the plate has to sit where that current puts it: B+ minus Ik*Ra.
    const double cathodeCurrent = vCathode / 1500.0;
    CHECK(vPlate == Catch::Approx(300.0 - cathodeCurrent * 100000.0).epsilon(0.02));

    // Gain is mu*Ra/(Ra+rp). A 12AX7 near 1 mA has rp around 62k, so with a
    // 100k plate load that lands near 60.
    const double gain = measureResponse(stage, 1000.0, 0.01f, 0.5) / 0.01;
    INFO("gain = " << gain);
    CHECK(gain > 45.0);
    CHECK(gain < 75.0);
    CHECK(stage.getNonConvergenceCount() == 0);
}

TEST_CASE("A triode compresses and clips asymmetrically", "[circuit][valve]")
{
    auto triode = Circuits::makeTriodeStage();
    auto& stage = triode.circuit;
    stage.prepare(sampleRate);

    // Positive and negative peaks separately -- the asymmetry is the point, and
    // a magnitude-only measurement would hide it.
    auto drive = [&stage](float amplitude)
    {
        const double phaseStep = 2.0 * std::numbers::pi * 1000.0 / sampleRate;
        double high = -1.0e9, low = 1.0e9;

        for (int i = 0; i < 24000; ++i)
        {
            const double v = stage.process(static_cast<float>(amplitude * std::sin(phaseStep * i)));

            if (i > 12000)
            {
                high = std::max(high, v);
                low = std::min(low, v);
            }
        }

        return std::pair{high, low};
    };

    const auto [quietHigh, quietLow] = drive(0.1f);
    const auto [loudHigh, loudLow] = drive(5.0f);

    INFO("quiet +" << quietHigh << " / " << quietLow << "   loud +" << loudHigh << " / " << loudLow);

    // Small signal: symmetric, and at the stage's full gain.
    CHECK(quietHigh / -quietLow == Catch::Approx(1.0).margin(0.05));

    // Fifty times the input buys about twenty-five times the output: the stage's
    // gain has halved, from around 60 to around 30.
    const double quietGain = std::max(quietHigh, -quietLow) / 0.1;
    const double loudGain = std::max(loudHigh, -loudLow) / 5.0;

    INFO("small-signal gain " << quietGain << " -> large-signal gain " << loudGain);
    CHECK(loudGain < 0.6 * quietGain);

    // And the two halves no longer match. The plate can swing a long way down
    // as the valve saturates, but far less up: driving the grid positive makes
    // it draw current, which charges the input coupling capacitor and drags the
    // bias towards cutoff. That's blocking distortion, and it's why the
    // positive peak actually shrinks as the drive goes up.
    CHECK(loudHigh < -loudLow * 0.6);

    // Nothing may leave the supply rails.
    CHECK(-loudLow < 300.0);
    CHECK(stage.getNonConvergenceCount() == 0);
}

TEST_CASE("A pentode stage's currents add up", "[circuit][valve]")
{
    auto stage = Circuits::makePentodeStage();
    stage.prepare(sampleRate);

    REQUIRE(stage.foundOperatingPoint());
    REQUIRE(stage.getPortCount() == 3); // grid, screen, plate

    const double vPlate = stage.getNodeVoltage("plate");
    const double vScreen = stage.getNodeVoltage("screen");
    const double vCathode = stage.getNodeVoltage("cathode");

    INFO("Vplate = " << vPlate << "  Vscreen = " << vScreen << "  Vcathode = " << vCathode);

    const double plateCurrent = (400.0 - vPlate) / 4000.0;
    const double screenCurrent = (400.0 - vScreen) / 1000.0;
    const double cathodeCurrent = vCathode / 680.0;

    // Everything that goes in at the plate and screen comes out at the cathode.
    // This is the check that catches a sign error anywhere in the 3x3 Jacobian.
    CHECK(cathodeCurrent == Catch::Approx(plateCurrent + screenCurrent).epsilon(0.01));

    // Screen current is a small fraction of plate current -- the screen
    // accelerates electrons, it isn't supposed to collect many of them.
    CHECK(screenCurrent > 0.02 * plateCurrent);
    CHECK(screenCurrent < 0.25 * plateCurrent);

    CHECK(stage.getNonConvergenceCount() == 0);
}

TEST_CASE("A pentode strapped as a triode still solves", "[circuit][valve]")
{
    // Screen tied to plate: the "triode mode" switch some amps have. It makes
    // two of the device's ports share the same node pair, which leaves the DK
    // port impedance matrix singular -- the Newton Jacobian I + K*di/dv stays
    // invertible, but it's worth pinning down that it does.
    Circuit circuit;
    circuit.addVoltageSource("b+", "gnd", 300.0);
    circuit.addResistor("b+", "plate", 10000.0);
    circuit.addResistor("grid", "gnd", 220000.0);
    circuit.addResistor("cathode", "gnd", 680.0);
    circuit.addCapacitor("in", "grid", 22.0e-9);
    circuit.addPentode("plate", "plate", "grid", "cathode", Circuit::PentodeModel::el34());
    circuit.setInputNode("in");
    circuit.setOutputNode("plate");
    circuit.prepare(sampleRate);

    INFO("Vplate = " << circuit.getNodeVoltage("plate")
                     << "  Vcathode = " << circuit.getNodeVoltage("cathode"));

    CHECK(circuit.foundOperatingPoint());
    CHECK(circuit.getNodeVoltage("plate") > 0.0);
    CHECK(circuit.getNodeVoltage("plate") < 300.0);

    for (int i = 0; i < 4800; ++i)
        REQUIRE(std::isfinite(circuit.process(0.05f * std::sin(0.1f * static_cast<float>(i)))));

    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("A valve rectifier sags under load", "[circuit][valve][rectifier]")
{
    auto measureRail = [](double loadOhms)
    {
        auto psu = Circuits::makeValveRectifierSupply(loadOhms);
        psu.prepare(sampleRate);

        double rail = 0.0;
        for (int i = 0; i < 24000; ++i)
            rail = psu.process(0.0f);

        CHECK(psu.getNonConvergenceCount() == 0);
        return rail;
    };

    const double light = measureRail(100000.0);
    const double medium = measureRail(20000.0);
    const double heavy = measureRail(5000.0);

    INFO("B+ light = " << light << "  medium = " << medium << "  heavy = " << heavy);

    // The whole point: the harder the amp pulls, the lower the rail goes. A
    // silicon rectifier would hold far closer to the peak throughout.
    CHECK(light > medium);
    CHECK(medium > heavy);
    CHECK(light - heavy > 50.0);

    // And it still rectifies -- the rail is up near the winding's peak, not
    // hovering near its zero average.
    CHECK(light > 350.0);
    CHECK(light < 460.0);
}

//==============================================================================
// Switches
//==============================================================================

TEST_CASE("A switch shorts and opens convincingly", "[circuit][switch]")
{
    // A divider with a switch across the lower leg. Closed it should short that
    // leg out; open it should be as if it weren't there at all. Both are checked
    // against the exact answer, because "close enough to a short" is the only
    // thing a resistor-based switch can be, and it needs to be very close.
    Circuit circuit;
    circuit.addResistor("in", "out", 10000.0);
    circuit.addResistor("out", "gnd", 10000.0);
    const auto bypass = circuit.addSwitch("out", "gnd", false);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // Open: an ordinary 10k/10k divider.
    CHECK(circuit.process(1.0f) == Catch::Approx(0.5f).epsilon(1.0e-4));

    // Closed: the lower leg is shorted, so the output is pinned near ground.
    bypass.setClosed(circuit, true);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.0f).margin(1.0e-5));

    // And back again -- the matrix has to be re-stamped and re-factorised each
    // time, which is what the dirty flag is for.
    bypass.setClosed(circuit, false);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.5f).epsilon(1.0e-4));
}

TEST_CASE("A series switch makes and breaks a connection", "[circuit][switch]")
{
    // The classic cathode-bypass arrangement: a capacitor that's only in circuit
    // when the switch is closed.
    Circuit circuit;
    circuit.addResistor("in", "out", 10000.0);
    circuit.addResistor("out", "gnd", 10000.0);
    circuit.addCapacitor("out", "capBottom", 1.0e-6);
    const auto engage = circuit.addSwitch("capBottom", "gnd", false);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // Open: the capacitor is stranded, so this is a flat divider at any frequency.
    CHECK(measureResponse(circuit, 5000.0, 1.0f) == Catch::Approx(0.5f).margin(0.01));

    // Closed: the capacitor now shunts the lower leg and the top end rolls off.
    engage.setClosed(circuit, true);
    CHECK(measureResponse(circuit, 5000.0, 1.0f) < 0.2f);

    // Far below the corner the capacitor is effectively open again, so the
    // divider comes back. 1 uF into 10k corners at 16 Hz, so this has to be
    // measured well under that -- at 20 Hz it's still shunting hard.
    CHECK(measureResponse(circuit, 2.0, 1.0f, 3.0) == Catch::Approx(0.5f).margin(0.02));
}

TEST_CASE("A changeover switch connects one throw at a time", "[circuit][switch]")
{
    // Common node fed from the input; the two throws are different resistances
    // to ground, so the divider ratio says which one is live.
    Circuit circuit;
    circuit.addResistor("in", "common", 10000.0);
    const auto selector = circuit.addChangeoverSwitch("common", "a", "b");
    circuit.addResistor("a", "gnd", 10000.0);
    circuit.addResistor("b", "gnd", 90000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("common");
    circuit.prepare(sampleRate);

    // Throw A: 10k/10k.
    selector.select(circuit, false);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.5f).epsilon(1.0e-3));

    // Throw B: 10k/90k. If A were still connected the answer would be nearer
    // 0.47, so this also pins down that the two really are exclusive.
    selector.select(circuit, true);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.9f).epsilon(1.0e-3));

    selector.select(circuit, false);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.5f).epsilon(1.0e-3));
}

TEST_CASE("A switch works alongside nonlinear devices", "[circuit][switch]")
{
    // Switching the clipping diodes in and out of a stage, which is what a dirt
    // pedal's mode switch does. The switch is linear but the circuit isn't, so
    // this covers the DK precomputation being redone on the toggle.
    Circuit circuit;
    circuit.addResistor("in", "out", 1300.0);
    circuit.addResistor("out", "gnd", 100000.0);
    circuit.addDiode("out", "clipBottom", Circuit::DiodeModel::silicon());
    circuit.addDiode("clipBottom", "out", Circuit::DiodeModel::silicon());
    const auto clipping = circuit.addSwitch("clipBottom", "gnd", false);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // Open: no clipping path, so a big signal passes almost untouched.
    const float clean = measureResponse(circuit, 1000.0, 5.0f, 0.2);
    CHECK(clean > 4.5f);

    // Closed: the diodes clamp it.
    clipping.setClosed(circuit, true);
    const float dirty = measureResponse(circuit, 1000.0, 5.0f, 0.2);

    INFO("clean = " << clean << "  dirty = " << dirty);
    CHECK(dirty < 1.0f);
    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("The triode stage's fat switch changes its gain", "[circuit][switch][valve]")
{
    auto triode = Circuits::makeTriodeStage();
    auto& stage = triode.circuit;
    stage.prepare(sampleRate);

    // Closed: Ck bypasses the cathode resistor, so the stage runs at full gain.
    triode.cathodeBypass.setClosed(stage, true);
    const double bypassed = measureResponse(stage, 1000.0, 0.01f, 0.5) / 0.01;

    // Open: Rk is left unbypassed and degenerates the stage badly.
    triode.cathodeBypass.setClosed(stage, false);
    const double degenerated = measureResponse(stage, 1000.0, 0.01f, 0.5) / 0.01;

    INFO("bypassed gain " << bypassed << ", unbypassed " << degenerated);

    CHECK(bypassed > 45.0);
    CHECK(degenerated < bypassed * 0.6);
    CHECK(stage.getNonConvergenceCount() == 0);
}

//==============================================================================
// Three-stage valve preamp
//==============================================================================

TEST_CASE("The three-stage preamp biases all its valves", "[circuit][preamp]")
{
    auto amp = Circuits::makeThreeStagePreamp();
    auto& circuit = amp.circuit;
    circuit.prepare(sampleRate);

    REQUIRE(circuit.foundOperatingPoint());
    CHECK(circuit.getReversedCapacitorCount() == 0);

    const double v1Plate = circuit.getNodeVoltage("v1plate");
    const double v1Cathode = circuit.getNodeVoltage("v1cathode");
    const double v2Plate = circuit.getNodeVoltage("v2plate");
    const double v2Cathode = circuit.getNodeVoltage("v2cathode");
    const double v3Cathode = circuit.getNodeVoltage("v3cathode");

    INFO("V1 " << v1Plate << "/" << v1Cathode << "  V2 " << v2Plate << "/" << v2Cathode
                << "  V3 cathode " << v3Cathode);

    // Each stage's plate has to sit where its own cathode current puts it:
    // B+ minus Ik * Ra, with Ik read off the cathode resistor.
    CHECK(v1Plate == Catch::Approx(325.0 - (v1Cathode / 820.0) * 100000.0).epsilon(0.02));
    CHECK(v2Plate == Catch::Approx(325.0 - (v2Cathode / 820.0) * 100000.0).epsilon(0.02));

    // The cathode follower is direct-coupled, so its cathode has to land within
    // a volt of V2's plate -- that's what "no gain, just drive" means.
    CHECK(v3Cathode == Catch::Approx(v2Plate).margin(2.0));

    // And it has to be a long way up the rail, not near ground.
    CHECK(v3Cathode > 100.0);
    CHECK(v3Cathode < 300.0);
    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("The classic tone stack scoops the mids", "[circuit][preamp]")
{
    auto amp = Circuits::makeThreeStagePreamp();
    auto& circuit = amp.circuit;
    circuit.prepare(sampleRate);

    amp.setControls(1.0f, 0.5f, 0.5f, 0.5f);

    const double low = measureResponse(circuit, 100.0, 0.01f, 0.4) / 0.01;
    const double middle = measureResponse(circuit, 1000.0, 0.01f, 0.4) / 0.01;
    const double high = measureResponse(circuit, 5000.0, 0.01f, 0.4) / 0.01;

    INFO("100 Hz " << low << ", 1 kHz " << middle << ", 5 kHz " << high);

    // With everything at noon this stack has a deep notch in the middle -- it's
    // the defining feature of the classic passive tone stack, and the reason
    // the "flat" setting on these amps is anything but.
    CHECK(middle < low * 0.7);
    CHECK(middle < high * 0.7);

    // And each control has to move its own band.
    auto at = [&](double f, float bass, float mid, float treble)
    {
        amp.setControls(1.0f, bass, mid, treble);
        return measureResponse(circuit, f, 0.01f, 0.4) / 0.01;
    };

    CHECK(at(100.0, 1.0f, 0.5f, 0.5f) > at(100.0, 0.0f, 0.5f, 0.5f));
    CHECK(at(5000.0, 0.5f, 0.5f, 1.0f) > at(5000.0, 0.5f, 0.5f, 0.0f));
    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("The bright cap only works with the volume down", "[circuit][preamp][switch]")
{
    auto amp = Circuits::makeThreeStagePreamp();
    auto& circuit = amp.circuit;
    circuit.prepare(sampleRate);

    auto highEndGain = [&](float volume, bool bright)
    {
        amp.setControls(volume, 0.5f, 0.5f, 0.5f);
        amp.brightSwitch.setClosed(circuit, bright);
        return measureResponse(circuit, 5000.0, 0.01f, 0.4) / 0.01;
    };

    const double quietDull = highEndGain(0.2f, false);
    const double quietBright = highEndGain(0.2f, true);
    const double loudDull = highEndGain(1.0f, false);
    const double loudBright = highEndGain(1.0f, true);

    INFO("volume 0.2: " << quietDull << " -> " << quietBright
                        << "   volume 1.0: " << loudDull << " -> " << loudBright);

    // Turned down, the cap bypasses most of the pot's track and lifts the top
    // end substantially.
    CHECK(quietBright > quietDull * 1.5);

    // Wound fully up there's no track left to bypass, so the switch does
    // nothing at all. That's the real complaint about bright caps, and it falls
    // straight out of the topology rather than being modelled in.
    CHECK(loudBright == Catch::Approx(loudDull).epsilon(1.0e-3));
}

//==============================================================================
// Zener diodes, JFETs and op-amps
//==============================================================================

TEST_CASE("A Zener clamps at its breakdown voltage", "[circuit][zener]")
{
    for (double vz : {3.3, 5.1, 9.1})
    {
        Circuit circuit;
        circuit.addResistor("in", "out", 1000.0);
        circuit.addDiode("out", "gnd", Circuit::DiodeModel::zener(vz));
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);

        const double forward = measureDC(circuit, 20.0f, 0.02);
        const double reverse = measureDC(circuit, -20.0f, 0.02);

        INFO("Vz = " << vz << ": forward " << forward << ", reverse " << reverse);

        // Forwards it's an ordinary silicon diode, whatever its Zener voltage.
        CHECK(forward == Catch::Approx(0.72).margin(0.15));

        // Backwards it holds at Vz, a little above it because of the current the
        // series resistor is pushing through.
        CHECK(-reverse > vz);
        CHECK(-reverse < vz + 0.6);

        // That asymmetry is the point -- clipping one way at 0.7 V and the other
        // at Vz is what a single Zener gives you.
        CHECK(-reverse > forward * 3.0);
        CHECK(circuit.getNonConvergenceCount() == 0);
    }
}

TEST_CASE("A stack of Zeners breaks down at the stack's voltage", "[circuit][zener]")
{
    // seriesCount scales the forward junction, and used not to scale the reverse
    // one: a pair of Zeners clipped at two forward drops one way and at a single
    // Vz the other, which is neither diode anybody owns. Both directions belong
    // to the same stack.
    constexpr double vz = 5.1;

    auto clampAt = [] (int seriesCount)
    {
        Circuit circuit;
        circuit.addResistor("in", "out", 1000.0);
        circuit.addDiode("out", "gnd", Circuit::DiodeModel::zener(vz), seriesCount);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);

        const double reverse = -measureDC(circuit, -40.0f, 0.02);
        CHECK(circuit.getNonConvergenceCount() == 0);
        return reverse;
    };

    const double one = clampAt(1);
    const double two = clampAt(2);
    const double three = clampAt(3);

    INFO("1x " << one << ", 2x " << two << ", 3x " << three);

    // Each holds at n*Vz, a little above because of the current the series
    // resistor pushes through -- the same allowance the single-Zener case takes.
    CHECK(one > vz);
    CHECK(one < vz + 0.6);

    CHECK(two > 2.0 * vz);
    CHECK(two < 2.0 * vz + 1.2);

    CHECK(three > 3.0 * vz);
    CHECK(three < 3.0 * vz + 1.8);

    // And the knee widens with the stack rather than staying a single diode's:
    // the steps are even, which a scaled offset alone would not give.
    CHECK((two - one) == Catch::Approx(three - two).margin(0.35));
}

TEST_CASE("A JFET stage biases and amplifies", "[circuit][jfet]")
{
    Circuit circuit;
    circuit.addVoltageSource("vcc", "gnd", 9.0);
    circuit.addResistor("vcc", "drain", 22000.0);
    circuit.addResistor("source", "gnd", 2200.0);
    circuit.addResistor("gate", "gnd", 1000000.0);
    circuit.addCapacitor("in", "gate", 100.0e-9);
    circuit.addPolarisedCapacitor("source", "gnd", 47.0e-6, 0.2);
    circuit.addJfet("drain", "gate", "source", Circuit::JfetModel::j201());
    circuit.addCapacitor("drain", "out", 100.0e-9);
    circuit.addResistor("out", "gnd", 1000000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    REQUIRE(circuit.foundOperatingPoint());

    const double vDrain = circuit.getNodeVoltage("drain");
    const double vSource = circuit.getNodeVoltage("source");

    INFO("Vd = " << vDrain << "  Vs = " << vSource);

    // Self-biased: drain current through the source resistor lifts the source,
    // which puts the gate that far below it. Depletion mode, so no divider needed.
    CHECK(vSource > 0.1);
    CHECK(vSource < 1.0);
    CHECK(vDrain == Catch::Approx(9.0 - (vSource / 2200.0) * 22000.0).epsilon(0.03));

    // Gain is gm*Rd, and gm for a square law is 2*beta*(Vgs - Vto).
    const double gain = measureResponse(circuit, 1000.0, 0.01f, 0.3) / 0.01;
    INFO("gain = " << gain);
    CHECK(gain > 10.0);
    CHECK(gain < 30.0);
    CHECK(circuit.getNonConvergenceCount() == 0);
}

TEST_CASE("An op-amp gives its textbook closed-loop gain", "[circuit][opamp]")
{
    // Non-inverting amplifier: gain should be 1 + Rf/Rg, set by the resistors
    // and essentially independent of the op-amp itself. That's the whole point
    // of feedback, and it's the sharpest test of the model.
    auto gainOf = [](double rf, double rg)
    {
        Circuit circuit;
        circuit.addVoltageSource("bias", "gnd", 4.5);
        circuit.addCapacitor("in", "inp", 1.0e-6);
        circuit.addResistor("bias", "inp", 1.0e6);
        circuit.addResistor("out", "inm", rf);
        circuit.addCapacitor("inm", "rgTop", 100.0e-6);
        circuit.addResistor("rgTop", "bias", rg);
        circuit.addOpAmp("U1", "inp", "inm", "out", Circuit::OpAmpModel::tl072());
        circuit.addResistor("out", "gnd", 1.0e6);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);
        circuit.setOutputOffsetToOperatingPoint(); // it rests at the 4.5 V bias

        CHECK(circuit.getNonConvergenceCount() == 0);
        return measureResponse(circuit, 1000.0, 0.02f, 0.5) / 0.02;
    };

    CHECK(gainOf(10000.0, 10000.0) == Catch::Approx(2.0).epsilon(0.02));
    CHECK(gainOf(90000.0, 10000.0) == Catch::Approx(10.0).epsilon(0.02));
    CHECK(gainOf(990000.0, 10000.0) == Catch::Approx(100.0).epsilon(0.02));
}

TEST_CASE("An op-amp clips at its supply rails", "[circuit][opamp]")
{
    Circuit circuit;
    circuit.addVoltageSource("bias", "gnd", 4.5);
    circuit.addCapacitor("in", "inp", 1.0e-6);
    circuit.addResistor("bias", "inp", 1.0e6);
    circuit.addResistor("out", "inm", 90000.0);
    circuit.addCapacitor("inm", "rgTop", 100.0e-6);
    circuit.addResistor("rgTop", "bias", 10000.0);
    circuit.addOpAmp("U1", "inp", "inm", "out", Circuit::OpAmpModel::tl072());
    circuit.addResistor("out", "gnd", 1.0e6);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    // Built out of primitives, the op-amp's only nonlinearity is its two clamp
    // diodes, so DK handles it like any other circuit. The earlier single-device
    // model forced the full-matrix path here.
    REQUIRE(circuit.getSolverStrategy() == Circuit::SolverStrategy::DiscreteK);

    const double phaseStep = 2.0 * std::numbers::pi * 1000.0 / sampleRate;
    double lowest = 1.0e30;
    double highest = -1.0e30;

    for (int i = 0; i < 19200; ++i)
    {
        circuit.process(static_cast<float>(0.5 * std::sin(phaseStep * i)));

        if (i > 9600)
        {
            const double v = circuit.getNodeVoltage("out");
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
    }

    INFO("V(out) spans " << lowest << " to " << highest);

    // Rails at 0 and 9 with 1.5 V of headroom puts the output near 1.5 to 7.5.
    // The clamp is diodes, so it's soft -- the output eases past a little as it
    // is driven harder, the way a real output stage does, rather than stopping
    // dead. What it must not do is approach the rails themselves.
    CHECK(lowest > 0.6);
    CHECK(lowest < 2.2);
    CHECK(highest > 6.8);
    CHECK(highest < 8.4);
    CHECK(highest - lowest > 4.0); // and it really is using the range
}

//==============================================================================
// Diode models
//==============================================================================

TEST_CASE("Diode models hit their forward voltages", "[circuit][diode]")
{
    // Drive through a resistor large enough that it, not the diode, sets the
    // current, then read the drop.
    auto forwardVoltage = [](const Circuit::DiodeModel& model, double milliamps)
    {
        constexpr double supply = 100.0;

        Circuit circuit;
        circuit.addResistor("in", "out", supply / (milliamps * 1.0e-3));
        circuit.addDiode("out", "gnd", model);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);

        return static_cast<double>(measureDC(circuit, static_cast<float>(supply), 0.05));
    };

    CHECK(forwardVoltage(Circuit::DiodeModel::d1n4148(), 1.0) == Catch::Approx(0.58).margin(0.03));
    CHECK(forwardVoltage(Circuit::DiodeModel::germanium(), 1.0) == Catch::Approx(0.33).margin(0.03));
    CHECK(forwardVoltage(Circuit::DiodeModel::schottky(), 1.0) == Catch::Approx(0.30).margin(0.03));
    CHECK(forwardVoltage(Circuit::DiodeModel::redLed(), 1.0) == Catch::Approx(1.56).margin(0.05));
    CHECK(forwardVoltage(Circuit::DiodeModel::greenLed(), 1.0) == Catch::Approx(1.93).margin(0.05));
    CHECK(forwardVoltage(Circuit::DiodeModel::blueLed(), 1.0) == Catch::Approx(3.18).margin(0.05));

    // The emission coefficient shows up as how much the forward voltage moves
    // per decade of current -- the knee. An LED's is several times silicon's,
    // which is why it clips so much more gradually.
    auto knee = [&forwardVoltage](const Circuit::DiodeModel& model)
    {
        return forwardVoltage(model, 1.0) - forwardVoltage(model, 0.1);
    };

    const double siliconKnee = knee(Circuit::DiodeModel::d1n4148());
    INFO("silicon knee " << siliconKnee << " V/decade");

    CHECK(siliconKnee == Catch::Approx(0.104).margin(0.01));
    CHECK(knee(Circuit::DiodeModel::redLed()) > siliconKnee * 1.8);
    CHECK(knee(Circuit::DiodeModel::blueLed()) > siliconKnee * 3.5);
}

TEST_CASE("Swapping the overdrive's clipping diodes changes its ceiling", "[circuit][diode]")
{
    auto peakWith = [](const Circuit::DiodeModel& model)
    {
        auto ts = Circuits::makeMidHumpOverdrive();
        ts.setClippingDiodes(model);
        ts.circuit.prepare(sampleRate);
        ts.setControls(0.5f, 0.7f, 1.0f);

        CHECK(ts.circuit.getNonConvergenceCount() == 0);
        return measureResponse(ts.circuit, 1000.0, 0.5f, 0.4);
    };

    const float silicon = peakWith(Circuit::DiodeModel::d1n4148());
    const float red = peakWith(Circuit::DiodeModel::redLed());
    const float blue = peakWith(Circuit::DiodeModel::blueLed());

    INFO("silicon " << silicon << ", red LED " << red << ", blue LED " << blue);

    // Higher forward voltage means the loop clips later, so more gets through.
    CHECK(red > silicon * 1.5f);
    CHECK(blue > red * 1.5f);

    // And this is the reason the handles exist. addOpAmp() builds its rail
    // clamps from ordinary diodes and takes ids 0 and 1 for U1's, so the
    // clipping pair is 2 and 3. Assuming otherwise silently changes the wrong
    // parts and leaves the circuit sounding unaltered.
    auto ts = Circuits::makeMidHumpOverdrive();
    CHECK(ts.clipperUp == 2);
    CHECK(ts.clipperDown == 3);
}

//==============================================================================
// Ideal op-amps and transformers
//==============================================================================

TEST_CASE("An ideal op-amp gives exactly the textbook gain", "[circuit][nullor]")
{
    // Not "close to" -- exactly. A nullor is the textbook assumption, so an
    // inverting amp built on one has no error term to allow for.
    auto invertingGain = [](double rin, double rf)
    {
        Circuit circuit;
        circuit.addResistor("in", "sum", rin);
        circuit.addResistor("sum", "out", rf);
        circuit.addIdealOpAmp("gnd", "sum", "out");
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        circuit.prepare(sampleRate);

        const double out = circuit.process(1.0f);

        // The inverting input is a virtual ground, and here it really is ground.
        CHECK(circuit.getNodeVoltage("sum") == Catch::Approx(0.0).margin(1.0e-9));

        // No nonlinear devices at all: one constraint row and nothing for Newton
        // to do. That is the reason to reach for this over the macro model.
        CHECK(circuit.getPortCount() == 0);
        CHECK_FALSE(circuit.isNonlinear());

        return out;
    };

    // To float resolution, which is as exact as process() can report -- -4.7
    // isn't representable in a float, so a tighter tolerance than this would be
    // testing the return type rather than the solver.
    CHECK(invertingGain(10000.0, 10000.0) == Catch::Approx(-1.0).epsilon(1.0e-6));
    CHECK(invertingGain(10000.0, 47000.0) == Catch::Approx(-4.7).epsilon(1.0e-6));
    CHECK(invertingGain(1000.0, 100000.0) == Catch::Approx(-100.0).epsilon(1.0e-6));

    // Non-inverting: 1 + Rf/Rg.
    Circuit circuit;
    circuit.addResistor("out", "inm", 90000.0);
    circuit.addResistor("inm", "gnd", 10000.0);
    circuit.addIdealOpAmp("in", "inm", "out");
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    CHECK(circuit.process(1.0f) == Catch::Approx(10.0).epsilon(1.0e-6));
}

TEST_CASE("A transformer transforms voltage, current and impedance", "[circuit][transformer]")
{
    // Impedance reflection is the sharp test here: nothing in the model states
    // it, so getting n^2 out is evidence the two equations are right rather than
    // that the answer was written in.
    for (const double turns : {1.0, 2.0, 10.0})
    {
        constexpr double load = 1000.0;

        Circuit circuit;
        circuit.addResistor("in", "pa", 1.0); // small series resistor to sense with
        const auto transformer = circuit.addTransformer({{"pa", "gnd", turns},
                                                         {"sa", "gnd", 1.0}});
        circuit.addResistor("sa", "gnd", load);
        circuit.setInputNode("in");
        circuit.setOutputNode("sa");
        circuit.prepare(sampleRate);

        const double secondary = circuit.process(10.0f);
        const double primary = circuit.getNodeVoltage("pa");
        const double primaryCurrent = circuit.getWindingCurrent(transformer, 0);
        const double secondaryCurrent = circuit.getWindingCurrent(transformer, 1);

        INFO("turns " << turns << ": Vp " << primary << ", Vs " << secondary);

        // Voltage divides by the turns ratio...
        CHECK(primary / secondary == Catch::Approx(turns).epsilon(1.0e-6));

        // ...current multiplies by it, in the opposite direction...
        CHECK(secondaryCurrent == Catch::Approx(-turns * primaryCurrent).epsilon(1.0e-6));

        // ...so power is conserved exactly, and the primary sees the load
        // multiplied by the square of the ratio.
        CHECK(primary / primaryCurrent == Catch::Approx(load * turns * turns).epsilon(1.0e-6));
    }
}

TEST_CASE("A centre tap splits the secondary evenly", "[circuit][transformer]")
{
    // One core, so both halves are locked to the same flux -- which is why this
    // is a single three-winding transformer and not two of them.
    Circuit circuit;
    circuit.addResistor("in", "pa", 1.0);
    circuit.addCenterTapTransformer("pa", "gnd", "sa", "tap", "sb", 1.0, 1.0);
    circuit.addResistor("tap", "gnd", 0.001); // tap grounded, as a rectifier wires it
    circuit.addResistor("sa", "gnd", 10000.0);
    circuit.addResistor("sb", "gnd", 10000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("sa");
    circuit.prepare(sampleRate);

    circuit.process(10.0f);

    const double upper = circuit.getNodeVoltage("sa");
    const double lower = circuit.getNodeVoltage("sb");

    INFO("sa " << upper << ", tap " << circuit.getNodeVoltage("tap") << ", sb " << lower);

    // Half the primary each, and in opposition -- which is what lets one of them
    // conduct on each half cycle in a full-wave rectifier.
    CHECK(upper == Catch::Approx(5.0).margin(0.01));
    CHECK(lower == Catch::Approx(-5.0).margin(0.01));
}

TEST_CASE("A magnetising inductance turns an ideal transformer into a real one",
          "[circuit][transformer]")
{
    // The ideal transformer has no low-frequency limit; a real one is bounded by
    // its primary inductance. Composing them gives the behaviour without the
    // component needing to know about it.
    Circuit circuit;
    circuit.addCapacitor("in", "pa", 10.0e-6);
    circuit.addResistor("pa", "gnd", 1.0e6);
    circuit.addTransformer({{"pa", "gnd", 10.0}, {"sa", "gnd", 1.0}});
    circuit.addInductor("pa", "gnd", 10.0); // magnetising inductance
    circuit.addResistor("sa", "gnd", 8.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("sa");
    circuit.prepare(sampleRate);

    // 10:1 into 8 ohms, so the load reflects to 800 ohms and 10 H corners at
    // 800/(2*pi*10) = 12.7 Hz.
    const float atLow = measureResponse(circuit, 20.0, 1.0f, 0.5);
    const float inBand = measureResponse(circuit, 1000.0, 1.0f, 0.5);

    INFO("20 Hz " << atLow << ", 1 kHz " << inBand);

    CHECK(inBand == Catch::Approx(0.1).epsilon(0.01)); // exactly the turns ratio
    CHECK(atLow < inBand);                             // and the bass is going
    CHECK(atLow > inBand * 0.8);                       // but only just, at 20 Hz
}

// Hidden from the default run (the "." tag): this is the measurement harness
// behind the drive-dependence figures in LIMITATIONS.md's capacitance section,
// not a pass/fail test -- it prints a table. Run it explicitly with
// ./Tests [scratch] to re-measure.
TEST_CASE ("Miller sweep: response against drive, with and without capacitance", "[.][scratch]")
{
    using namespace CircuitComponents;
    constexpr double fs = 96000.0;

    auto stage = [] (TriodeModel m, double sourceOhms)
    {
        auto c = std::make_unique<Circuit>();
        c->addVoltageSource ("hv", "gnd", 300.0);
        c->addResistor ("hv", "plate", 100000.0);
        c->addTriode ("plate", "grid", "cathode", m);
        c->addResistor ("cathode", "gnd", 1500.0);
        c->addCapacitor ("cathode", "gnd", 22.0e-6);
        c->addResistor ("in", "grid", sourceOhms);
        c->addResistor ("grid", "gnd", 1.0e6);
        c->addCapacitor ("plate", "out", 22.0e-9);
        c->addResistor ("out", "gnd", 1.0e6);
        c->setInputNode ("in"); c->setOutputNode ("out"); c->prepare (fs);
        return c;
    };

    auto probeGain = [&] (Circuit& c, double drive, double probeHz, double probeAmp)
    {
        constexpr int n = 96000;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs;
            c.process (static_cast<float> (drive * std::sin (2.0 * std::numbers::pi * 100.0 * t)
                                         + probeAmp * std::sin (2.0 * std::numbers::pi * probeHz * t)));
        }
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (i + n) / fs;
            const double v = c.process (static_cast<float> (drive * std::sin (2.0 * std::numbers::pi * 100.0 * t)
                                                          + probeAmp * std::sin (2.0 * std::numbers::pi * probeHz * t)));
            const double w = 2.0 * std::numbers::pi * probeHz * i / fs;
            re += v * std::cos (w); im += v * std::sin (w);
        }
        return 2.0 * std::sqrt (re * re + im * im) / n / probeAmp;
    };

    auto zeroCaps = [] (TriodeModel m) { m.capGridCathode = m.capGridPlate = m.capPlateCathode = 0.0; return m; };
    const double freqs[] = { 1150.0, 2150.0, 5150.0, 10150.0, 15150.0, 20150.0 };

    printf ("\n  Incremental response, normalised to 1150 Hz. 470k source.\n");
    printf ("  %-28s", "Hz:");
    for (double f : freqs) printf ("%8.0f", f);
    printf ("\n");

    struct Row { const char* label; TriodeModel m; double drive; };
    for (const auto& row : { Row{"capacitance, clean",      TriodeModel::ecc83(),            0.01},
                             Row{"capacitance, driven hard", TriodeModel::ecc83(),            6.00},
                             Row{"no capacitance (control)", zeroCaps (TriodeModel::ecc83()), 0.01} })
    {
        double ref = 0.0;
        printf ("  %-28s", row.label);
        for (double f : freqs)
        {
            auto c = stage (row.m, 470000.0);
            const double g = probeGain (*c, row.drive, f, 0.002);
            if (ref == 0.0) ref = g;
            printf ("%8.2f", 20.0 * std::log10 (g / ref));
        }
        printf ("  dB\n");
    }
}

TEST_CASE("A node held up only by semiconductors still solves", "[circuit][dk]")
{
    // DK lifts the nonlinear devices out of the linear matrix, so a node whose
    // only company is semiconductor terminals used to end up with no
    // conductance in it at all and a singular factorisation. A transistor's
    // collector wired straight to the next one's base is exactly that node, and
    // it is an ordinary direct-coupled pair, not an exotic case.
    //
    // The symptom was silence with no diagnostic: the DC system stamps the
    // devices properly, so the operating point solved and looked correct, while
    // process() saw an invalid factorisation and returned 0 for every sample.
    Circuit circuit;
    circuit.setInputNode("in");
    circuit.addVoltageSource("vcc", "gnd", 9.0);

    circuit.addResistor("in", "b1", 100000.0);
    circuit.addResistor("b1", "gnd", 1.0e6);
    circuit.addResistor("e1", "gnd", 4700.0);
    circuit.addTransistor("b1", "c1", "e1", Circuit::BjtModel::npnSilicon());

    // "c1" now carries the NPN's collector and the PNP's base, and nothing else.
    circuit.addTransistor("c1", "e1", "vcc", Circuit::BjtModel::pnpSilicon());

    circuit.setOutputNode("e1");
    circuit.prepare(48000.0);

    REQUIRE(circuit.foundOperatingPoint());

    // The real check: a circuit whose matrix wouldn't factorise returns exactly
    // zero forever, so any finite non-zero response proves it is solving.
    double phase = 0.0;
    const auto step = 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;

    for (int i = 0; i < 20000; ++i)
    {
        circuit.process(0.2f * static_cast<float>(std::sin(phase)));
        phase += step;
    }

    double lowest = 1.0e30, highest = -1.0e30;

    for (int i = 0; i < 2000; ++i)
    {
        const double out = circuit.process(0.2f * static_cast<float>(std::sin(phase)));
        phase += step;
        REQUIRE(std::isfinite(out));
        lowest = std::min(lowest, out);
        highest = std::max(highest, out);
    }

    CHECK(highest - lowest > 0.0);
}

//==============================================================================
TEST_CASE("The 2N5952 reproduces its datasheet", "[jfet][model]")
{
    // Held to the same standard as the valve models: the fitted parameters have
    // to land inside every figure the manufacturer publishes, at the operating
    // point the sheet quotes. Fairchild give no typicals for this part, only
    // limits, so "inside the band" is the whole of what can be asked -- but the
    // three figures are tied together by the square law, so a pair chosen at
    // odds with each other shows up as the third falling outside.
    const auto m = CircuitComponents::JfetModel::n2n5952();

    // Fairchild 2N5952 Rev A1, at VDS = 15 V, VGS = 0.
    constexpr double vds = 15.0;
    const double overdrive = -m.threshold;
    const double modulation = 1.0 + m.lambda * vds;

    const double idss = m.beta * modulation * overdrive * overdrive;
    const double gfs = m.beta * modulation * 2.0 * overdrive;
    const double gos = m.beta * m.lambda * overdrive * overdrive;

    INFO("Vgs(off) " << m.threshold << " V, IDSS " << idss * 1e3 << " mA, gfs "
                     << gfs * 1e3 << " mA/V, gos " << gos * 1e6 << " umho");

    CHECK(m.threshold <= -1.3);          // VGS(off) -1.3 V min
    CHECK(m.threshold >= -3.5);          //          -3.5 V max
    CHECK(idss >= 4.0e-3);               // IDSS 4.0 mA min
    CHECK(idss <= 8.0e-3);               //      8.0 mA max
    CHECK(gfs >= 2.0e-3);                // gfs 2000 umho min
    CHECK(gfs <= 6.5e-3);                //     6500 umho max
    CHECK(gos <= 75.0e-6);               // gos 75 umho max

    // N-channel, and the gate junction is present -- a Phase 90 drives these
    // gates from an LFO and the stage has to block when that goes positive.
    CHECK(m.channel == CircuitComponents::JfetModel::Channel::N);
    CHECK(m.gateSaturationCurrent > 0.0);
}

TEST_CASE("The 2N5952 works as a voltage-controlled resistor", "[jfet][model]")
{
    // What the part is in the palette for. Near Vds = 0 the channel is a
    // resistor whose value the gate sets, and a phaser is four of them sweeping
    // together -- so what matters is not the absolute ohms but that the gate
    // moves them over a usable range.
    //
    // Driven through linearise(), deliberately. The version of this test that
    // this replaces computed the slope resistance from the triode-region formula
    // transcribed into the test body, which only ever confirmed that the formula
    // had been retyped correctly. It passed for months while the device it
    // claimed to cover conducted in one direction only, because the branch that
    // did that was never reached from here.
    CircuitComponents::Jfet jfet{};
    jfet.model = CircuitComponents::JfetModel::n2n5952();
    CircuitComponents::DeviceLinearisation out{};

    // Slope resistance at the origin: the channel conductance the solver would
    // actually stamp, taken just off zero so the reading is the device's and not
    // a special case.
    const auto channelOhms = [&](double vgs)
    {
        double v[2] = {vgs, 1.0e-6};
        linearise(jfet, v, out);
        return 1.0 / out.jacobian[3];
    };

    const double wideOpen = channelOhms(0.0);
    const double halfOff = channelOhms(-1.2);
    const double pinched = channelOhms(-2.3);

    INFO("Rds(on): " << wideOpen << " / " << halfOff << " / " << pinched << " ohm");

    // A few hundred ohms wide open is what this part measures, and it has to
    // climb by more than a decade before pinch-off or a phaser built round it
    // would barely sweep.
    CHECK(wideOpen > 100.0);
    CHECK(wideOpen < 1000.0);
    CHECK(halfOff > wideOpen * 1.5);
    CHECK(pinched > wideOpen * 10.0);
}

TEST_CASE("A JFET channel conducts both ways", "[jfet][model]")
{
    // The defect the test above could not see. A JFET has no built-in drain and
    // source -- it is one channel with a gate over it, and whichever end sits
    // lower is the source. Treating negative Vds as an off state turned the part
    // into a half-wave rectifier, and did it worst in the circuit it is in the
    // palette for: a phaser or a compressor holds the channel near Vds = 0 and
    // swings the signal straight through zero.
    CircuitComponents::Jfet jfet{};
    jfet.model = CircuitComponents::JfetModel::n2n5952();
    CircuitComponents::DeviceLinearisation out{};

    const auto drainCurrent = [&](double vgs, double vds)
    {
        double v[2] = {vgs, vds};
        linearise(jfet, v, out);
        return out.current[1];
    };

    const auto slope = [&](double vgs, double vds)
    {
        double v[2] = {vgs, vds};
        linearise(jfet, v, out);
        return out.jacobian[3];
    };

    // It conducts at all, in the direction that used to read exactly zero.
    const double forward = drainCurrent(-1.0, 0.4);
    const double reverse = drainCurrent(-1.0, -0.4);

    INFO("Id(+0.4) = " << forward << " A, Id(-0.4) = " << reverse << " A");
    CHECK(forward > 1.0e-6);
    CHECK(reverse < -1.0e-6);

    // The channel is symmetric about its own terminals rather than about the
    // gate: measured from the *source*, a reversed channel sees Vgd instead of
    // Vgs, so it opens wider and passes more. That asymmetry is real -- it is
    // why a JFET used as a variable resistor wants the classic 50% drain-to-gate
    // feedback pair to cancel it -- so what is checked is that it is present and
    // the right way round, not that the device is antisymmetric.
    CHECK(std::abs(reverse) > std::abs(forward));

    // Antisymmetric where it should be: with the gate referred to the midpoint
    // of the swing, both ends of the channel see the same thing.
    const double a = drainCurrent(-1.0, 0.4);
    double v[2] = {-1.0 - 0.4, -0.4};
    linearise(jfet, v, out);
    CHECK(out.current[1] == Catch::Approx(-a).epsilon(1.0e-9));

    // And smooth across the crossing, which is where the signal spends its time.
    // A kink here would be a discontinuity in the conductance the solver stamps.
    const double justAbove = slope(-1.0, 1.0e-9);
    const double justBelow = slope(-1.0, -1.0e-9);

    INFO("gds just above zero " << justAbove << ", just below " << justBelow);
    CHECK(justBelow == Catch::Approx(justAbove).epsilon(1.0e-6));
}

//==============================================================================
TEST_CASE("The 2N5133 reproduces its datasheet", "[bjt][model]")
{
    // Same standard as the valves and the 2N5952: the fitted card has to
    // reproduce the figures the manufacturer publishes, at the currents they
    // publish them at. Computed with the engine's own forward-active equations
    // (see linearise in Transistor.h) rather than an independent formula, so
    // this measures the model the solver will actually run.
    const auto m = CircuitComponents::BjtModel::npn2N5133();

    const auto forwardActive = [&m](double ic)
    {
        // Ic = IS*(ef - 1) with the collector junction off, so ef follows from
        // the current, and Vbe from ef.
        const double ef = ic / m.saturationCurrent + 1.0;
        const double vbe = m.forwardScaleVoltage() * std::log(ef);

        double ib = (m.saturationCurrent / m.forwardBeta) * (ef - 1.0);
        ib += m.baseEmitterLeakage
            * (std::exp(vbe / (m.baseEmitterLeakageEmission * m.thermalVoltage)) - 1.0);

        return std::pair { ic / ib, vbe };
    };

    const auto [hfe1mA, vbe1mA] = forwardActive(1.0e-3);
    const auto [hfe50uA, vbe50uA] = forwardActive(50.0e-6);

    INFO("hFE " << hfe1mA << " at 1 mA, " << hfe50uA << " at 50 uA, Vbe "
                << vbe1mA << " V at 1 mA");

    // NJ Semi 2N5133: hFE 220 typ at 1.0 mA (60 min, 1000 max), 50 typ at 50 uA.
    CHECK(hfe1mA == Catch::Approx(220.0).epsilon(0.01));
    CHECK(hfe50uA == Catch::Approx(50.0).epsilon(0.01));

    // And inside the graded window at the point it is graded.
    CHECK(hfe1mA >= 60.0);
    CHECK(hfe1mA <= 1000.0);

    // Vbe(on) 0.75 V max at 1 mA.
    CHECK(vbe1mA <= 0.75);
    CHECK(vbe1mA > 0.55);
    CHECK(vbe50uA < vbe1mA);

    // BF is the no-recombination asymptote, so it should sit under the hFE
    // maximum -- a typical part approaching the spread's top without passing it.
    CHECK(m.forwardBeta < 1000.0);
    CHECK(m.forwardBeta > hfe1mA);

    CHECK(m.polarity == CircuitComponents::BjtModel::Polarity::NPN);
}

TEST_CASE("The 2N5133's gain sags at low current", "[bjt][model]")
{
    // The reason the part is worth having as its own model rather than as
    // "another silicon NPN": a plain Ebers-Moll device holds BF flat all the
    // way down, and this one loses three quarters of its gain over one decade.
    const auto m = CircuitComponents::BjtModel::npn2N5133();
    const auto plain = CircuitComponents::BjtModel::npnSilicon();

    const auto gainAt = [](const CircuitComponents::BjtModel& model, double ic)
    {
        const double ef = ic / model.saturationCurrent + 1.0;
        const double vbe = model.forwardScaleVoltage() * std::log(ef);
        double ib = (model.saturationCurrent / model.forwardBeta) * (ef - 1.0);

        if (model.baseEmitterLeakage > 0.0)
            ib += model.baseEmitterLeakage
                * (std::exp(vbe / (model.baseEmitterLeakageEmission * model.thermalVoltage)) - 1.0);

        return ic / ib;
    };

    const double hot = gainAt(m, 1.0e-3);
    const double cold = gainAt(m, 50.0e-6);

    CHECK(cold < hot * 0.3);   // 50 against 220 is a factor of 4.4

    // The generic silicon model, by contrast, barely moves -- which is what
    // makes it the wrong stand-in for this part.
    CHECK(gainAt(plain, 50.0e-6) > gainAt(plain, 1.0e-3) * 0.9);
}

//==============================================================================
TEST_CASE("Node names resolve to indices that read the same voltage", "[circuit][metering]")
{
    // What lets a scope read a node every sample without hashing its name every
    // sample. The index form has to agree with the name form exactly, or the
    // readout quietly moves to a different node.
    Circuit circuit;
    circuit.addResistor("in", "mid", 1000.0);
    circuit.addResistor("mid", "gnd", 3000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("mid");
    circuit.prepare(sampleRate);

    const auto mid = circuit.getNodeIndex("mid");
    const auto ground = circuit.getNodeIndex("gnd");

    CHECK(mid >= 0);
    CHECK(ground == 0); // ground is always node 0

    // A name the netlist hasn't got resolves to -1 and reads as 0 V, so an
    // unresolved probe reads like ground rather than indexing off the end.
    CHECK(circuit.getNodeIndex("nowhere") == -1);
    CHECK(circuit.getNodeVoltage(Circuit::NodeIndex{-1}) == Catch::Approx(0.0));

    circuit.process(1.0f);

    // The divider puts 3/4 of the input on `mid`, and both lookups say so.
    CHECK(circuit.getNodeVoltage(mid) == Catch::Approx(0.75).margin(1.0e-9));
    CHECK(circuit.getNodeVoltage(mid) == Catch::Approx(circuit.getNodeVoltage("mid")));

    // Indices survive processing -- nodes are only ever appended, never
    // renumbered -- which is what makes it safe to resolve once per build.
    for (int i = 0; i < 64; ++i)
        circuit.process(0.5f);

    CHECK(circuit.getNodeIndex("mid") == mid);
    CHECK(circuit.getNodeVoltage(mid) == Catch::Approx(circuit.getNodeVoltage("mid")));
}

//==============================================================================
TEST_CASE("A singular netlist stays quiet instead of re-factorising forever", "[circuit]")
{
    // An output terminal on a node nothing else touches: its row is all zeros,
    // so the matrix will not factorise. process() has to report that once and
    // then be cheap about it -- it used to retry the whole O(n^3) stamp and
    // factorise on every sample, each attempt as doomed as the last, because
    // rebuildLinearSystem() clears linearDirty whether or not it succeeded.
    Circuit circuit;
    circuit.addResistor("in", "a", 1000.0);
    circuit.addResistor("a", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("dangling"); // nothing is wired to it
    circuit.prepare(sampleRate);

    REQUIRE(circuit.getSystemSize() > 0);
    REQUIRE(! circuit.hasUsableFactorisation());

    for (int i = 0; i < 512; ++i)
    {
        const float out = circuit.process(1.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE(out == 0.0f);
    }

    // Still singular after all that: the answer does not drift, and nothing has
    // quietly declared the factorisation usable again.
    CHECK(! circuit.hasUsableFactorisation());
}

//==============================================================================
TEST_CASE("A value change still re-stamps after prepare", "[circuit]")
{
    // The other half of the change above. Gating the rebuild on linearDirty
    // alone is only safe because every setter re-sets it, so this pins that a
    // knob move is still picked up -- the case that would break if the gate were
    // ever tightened further.
    Circuit circuit;
    const auto top = circuit.addResistor("in", "out", 1000.0);
    circuit.addResistor("out", "gnd", 1000.0);
    circuit.setInputNode("in");
    circuit.setOutputNode("out");
    circuit.prepare(sampleRate);

    CHECK(circuit.process(1.0f) == Catch::Approx(0.5).margin(1.0e-6));

    circuit.setResistance(top, 3000.0);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.25).margin(1.0e-6));

    circuit.setResistance(top, 1000.0);
    CHECK(circuit.process(1.0f) == Catch::Approx(0.5).margin(1.0e-6));
}

//==============================================================================
TEST_CASE("A valve plate never sources current", "[valve][model]")
{
    // A plate collects electrons. It cannot emit them, so its current cannot go
    // negative however the rest of the valve is driven -- and both formulations
    // used to let it, by different routes.
    //
    // This is not an exotic region. A push-pull output stage undershoots its
    // plates on inductive kick from the transformer, which is why some amps fit
    // flyback diodes across the primary; and Newton visits it while hunting for
    // an operating point, where a large negative current pushes the iteration
    // away from the root rather than towards it.
    CircuitComponents::DeviceLinearisation out{};

    SECTION("Koren pentode, past the arctangent's useful range")
    {
        // Koren bends the plate curve over with atan(Vp/KVB), a fit over the
        // region the valve is used in. Below zero it returns a negative number
        // and took the plate current with it: an EL34 with the grid at +5 V
        // reported -285 mA at Vpk = -100 V.
        CircuitComponents::Pentode pentode{};

        for (const auto& model : {CircuitComponents::PentodeModel::el34(),
                                  CircuitComponents::PentodeModel::el84(),
                                  CircuitComponents::PentodeModel::kt77(),
                                  CircuitComponents::PentodeModel::u6v6()})
        {
            pentode.model = model;

            for (double grid : {-30.0, -10.0, 0.0, 5.0})
                for (double plate : {-300.0, -100.0, -20.0, 0.0, 20.0, 250.0})
                {
                    double v[3] = {grid, 265.0, plate};
                    linearise(pentode, v, out);

                    INFO("Vg1 = " << grid << ", Vpk = " << plate
                                  << ", Ia = " << out.current[2] << " A");

                    // gmin is stamped across every port and rides on the answer,
                    // so the floor is that rather than a hard zero.
                    REQUIRE(out.current[2] >= CircuitComponents::gmin * plate - 1.0e-15);
                }
        }

        // The screen keeps conducting throughout -- it does not depend on the
        // plate, which is the whole point of a screen grid, and clamping the
        // plate must not have quietly changed that.
        double v[3] = {-13.5, 265.0, -100.0};
        linearise(pentode, v, out);
        CHECK(out.current[1] > 0.0);
    }

    SECTION("Dempwolf-Zolzer triode, where Ig overtakes Ik")
    {
        // These derive the plate current by subtraction, Ia = Ik - Ig, which is
        // what makes them conserve cathode current. But their grid current has
        // no plate term at all, so once the plate falls far enough for Ik to
        // collapse below Ig the difference goes negative.
        //
        // Reachable two ways, and the second is the common one: deep plate
        // undershoot, and *cutoff* -- where Ik falls below the model's constant
        // Ig0 term, tens of nanoamps, and the subtraction goes slightly negative
        // at every cut-off moment a clipping preamp has.
        CircuitComponents::Triode triode{};

        for (const auto& model : {CircuitComponents::TriodeModel::measured12ax7RSD(),
                                  CircuitComponents::TriodeModel::measured12ax7RSD2(),
                                  CircuitComponents::TriodeModel::measured12ax7EHX()})
        {
            triode.model = model;

            for (double grid : {-6.0, -2.0, 0.0, 3.0})
                for (double plate : {-300.0, -133.0, -50.0, 0.0, 5.0, 100.0, 250.0})
                {
                    double v[2] = {grid, plate};
                    linearise(triode, v, out);

                    INFO("Vgk = " << grid << ", Vpk = " << plate
                                  << ", Ia = " << out.current[1] << " A");
                    REQUIRE(out.current[1] >= CircuitComponents::gmin * plate - 1.0e-15);
                }
        }
    }

    SECTION("Koren triode, which was already guarded")
    {
        // Included so the three stay honest against one standard. Koren computes
        // the plate current independently and switches off below cutoff, so it
        // never had this defect -- worth pinning rather than assuming.
        CircuitComponents::Triode triode{};
        triode.model = CircuitComponents::TriodeModel::ecc83();

        for (double grid : {-5.0, 0.0, 3.0})
            for (double plate : {-300.0, -50.0, 0.0, 250.0})
            {
                double v[2] = {grid, plate};
                linearise(triode, v, out);

                INFO("Vgk = " << grid << ", Vpk = " << plate);
                REQUIRE(out.current[1] >= CircuitComponents::gmin * plate - 1.0e-15);
            }
    }
}

//==============================================================================
TEST_CASE("A pentode conducts smoothly as its screen comes up", "[valve][model]")
{
    // The gate that cuts the valve off used to sit at Ug2 = 1 V, where the
    // model is still plainly conducting -- so Newton, which walks every node up
    // from zero during the operating-point solve, met a step on the way.
    //
    // Below a *positive* grid the model is genuinely continuous through zero,
    // so the step was pure artefact. This walks across where the old gate was
    // and insists no neighbouring pair of screen voltages disagrees by more
    // than the trend either side of it.
    CircuitComponents::Pentode pentode{};
    CircuitComponents::DeviceLinearisation out{};

    for (const auto& model : {CircuitComponents::PentodeModel::el34(),
                              CircuitComponents::PentodeModel::el84(),
                              CircuitComponents::PentodeModel::u6v6()})
    {
        pentode.model = model;

        double previous = 0.0;

        for (int step = 0; step <= 40; ++step)
        {
            const double screen = 0.05 * step; // 0 V to 2 V, straight over the old gate
            double v[3] = {0.0, screen, 250.0};
            linearise(pentode, v, out);

            const double ik = out.current[1] + out.current[2];

            INFO("Ug2 = " << screen << ": Ik = " << ik * 1e3 << " mA");

            // Monotone up, and never jumping by more than a fifth of a
            // milliamp between neighbouring screen voltages a twentieth of a
            // volt apart. The old gate stepped 0.18 mA at once on the EL34.
            CHECK(ik >= previous - 1.0e-12);
            CHECK(ik - previous < 0.2e-3);
            previous = ik;
        }
    }
}

TEST_CASE("A Koren triode conserves cathode current", "[valve][model]")
{
    // Koren's curve, continued past a positive grid, is what the cathode emits;
    // the grid's share has to come out of the plate rather than be added
    // alongside it. This is the invariant the Dempwolf-Zolzer path has always
    // held, now held by both.
    CircuitComponents::Triode triode{};
    CircuitComponents::DeviceLinearisation out{};

    for (const auto& model : {CircuitComponents::TriodeModel::ecc83(),
                              CircuitComponents::TriodeModel::ecc81(),
                              CircuitComponents::TriodeModel::ecc82(),
                              CircuitComponents::TriodeModel::ecc12ay7()})
    {
        triode.model = model;

        for (const double grid : {0.5, 1.0, 2.0, 5.0})
        {
            double v[2] = {grid, 250.0};
            linearise(triode, v, out);

            const double ig = out.current[0];
            const double ia = out.current[1];

            // Ik is Koren's own curve at this point, recomputed independently of
            // the split so this measures the split rather than restating it.
            const double root = std::sqrt(model.kvb + 250.0 * 250.0);
            const double a = 1.0 / model.mu + grid / root;
            const double e1 = (250.0 / model.kp) * std::log1p(std::exp(model.kp * a));
            const double ik = 2.0 * std::pow(e1, model.ex) / model.kg1;

            INFO("Vgk = " << grid << ": Ia " << ia * 1e3 << " mA + Ig " << ig * 1e3
                          << " mA vs Ik " << ik * 1e3 << " mA");
            CHECK(ia + ig == Catch::Approx(ik).epsilon(1.0e-6));
        }

        // And a grid below the cathode is untouched: the whole of Koren's
        // current still reaches the plate, to well inside any audible margin.
        double v[2] = {-2.0, 250.0};
        linearise(triode, v, out);

        const double root = std::sqrt(model.kvb + 250.0 * 250.0);
        const double a = 1.0 / model.mu - 2.0 / root;
        const double e1 = (250.0 / model.kp) * std::log1p(std::exp(model.kp * a));
        const double ik = 2.0 * std::pow(e1, model.ex) / model.kg1;

        // Written out with the gmin terms rather than hidden behind a loose
        // tolerance: the plate carries gmin*Vpk, and the subtraction hands it
        // back the grid branch's gmin*Vgk. Together 2.5e-10 A on these numbers,
        // which is what a slack epsilon here would have been quietly absorbing.
        const double leakage = CircuitComponents::gmin * 250.0 - CircuitComponents::gmin * -2.0;

        INFO("cold grid: Ia = " << out.current[1] * 1e3 << " mA vs Ik " << ik * 1e3 << " mA");
        CHECK(out.current[1] - leakage == Catch::Approx(ik).epsilon(1.0e-12));
    }
}

TEST_CASE("The plate-current floor leaves working valves alone", "[valve][model]")
{
    // The other half of the guard above. A floor that fired during ordinary
    // operation would change what every saved sheet sounds like, so this pins
    // the datasheet operating points the models were fitted at.
    CircuitComponents::DeviceLinearisation out{};

    CircuitComponents::Triode triode{};
    triode.model = CircuitComponents::TriodeModel::measured12ax7RSD();
    double t[2] = {-2.0, 250.0};
    linearise(triode, t, out);
    INFO("12AX7 RSD at Va 250, Vg -2: Ia = " << out.current[1] * 1e3 << " mA");
    CHECK(out.current[1] == Catch::Approx(0.901322239e-3).epsilon(1.0e-6));

    CircuitComponents::Pentode pentode{};
    pentode.model = CircuitComponents::PentodeModel::el34();
    double p[3] = {-13.5, 265.0, 250.0};
    linearise(pentode, p, out);
    INFO("EL34 at Ua 250, Ug2 265, Ug1 -13.5: Ia = " << out.current[2] * 1e3
         << " mA, Ig2 = " << out.current[1] * 1e3 << " mA");
    CHECK(out.current[2] == Catch::Approx(100.0e-3).epsilon(0.02));  // datasheet 100 mA
    CHECK(out.current[1] == Catch::Approx(14.9e-3).epsilon(0.02));   // datasheet 14.9 mA
}

//==============================================================================
TEST_CASE("The Early effect scales the transport current with collector voltage", "[bjt][model]")
{
    // The level-1 SPICE form: the transport current picks up a factor
    // 1 + Vce/VAF, which is an output resistance ro = VAF/Ic in disguise.
    // Checked at the device, where the numbers are exact: at Vbe 0.65 V and
    // Vce 5.65 V the scale is 1.0763 and ro should come out at 74.03/Ic.
    CircuitComponents::DeviceLinearisation on{};
    CircuitComponents::DeviceLinearisation off{};

    const auto lineariseWith = [](double vaf, double vBe, double vBc, CircuitComponents::DeviceLinearisation& out)
    {
        CircuitComponents::Bjt bjt{};
        bjt.model = CircuitComponents::BjtModel::npnSilicon();
        bjt.model.forwardEarlyVoltage = vaf;
        double v[2] = {vBe, vBc};
        linearise(bjt, v, out);
    };

    lineariseWith(74.03, 0.65, -5.0, on);
    lineariseWith(0.0, 0.65, -5.0, off);

    // Port 1 is (base, collector): its current is the base junction's minus the
    // transport current, so negated it is essentially the collector current.
    const double icOn = -on.current[1];
    const double icOff = -off.current[1];

    INFO("Ic = " << icOff * 1e3 << " mA without, " << icOn * 1e3 << " mA with");
    CHECK(icOn / icOff == Catch::Approx(1.0 + 5.65 / 74.03).epsilon(1.0e-3));

    // The output conductance is the difference the effect makes to the
    // collector port's own slope: 1/ro with ro = VAF/Ic.
    const double go = on.jacobian[3] - off.jacobian[3];
    INFO("go = " << go * 1e6 << " uS, so ro = " << 1.0 / go / 1e3 << " kohm");
    CHECK(go > 0.0);
    CHECK(1.0 / go == Catch::Approx(74.03 / icOff).epsilon(0.01));

    // And the cross term mirrors it: the collector port now also sees the
    // base-emitter voltage, with the opposite sign.
    CHECK(on.jacobian[1] == Catch::Approx(-go).epsilon(0.01));
    CHECK(std::abs(off.jacobian[1]) < 1.0e-12);

    // PNP mirrors it exactly, polarity folded in twice.
    CircuitComponents::DeviceLinearisation pnpOut{};
    CircuitComponents::Bjt pnp{};
    pnp.model = CircuitComponents::BjtModel::pnpSilicon();
    double v[2] = {-0.65, 5.0};
    linearise(pnp, v, pnpOut);

    const double scale = 1.0 + 5.65 / 18.7; // the 2N3906's VAF
    const double transport = 1.41e-15 * (std::exp(0.65 / 0.025852) - 1.0);
    CHECK(std::abs(pnpOut.current[1]) == Catch::Approx(transport * scale).epsilon(1.0e-3));
}

namespace
{
    /** The grounded-emitter stage both transistor-refinement tests measure:
        high gain and a high-impedance drive, so the Early output resistance
        and the Miller-multiplied capacitance each have something to act on.

        Biased near 0.13 mA through a 27M base resistor, which keeps the
        transistor's own input resistance (~80k) comparable to the 100k source
        rather than shorting the capacitance's pole out. */
    Circuit makeGroundedEmitterStage(Circuit::BjtModel model)
    {
        Circuit circuit;
        circuit.addVoltageSource("vcc", "gnd", 9.0);
        circuit.addResistor("vcc", "base", 27.0e6);
        circuit.addCapacitor("in", "src", 100.0e-6);  // blocks DC, transparent in band
        circuit.addResistor("src", "base", 100000.0); // the source the Miller cap works against
        circuit.addResistor("vcc", "collector", 22000.0);
        circuit.addTransistor("base", "collector", "gnd", model);
        circuit.addCapacitor("collector", "out", 100.0e-6);
        circuit.addResistor("out", "gnd", 1000000.0);
        circuit.setInputNode("in");
        circuit.setOutputNode("out");
        return circuit;
    }

    double stageGainAt(Circuit& circuit, double frequency)
    {
        // 1 mV in, ~110 out: deep inside the small-signal region.
        return measureResponse(circuit, frequency, 0.001f, 0.3) / 0.001;
    }
}

TEST_CASE("The Early effect moves a stage's gain by a few percent", "[circuit][transistor]")
{
    // In a circuit the effect is deliberately small -- a few percent of gain,
    // not a doubling, and not nothing either. The sign is the topology's
    // business: with this stage's current-source-ish bias the higher collector
    // current wins over the extra output resistance and the gain rises
    // slightly; with a stiff voltage bias it would fall.
    auto model = Circuit::BjtModel::npnSilicon();
    model.capBaseEmitter = model.capBaseCollector = 0.0; // the DC effect only

    auto withoutEarly = model;
    withoutEarly.forwardEarlyVoltage = 0.0;

    auto on = makeGroundedEmitterStage(model);
    auto off = makeGroundedEmitterStage(withoutEarly);
    on.prepare(sampleRate);
    off.prepare(sampleRate);

    REQUIRE(on.foundOperatingPoint());
    REQUIRE(off.foundOperatingPoint());

    const double gainOn = stageGainAt(on, 1000.0);
    const double gainOff = stageGainAt(off, 1000.0);
    const double change = gainOn / gainOff - 1.0;

    INFO("gain " << gainOff << " without, " << gainOn << " with: "
         << change * 100.0 << "%");

    CHECK(std::abs(change) > 0.005);
    CHECK(std::abs(change) < 0.10);
    CHECK(on.getNonConvergenceCount() == 0);

    // And both solvers read the same device: DK eliminates the linear part and
    // iterates on the ports, full Newton stamps everything each iteration.
    auto fullNewtonStage = makeGroundedEmitterStage(model);
    fullNewtonStage.setSolverStrategy(Circuit::SolverStrategy::FullNewton);
    fullNewtonStage.prepare(sampleRate);

    const double gainFN = stageGainAt(fullNewtonStage, 1000.0);
    INFO("full-Newton gain = " << gainFN);
    CHECK(gainFN == Catch::Approx(gainOn).epsilon(1.0e-3));
    CHECK(fullNewtonStage.getNonConvergenceCount() == 0);
}

TEST_CASE("Junction capacitance puts a pole on a transistor stage", "[circuit][transistor]")
{
    // The estimate for the stage above: Av ~ gm * Rc ~ 110, so
    // Cin = Cbe + Cbc*(1+Av) ~ 410 pF, against 100k || 27M || r_pi ~ 44k that
    // is a lowpass at ~8.7 kHz -- inside the audio band, which is the whole
    // reason this is modelled.
    const auto gainAt = stageGainAt;

    auto model = Circuit::BjtModel::npnSilicon();
    auto stage = makeGroundedEmitterStage(model);
    stage.prepare(sampleRate);
    REQUIRE(stage.foundOperatingPoint());

    const double vCollector = stage.getNodeVoltage("collector");
    INFO("Vc = " << vCollector);
    REQUIRE(vCollector > 2.0);
    REQUIRE(vCollector < 8.0);

    const double g0 = gainAt(stage, 100.0);
    INFO("low-frequency gain = " << g0);
    REQUIRE(g0 > 50.0);

    // Find the -3 dB point by scanning, then check it lands near the estimate.
    double poleFrequency = 0.0;
    double previousGain = g0;
    double previousFrequency = 100.0;

    for (const double f : {2000.0, 3000.0, 4500.0, 6500.0, 9000.0, 13000.0, 18000.0, 24000.0})
    {
        const double g = gainAt(stage, f);
        INFO("gain at " << f << " Hz = " << g);

        if (g <= g0 * 0.7071 && poleFrequency == 0.0)
        {
            // Interpolate on the log-frequency axis, where a one-pole rolloff
            // is a straight line.
            const double t = (std::log10(g0 * 0.7071) - std::log10(previousGain))
                           / (std::log10(g) - std::log10(previousGain));
            poleFrequency = previousFrequency * std::pow(f / previousFrequency, t);
        }

        previousGain = g;
        previousFrequency = f;
    }

    INFO("measured -3 dB at " << poleFrequency << " Hz, estimate 8.7 kHz");
    REQUIRE(poleFrequency > 0.0);
    CHECK(poleFrequency > 4000.0);
    CHECK(poleFrequency < 16000.0);

    // Well past the pole the stage should be more than 6 dB down.
    CHECK(previousGain < g0 * 0.5);
    CHECK(stage.getNonConvergenceCount() == 0);

    // The capacitances are the only frequency limit here: without them the
    // stage is flat to 20 kHz, at the same gain.
    auto noCaps = model;
    noCaps.capBaseEmitter = noCaps.capBaseCollector = 0.0;
    auto flatStage = makeGroundedEmitterStage(noCaps);
    flatStage.prepare(sampleRate);
    REQUIRE(flatStage.foundOperatingPoint());

    const double flat0 = gainAt(flatStage, 100.0);
    const double flat20k = gainAt(flatStage, 20000.0);
    INFO("without caps: " << flat0 << " at 100 Hz, " << flat20k << " at 20 kHz");
    CHECK(flat0 == Catch::Approx(g0).epsilon(0.01));
    CHECK(flat20k == Catch::Approx(flat0).epsilon(0.03));

    // And it is the Miller path, not the base-emitter capacitance: Cbe alone
    // puts the pole near 800 kHz, which is flat for audio purposes.
    auto emitterOnly = model;
    emitterOnly.capBaseCollector = 0.0;
    auto beOnlyStage = makeGroundedEmitterStage(emitterOnly);
    beOnlyStage.prepare(sampleRate);
    REQUIRE(beOnlyStage.foundOperatingPoint());

    const double beOnly20k = gainAt(beOnlyStage, 20000.0);
    INFO("Cbe only, 20 kHz gain = " << beOnly20k);
    CHECK(beOnly20k == Catch::Approx(g0).epsilon(0.03));
}

//==============================================================================
// SCRATCH: GE ET-T509B resistance-coupled amplifier table, 12AX7.
// Not a permanent test yet -- this is the measurement that says how far off the
// shipped ECC83 model is against a real circuit rather than one bias point.
namespace
{
    struct GeRow { double rp, rs, rg1, rk; int gain; double eo; double ebb; };

    // Page 2 of ET-T509B (6-53), the Rg1 = 0.1 Meg (cathode-biased) rows.
    // Rp/Rs/Rg1 in megohms, Rk in ohms, Gain measured at 2.0 V RMS out,
    // Eo = max RMS out for 5% THD.
    const GeRow geRows[] = {
        {0.10, 0.10, 0.1, 1700, 31, 5.0,  90}, {0.10, 0.10, 0.1, 1000, 40, 15, 180}, {0.10, 0.10, 0.1,  760, 43, 30, 300},
        {0.10, 0.24, 0.1, 2000, 38, 6.9,  90}, {0.10, 0.24, 0.1, 1100, 46, 20, 180}, {0.10, 0.24, 0.1,  900, 50, 40, 300},
        {0.24, 0.24, 0.1, 3500, 43, 6.5,  90}, {0.24, 0.24, 0.1, 2000, 54, 18, 180}, {0.24, 0.24, 0.1, 1600, 58, 37, 300},
        {0.24, 0.51, 0.1, 3900, 49, 8.6,  90}, {0.24, 0.51, 0.1, 2300, 59, 24, 180}, {0.24, 0.51, 0.1, 1800, 64, 47, 300},
        {0.51, 0.51, 0.1, 7100, 50, 7.4,  90}, {0.51, 0.51, 0.1, 4300, 62, 19, 180}, {0.51, 0.51, 0.1, 3100, 66, 39, 300},
        {0.51, 1.00, 0.1, 7800, 53, 9.1,  90}, {0.51, 1.00, 0.1, 4800, 64, 24, 180}, {0.51, 1.00, 0.1, 3600, 69, 46, 300},
    };

    /** The GE figure, built exactly as ge_circuit.celsch draws it. */
    std::unique_ptr<Circuit> makeGeStage(const GeRow& r, const CircuitComponents::TriodeModel& model)
    {
        auto c = std::make_unique<Circuit>();
        c->addVoltageSource("ebb", "gnd", r.ebb);
        c->addCapacitor("in", "grid", 0.1e-6);           // C, large: midband only
        c->addResistor("grid", "gnd", r.rg1 * 1e6);      // Rg1
        c->addResistor("ebb", "plate", r.rp * 1e6);      // Rp
        c->addTriode("plate", "grid", "cathode", model);
        c->addResistor("cathode", "gnd", r.rk);          // Rk
        c->addCapacitor("cathode", "gnd", 100.0e-6);     // Ck, "adequately by-passed"
        c->addCapacitor("plate", "out", 0.1e-6);         // C
        c->addResistor("out", "gnd", r.rs * 1e6);        // Rs
        c->setInputNode("in");
        c->setOutputNode("out");
        return c;
    }

    /** RMS output for a 1 kHz sine of the given amplitude, after settling. */
    double geOutputRms(Circuit& c, double amplitudeVolts, double rate = 48000.0)
    {
        const double w = 2.0 * std::numbers::pi * 1000.0 / rate;
        const int settle = static_cast<int>(rate * 0.2);
        for (int i = 0; i < settle; ++i)
            c.process(static_cast<float>(amplitudeVolts * std::sin(w * i)));

        double sum = 0.0;
        const int n = static_cast<int>(rate / 1000.0) * 20;          // 20 cycles
        for (int i = 0; i < n; ++i)
        {
            const double v = c.process(static_cast<float>(amplitudeVolts
                                       * std::sin(w * (settle + i))));
            sum += v * v;
        }
        return std::sqrt(sum / n);
    }
}

namespace
{
    /** Gain the way GE measured it: drive the stage until it puts out 2.0 V RMS
        (the sheet's note 2), then divide by the input that took. */
    double geGain(const GeRow& r, const CircuitComponents::TriodeModel& model)
    {
        auto build = [&]
        {
            auto c = makeGeStage(r, model);
            c->prepare(96000.0);
            c->setOutputOffsetToOperatingPoint();
            return c;
        };
        // Fixed point rather than bisection. The stage is very nearly linear at
        // this level -- driving to 2 V RMS moves the gain about 1% -- so scaling
        // the drive by the RMS it missed by converges in a couple of passes.
        // Bisecting to the same accuracy costs forty circuit builds per row,
        // which is ten times the runtime for no more precision.
        double drive = 2.0 * std::numbers::sqrt2 / 50.0;
        double got = 0.0;
        for (int it = 0; it < 4; ++it)
        {
            auto probe = build();
            const double rms = geOutputRms(*probe, drive);
            got = rms / (drive / std::numbers::sqrt2);
            if (std::abs(rms - 2.0) < 1e-4)
                break;
            drive *= 2.0 / rms;
        }
        return got;
    }
}

TEST_CASE("SCRATCH GE resistance-coupled table", "[.][ge]")
{
    // Candidates to compare in the real solver. The analytic fitter in the
    // scratch tree is only a proxy -- it solves the load line directly instead
    // of running the circuit -- so whatever it proposes has to be confirmed
    // here, through the same DK engine the plugin actually uses.
    struct Candidate { const char* name; CircuitComponents::TriodeModel model; };
    std::vector<Candidate> candidates;
    candidates.push_back({"JJ", CircuitComponents::TriodeModel::ecc83()});
    candidates.push_back({"GE", CircuitComponents::TriodeModel::ecc83ge()});

    if (const char* env = std::getenv("CELINE_TRIODE_FIT"))
    {
        auto m = CircuitComponents::TriodeModel::ecc83();
        if (std::sscanf(env, "%lf,%lf,%lf,%lf,%lf", &m.mu, &m.ex, &m.kg1, &m.kp, &m.kvb) == 5)
            candidates.push_back({"candidate", m});
        else
            FAIL("CELINE_TRIODE_FIT must be mu,ex,kg1,kp,kvb");
    }

    printf("\n  GE ET-T509B resistance-coupled amplifier, 12AX7 -- gain at 2 V RMS out\n");
    printf("  %5s %5s %6s %6s | %6s", "Ebb", "Rp", "Rs", "Rk", "GE");
    for (const auto& c : candidates) printf(" %10s %8s", c.name, "error");
    printf("\n");

    std::vector<double> sumsq(candidates.size(), 0.0);
    int n = 0;
    for (const auto& r : geRows)
    {
        printf("  %5.0f %5.2f %6.2f %6.0f | %6d", r.ebb, r.rp, r.rs, r.rk, r.gain);
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const double got = geGain(r, candidates[i].model);
            const double err = 100.0 * (got / r.gain - 1.0);
            sumsq[i] += err * err;
            printf(" %10.1f %+7.1f%%", got, err);
        }
        printf("\n");
        ++n;
    }
    printf("\n  rms error across %d points:", n);
    for (size_t i = 0; i < candidates.size(); ++i)
        printf("   %s %.1f%%", candidates[i].name, std::sqrt(sumsq[i] / n));
    printf("\n\n");
}

TEST_CASE("SCRATCH GE bias diagnostics", "[.][gebias]")
{
    auto model = CircuitComponents::TriodeModel::ecc83();
    printf("\n  Operating point Celine settles on, and small-signal gain\n");
    printf("  %5s %5s %6s %6s | %7s %7s %8s | %7s %7s %7s\n",
           "Ebb","Rp","Rs","Rk","Va","Vk","Ia(mA)","gm","ra(k)","A_ss");
    for (const auto& r : geRows)
    {
        auto c = makeGeStage(r, model);
        c->prepare(96000.0);
        const double va = c->getNodeVoltage("plate"), vk = c->getNodeVoltage("cathode");
        const double ia = (r.ebb - va) / (r.rp * 1e6);

        // small-signal gain: tiny sine, well below anything nonlinear
        c->setOutputOffsetToOperatingPoint();
        const double a = geOutputRms(*c, 1.0e-4) / (1.0e-4 / std::numbers::sqrt2);

        // the model's own gm and ra at that bias
        CircuitComponents::Triode t{}; t.model = model;
        CircuitComponents::DeviceLinearisation o{};
        double v[2] = {-(vk - 0.0) , va - vk};
        linearise(t, v, o);
        printf("  %5.0f %5.2f %6.2f %6.0f | %7.1f %7.3f %8.3f | %7.2f %7.1f %7.1f\n",
               r.ebb, r.rp, r.rs, r.rk, va, vk, ia*1e3,
               o.jacobian[2]*1e3, 1.0/o.jacobian[3]/1e3, a);
    }
    printf("\n");
}
