#include <CelineEngine/Engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>

// High-gain valve preamps, laid out the way most of them are and driven the way
// a guitar drives them, held to the one thing that has to be true of all of
// them: Newton converges on every sample. A sample it gives up on is a click.
//
// The fixed cases each broke a version of the solver. The hidden search at the
// end is what found them, and is the thing to run after changing anything in
// EngineSolve.cpp:
//
//     STRESS_COUNT=700 ./Tests "[.convergencestress]"
//
// STRESS_SEED picks the first preamp, STRESS_WHERE=1 names the samples that
// failed. Seeds are only repeatable on one standard library, since the
// distributions that turn them into parts are not specified exactly.

namespace
{
    /** One common-cathode 12AX7 stage, and what couples it to the next. */
    struct GainStage
    {
        double stopper = 0.0;    // grid stopper; zero for none
        double plateLoad = 100e3;
        double cathode = 1.5e3;
        double bypass = 0.0;     // cathode bypass; zero leaves the stage cold
        double coupling = 22e-9; // to the next stage's grid, unused on the last
        double leak = 1e6;       // the next stage's grid leak, likewise
    };

    /** The layout nearly every high-gain preamp shares: gain stages in
        cascade, the last DC-coupled into a cathode follower, the follower
        driving a tone stack through a coupling capacitor. `secondHalf` adds what
        the Diezel VH4 has after its tone stack -- a volume at noon, a 680k
        stopper, one more stage and a second follower into a 100k + 4.7k
        divider. */
    struct Preamp
    {
        double rate = 48000.0;
        double bplus = 370.0;
        std::vector<GainStage> stages;
        double followerCathode = 100e3;
        bool toneStack = true;
        double load = 4.7e3; // the follower's load when there is no tone stack
        double outputCoupling = 100e-9;
        bool secondHalf = false;
        bool interelectrode = true;
        bool predict = false;
    };

    enum class Drive
    {
        plucks,       // decaying notes with their harmonics
        clippedNotes, // notes a stage or two has already squared off
        sweep,        // 40 Hz to 8 kHz over two seconds, rising 30 dB
        attacks,      // sharp pick attacks at random times and pitches
    };

    void addGainStage(Circuit& c, const Circuit::TriodeModel& model, const juce::String& in, const juce::String& id, const GainStage& stage)
    {
        const auto grid = stage.stopper > 0.0 ? "g" + id : in;
        if (stage.stopper > 0.0)
            c.addResistor(in, grid, stage.stopper);

        c.addTriode("p" + id, grid, "k" + id, model);
        c.addResistor("bplus", "p" + id, stage.plateLoad);
        c.addResistor("k" + id, "gnd", stage.cathode);

        if (stage.bypass > 0.0)
            c.addCapacitor("k" + id, "gnd", stage.bypass);
    }

    /** A Marshall-style treble-middle-bass stack, every pot at noon. */
    void addToneStack(Circuit& c, const juce::String& in, const juce::String& out)
    {
        c.addCapacitor(in, "treble", 470e-12);
        c.addResistor("treble", out, 110e3);    // treble pot, upper half
        c.addResistor(out, "bass", 110e3);      // and lower
        c.addResistor(in, "slope", 33e3);
        c.addCapacitor("slope", "bass", 22e-9); // bass capacitor
        c.addResistor("bass", "middle", 500e3); // bass pot
        c.addCapacitor("slope", "middle", 22e-9);
        c.addResistor("middle", "gnd", 12.5e3); // middle pot
    }

    Circuit build(const Preamp& preamp)
    {
        auto model = Circuit::TriodeModel::ecc83();
        if (! preamp.interelectrode)
            model.capGridCathode = model.capGridPlate = model.capPlateCathode = 0.0;

        Circuit c;
        c.addVoltageSource("bplus", "gnd", preamp.bplus);
        c.addCapacitor("in", "n0", 22e-9);
        c.addResistor("n0", "gnd", 1e6);

        juce::String node = "n0";
        const auto count = static_cast<int>(preamp.stages.size());

        for (int s = 0; s < count; ++s)
        {
            const auto& stage = preamp.stages[static_cast<size_t>(s)];
            const juce::String id(s);
            addGainStage(c, model, node, id, stage);

            if (s + 1 < count)
            {
                const auto next = "n" + juce::String(s + 1);
                c.addCapacitor("p" + id, next, stage.coupling);
                c.addResistor(next, "gnd", stage.leak);
                node = next;
            }
        }

        c.addTriode("bplus", "p" + juce::String(count - 1), "follower", model);
        c.addResistor("follower", "gnd", preamp.followerCathode);

        juce::String out;
        if (preamp.toneStack)
        {
            c.addCapacitor("follower", "stackIn", preamp.outputCoupling);
            c.addResistor("stackIn", "gnd", 470e3);
            addToneStack(c, "stackIn", "stackOut");
            out = "stackOut";
        }
        else
        {
            c.addCapacitor("follower", "load", preamp.outputCoupling);
            c.addResistor("load", "gnd", preamp.load);
            out = "load";
        }

        if (preamp.secondHalf)
        {
            c.addResistor(out, "volume", 235e3);
            c.addResistor("volume", "gnd", 235e3);
            addGainStage(c, model, "volume", "x", { 680e3, 120e3, 640.0, 0.0 });
            c.addTriode("bplus", "px", "followerX", model);
            c.addResistor("followerX", "gnd", 120e3);
            c.addCapacitor("followerX", "outX", 220e-9);
            c.addResistor("outX", "divider", 100e3);
            c.addResistor("divider", "gnd", 4.7e3);
            out = "divider";
        }

        c.setInputNode("in");
        c.setOutputNode(out);
        c.setPortVoltagePrediction(preamp.predict);
        c.prepare(preamp.rate);
        return c;
    }

    /** Sample i of a drive, peaking near `level`. */
    class Guitar
    {
    public:
        Guitar(Drive drive, double sampleRate, double peak, unsigned seed = 1)
            : kind(drive), rate(sampleRate), level(peak), random(seed)
        {
        }

        double operator()(int i)
        {
            constexpr double twoPi = 2.0 * std::numbers::pi;
            const double t = i / rate;
            const int note = (i / static_cast<int>(0.5 * rate)) % 4;
            const double u = std::fmod(t, 0.5);

            switch (kind)
            {
                case Drive::clippedNotes:
                {
                    const double notes[] = { 110.0, 220.0, 329.63, 659.26 };
                    return level * std::exp(-3.0 * u) * std::tanh(12.0 * std::sin(twoPi * notes[note] * u));
                }

                case Drive::plucks:
                {
                    const double notes[] = { 82.41, 110.0, 146.83, 196.0 };
                    double x = 0.0;
                    for (int h = 1; h <= 6; ++h)
                        x += std::sin(twoPi * notes[note] * h * u) / (h * h);

                    return level * 0.7 * std::exp(-4.0 * u) * x * std::min(1.0, u * rate / 24.0);
                }

                case Drive::sweep:
                {
                    const double length = 2.0;
                    const double phase = twoPi * 40.0 * length / std::log(200.0) * (std::pow(200.0, t / length) - 1.0);
                    return level * std::pow(10.0, (-30.0 + 30.0 * t / length) / 20.0) * std::sin(phase);
                }

                case Drive::attacks:
                default:
                {
                    if (i == 0 || std::uniform_real_distribution<double>(0.0, 1.0)(random) < 6.0 / rate)
                    {
                        frequency = 80.0 * std::pow(2.0, std::uniform_real_distribution<double>(0.0, 3.0)(random));
                        attack = t;
                    }

                    const double since = t - attack;
                    return level * std::exp(-6.0 * since) * (std::sin(twoPi * frequency * since) + 0.5 * std::sin(twoPi * 2.0 * frequency * since));
                }
            }
        }

    private:
        Drive kind;
        double rate, level;
        std::mt19937 random;
        double frequency = 110.0, attack = 0.0;
    };
} // namespace

//==============================================================================
TEST_CASE("High-gain preamps converge on every sample", "[circuit][valve]")
{
    struct Case
    {
        const char* what;
        Preamp preamp;
        Drive drive;
        double level;
    };

    // Two seconds of each, at the rate and with the seed prediction each one
    // was found with. Where a version failed is chaotic -- it moves with the
    // bits of every sample before -- so these are the whole renders that
    // caught them, not a window around the sample.
    const Case cases[] = {
        // A squared-off edge flips four stages within a thousandth of one
        // sample's step. Wherever a grid crossed the onset of grid current
        // there, Newton orbited the answer, and continuation gave up: the
        // sub-steps that converge were finer than the 1/1024 it went down to.
        { "four bypassed stages into the follower, tone stack and the VH4's second half, clipped notes at 2 V",
          { 48000.0, 420.0,
            { { 0.0, 120e3, 1800.0, 22e-6, 22e-9, 470e3 },
              { 68e3, 220e3, 820.0, 22e-6, 47e-9, 3.3e6 },
              { 1e3, 68e3, 2700.0, 22e-6, 22e-9, 3.3e6 },
              { 68e3, 68e3, 10e3, 0.68e-6, 4.7e-9, 470e3 } },
            100e3, true, 4.7e3, 100e-9, true, true, true },
          Drive::clippedNotes, 2.0 },

        // The Dual Rectifier's arrangement: cold clippers on 39k cathodes ahead
        // of the follower. Restarting from the sample's start found each valve's
        // step limiter still remembering the abandoned attempt, and it clamped
        // the restart up to a hundred volts from where it was put.
        { "four cold stages, the last two clippers on 39k cathodes, into the follower and tone stack, clipped notes at 1 V",
          { 48000.0, 420.0,
            { { 0.0, 100e3, 1800.0, 0.0, 4.7e-9, 470e3 },
              { 0.0, 100e3, 1500.0, 0.0, 4.7e-9, 220e3 },
              { 33e3, 68e3, 39e3, 0.0, 47e-9, 220e3 },
              { 470e3, 150e3, 39e3, 0.0, 47e-9, 1e6 } },
            100e3, true, 4.7e3, 22e-9, false, true, true },
          Drive::clippedNotes, 1.0 },

        { "680k and 470k grid stoppers around a cold stage, the follower, tone stack and second half, a sweep at 0.5 V",
          { 48000.0, 400.0,
            { { 680e3, 68e3, 680.0, 22e-6, 4.7e-9, 220e3 },
              { 33e3, 150e3, 1500.0, 1e-6, 1e-9, 470e3 },
              { 680e3, 100e3, 680.0, 0.0, 22e-9, 3.3e6 },
              { 470e3, 220e3, 680.0, 22e-6, 22e-9, 1e6 } },
            120e3, true, 4.7e3, 100e-9, true, true, false },
          Drive::sweep, 0.5 },

        { "three stages at 96 kHz, a 680k stopper and a 39k cold clipper, the follower, tone stack and second half, a sweep at 0.2 V",
          { 96000.0, 370.0,
            { { 0.0, 120e3, 1800.0, 22e-6, 4.7e-9, 3.3e6 },
              { 680e3, 220e3, 1800.0, 0.0, 4.7e-9, 1e6 },
              { 68e3, 220e3, 39e3, 0.0, 22e-9, 3.3e6 } },
            100e3, true, 4.7e3, 220e-9, true, true, true },
          Drive::sweep, 0.2 },
    };

    for (const auto& test : cases)
    {
        INFO(test.what);

        auto circuit = build(test.preamp);
        REQUIRE(circuit.foundOperatingPoint());

        Guitar guitar(test.drive, test.preamp.rate, test.level);
        const auto length = static_cast<int>(2.0 * test.preamp.rate);
        bool finite = true;
        float peak = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const float out = circuit.process(static_cast<float>(guitar(i)));
            finite = finite && std::isfinite(out);
            peak = std::max(peak, std::abs(out));
        }

        CHECK(finite);
        CHECK(circuit.getNonConvergenceCount() == 0);

        // A sample given up on leaves whatever the iteration was holding, and
        // that has reached kilovolts. Nothing real leaves the supply.
        CHECK(peak < test.preamp.bplus);
    }
}

//==============================================================================
namespace
{
    template <typename Random>
    double pick(Random& random, std::initializer_list<double> options)
    {
        std::uniform_int_distribution<size_t> index(0, options.size() - 1);
        return *(options.begin() + index(random));
    }

    /** A preamp somewhere in the space the fixed cases come from. */
    Preamp randomPreamp(unsigned seed, Drive& drive, double& level)
    {
        std::mt19937 random(seed);
        Preamp p;
        p.rate = pick(random, { 44100.0, 48000.0, 48000.0, 96000.0 });
        p.bplus = pick(random, { 300.0, 340.0, 370.0, 400.0, 420.0 });

        const auto count = static_cast<int>(pick(random, { 1.0, 2.0, 3.0, 4.0 }));
        for (int s = 0; s < 4; ++s)
        {
            GainStage stage;
            stage.stopper = pick(random, { 0.0, 1e3, 33e3, 68e3, 470e3, 680e3 });
            stage.plateLoad = pick(random, { 68e3, 100e3, 120e3, 150e3, 220e3 });
            stage.cathode = pick(random, { 680.0, 820.0, 1.5e3, 1.8e3, 2.7e3, 10e3, 39e3 });
            stage.bypass = stage.cathode >= 10e3 ? pick(random, { 0.0, 0.0, 0.68e-6 }) : pick(random, { 0.0, 1e-6, 22e-6 });
            stage.coupling = pick(random, { 1e-9, 4.7e-9, 22e-9, 47e-9 });
            stage.leak = pick(random, { 220e3, 470e3, 1e6, 3.3e6 });

            if (s < count)
                p.stages.push_back(stage);
        }

        p.followerCathode = pick(random, { 47e3, 82e3, 100e3, 120e3 });
        p.toneStack = pick(random, { 0.0, 1.0 }) > 0.5;
        p.load = pick(random, { 4.7e3, 10e3, 33e3, 100e3 });
        p.outputCoupling = pick(random, { 22e-9, 100e-9, 220e-9 });
        p.secondHalf = pick(random, { 0.0, 1.0 }) > 0.5;
        p.interelectrode = pick(random, { 0.0, 1.0, 1.0 }) > 0.5;
        p.predict = pick(random, { 0.0, 1.0 }) > 0.5;
        level = pick(random, { 0.05, 0.2, 0.5, 1.0, 2.0 });
        drive = static_cast<Drive>(static_cast<int>(pick(random, { 0.0, 1.0, 2.0, 3.0 })));
        return p;
    }

    juce::String describe(unsigned seed, const Preamp& p, Drive drive, double level)
    {
        const char* drives[] = { "plucks", "clipped notes", "sweep", "attacks" };

        juce::String s;
        s << "seed " << static_cast<int>(seed) << ": " << p.rate << " Hz, B+ " << p.bplus << ", [";

        for (size_t k = 0; k < p.stages.size(); ++k)
        {
            const auto& stage = p.stages[k];
            s << (k > 0 ? " | " : "") << "stopper " << stage.stopper << " Ra " << stage.plateLoad << " Rk " << stage.cathode
              << (stage.bypass > 0.0 ? " bypassed " + juce::String(stage.bypass) : juce::String(" cold"))
              << " Cc " << stage.coupling << " leak " << stage.leak;
        }

        s << "], follower Rk " << p.followerCathode << (p.toneStack ? " into the tone stack" : " into " + juce::String(p.load))
          << ", Cout " << p.outputCoupling << (p.secondHalf ? ", second half" : "")
          << (p.interelectrode ? ", interelectrode" : ", no interelectrode") << (p.predict ? ", predict" : "")
          << ", " << drives[static_cast<int>(drive)] << " at " << level << " V";
        return s;
    }
} // namespace

TEST_CASE("Random high-gain preamps converge on every sample", "[.convergencestress]")
{
    // A search, not a test: hidden, since a thorough one takes minutes. Every
    // preamp that fails is printed with its seed and its parts.
    const int count = juce::SystemStats::getEnvironmentVariable("STRESS_COUNT", "100").getIntValue();
    const auto first = static_cast<unsigned>(juce::SystemStats::getEnvironmentVariable("STRESS_SEED", "1").getIntValue());
    const bool where = juce::SystemStats::getEnvironmentVariable("STRESS_WHERE", "").isNotEmpty();

    int failures = 0;
    int hardSamples = 0;
    int worst = 0;
    long long samples = 0;
    long long hardIterations = 0;

    for (unsigned seed = first; seed < first + static_cast<unsigned>(count); ++seed)
    {
        Drive drive = Drive::plucks;
        double level = 0.0;
        const auto preamp = randomPreamp(seed, drive, level);
        auto circuit = build(preamp);

        if (! circuit.foundOperatingPoint() || ! circuit.hasUsableFactorisation())
        {
            std::cout << "NO BIAS POINT  " << describe(seed, preamp, drive, level) << std::endl;
            ++failures;
            continue;
        }

        Guitar guitar(drive, preamp.rate, level, seed * 7919u);
        const auto length = static_cast<int>(2.0 * preamp.rate);
        bool sane = true;
        int worstHere = 0;

        for (int i = 0; i < length; ++i)
        {
            const int failedBefore = circuit.getNonConvergenceCount();
            const float out = circuit.process(static_cast<float>(guitar(i)));
            sane = sane && std::isfinite(out) && std::abs(out) < preamp.bplus;

            if (where && circuit.getNonConvergenceCount() > failedBefore)
                std::cout << "  sample " << i << " (" << i / preamp.rate << " s) did not converge" << std::endl;

            const int iterations = circuit.getLastIterationCount();
            worstHere = std::max(worstHere, iterations);

            if (iterations > 32)
            {
                ++hardSamples;
                hardIterations += iterations;
            }
        }

        samples += length;
        worst = std::max(worst, worstHere);

        if (circuit.getNonConvergenceCount() > 0 || ! sane)
        {
            ++failures;
            std::cout << "FAIL (" << circuit.getNonConvergenceCount() << " non-converged" << (sane ? "" : ", non-finite or past the rails")
                      << ", worst " << worstHere << " iterations, " << circuit.getPortCount() << " ports)  "
                      << describe(seed, preamp, drive, level) << std::endl;
        }
    }

    std::cout << count << " preamps, " << samples << " samples: " << failures << " failing, " << hardSamples
              << " samples needed continuation (" << hardIterations << " iterations), worst " << worst << std::endl;
}
