// Six real amplifiers drawn on the sheet, and the engine's diagnostics, in one file.
//
// The amps are the heaviest circuits Céline runs, and each was a known problem at some
// point: samples that would not converge, clicks, voltages running away. Each is a
// drawing of a published schematic, named after the model it derives from, never the
// maker. They are held here two ways:
//
//   - "Heavy amp sheets settle every sample" plays each through the plugin as a user's
//     would (restoreDocument(), the document's own knobs, gains and oversampling, then
//     processBlock()) and checks that nothing is held or runs away, and that the output
//     level is the one the amp had when it was added. Run it before and after any
//     engine change: ./Tests "[heavysheets]"
//   - The hidden [.diagnostics] cases print tables -- cost a sample, Newton iterations,
//     the share of a core at 1-16x, held samples, and where a sample's time goes -- over
//     the prefab circuits and these amps: ./Tests "[.diagnostics]" -s
//     "phase breakdown" needs the engine built with CELINE_ENGINE_PROFILE defined (see
//     CelineEngine/Profiling.h). Timings are TSC ticks on x86, nanoseconds elsewhere.
//
// The sheets are kept here rather than as files: each .celsch, gzipped and in base64.
// To open or change one, "Heavy amp sheets: write them out" puts them in a folder as
// .celsch files; after editing, "Heavy amp sheets: encode a folder" prints the entries
// to paste back below. Update an amp's output level only for a change meant to alter
// its sound, after listening to it.

#include "PluginProcessor.h"
#include "Schematic/Schematic.h"
#include "Schematic/SchematicBuilder.h"

#include <CelineEngine/Circuits.h>
#include <CelineEngine/Components/FastMath.h>
#include <CelineEngine/Profiling.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    //==========================================================================
    // The sheets

    struct HeavySheet
    {
        /** A short name, and the .celsch's file name when written out. */
        const char* file;

        const char* description;

        /** RMS of the left output over the whole drive, in dBFS, measured when the
            sheet was added. A sound fingerprint, not a reference render: a change
            here means the amp sounds different. */
        double outputRmsDb;

        /** The .celsch, gzipped, in base64. */
        const char* document;
    };

    const HeavySheet heavySheets[] = {
        { "v4-inspired-bass-1977", "V-4 inspired bass amp (1977)", 0.60,
          "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"
          "PTs6Wbw8fb3Y+qv/A81bF3CKkwAA"
        },
        { "v4b-inspired-bass-1971", "V-4B inspired bass amp (1971)", 0.29,
          "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"
        },
        { "vh140c-inspired-channel-a-preamp", "VH-140C inspired, channel A preamp", -3.45,
          "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"
        },
        { "vh4-inspired-channel-3-full", "VH4 inspired, channel 3, full amp", -12.65,
          "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"
        },
        { "5150iii-inspired-channel-3-full", "5150 III inspired, channel 3, full amp", -9.49,
          "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"
        },
        { "rgt100-inspired-channel-2-preamp", "RGT100 inspired, channel 2 preamp", -3.04,
          "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"
          "V33If8rj4G93XL1if+/mzYv/AYomf+UqRwAA"
        },
    };

    /** A sheet's document, as the .celsch file it was. */
    juce::String documentOf (const HeavySheet& sheet)
    {
        juce::MemoryOutputStream compressed;
        juce::Base64::convertFromBase64 (compressed, sheet.document);

        juce::MemoryInputStream input (compressed.getData(), compressed.getDataSize(), false);
        juce::GZIPDecompressorInputStream gunzip (&input, false, juce::GZIPDecompressorInputStream::gzipFormat);
        return gunzip.readEntireStreamAsString();
    }

    //==========================================================================
    // Playing a sheet through the plugin

    constexpr double heavyRate = 48000.0;
    constexpr int heavyBlock = 256;

    /** A guitar, roughly: two notes half a second each, their harmonics decaying faster
        than the fundamental, then hard pick attacks at twice the level, sixteen a second.
        The attacks are what used to break these amps: the operating point has to move
        volts between one sample and the next. */
    float guitar (int n)
    {
        constexpr double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 98.0, 123.5 };
        const int noteLength = static_cast<int> (heavyRate / 2.0);
        const int attacksFrom = noteLength * 2;
        const double level = n < attacksFrom ? 0.5 : 1.0;
        const int length = n < attacksFrom ? noteLength : noteLength / 8;
        const double t = static_cast<double> (n % length) / heavyRate;
        const double f = notes[(n / length) % 8];
        double x = 0.0;

        for (int h = 1; h <= 10; ++h)
            x += std::sin (2.0 * std::numbers::pi * f * h * t) / h * std::exp (-t * (3.0 + h));

        return static_cast<float> (level * x);
    }

    struct Played
    {
        bool built = false;
        juce::String error;
        double peak = 0.0;
        double rmsDb = -400.0;
        bool finite = true;
        juce::int64 held = 0;
    };

    Played play (const HeavySheet& sheet, double seconds)
    {
        Played played;

        const auto xml = juce::parseXML (documentOf (sheet));
        if (xml == nullptr)
        {
            played.error = "the embedded document does not read";
            return played;
        }

        PluginProcessor plugin;
        plugin.prepareToPlay (heavyRate, heavyBlock);

        if (! plugin.restoreDocument (juce::ValueTree::fromXml (*xml)))
        {
            played.error = "restoreDocument refused it";
            return played;
        }

        const auto result = plugin.rebuild();
        played.built = result.isValid();
        played.error = result.error;
        if (! played.built)
            return played;

        // Straight in, no fade: prepareToPlay after the build swaps the new circuit in
        // at once (see CLAUDE.md, "a dip through silence").
        plugin.prepareToPlay (heavyRate, heavyBlock);

        juce::AudioBuffer<float> buffer (2, heavyBlock);
        juce::MidiBuffer midi;
        double sumOfSquares = 0.0;
        juce::int64 count = 0;
        int n = 0;

        for (int block = 0; block < juce::roundToInt (seconds * heavyRate / heavyBlock); ++block)
        {
            for (int i = 0; i < heavyBlock; ++i, ++n)
            {
                const auto x = guitar (n);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < heavyBlock; ++i)
            {
                const double y = buffer.getSample (0, i);
                played.finite = played.finite && std::isfinite (y);
                played.peak = std::max (played.peak, std::abs (y));
                sumOfSquares += y * y;
                ++count;
            }
        }

        played.held = plugin.getHeldSampleTotal();
        played.rmsDb = 10.0 * std::log10 (std::max (sumOfSquares / static_cast<double> (count), 1.0e-40));
        return played;
    }
} // namespace

TEST_CASE ("Heavy amp sheets settle every sample", "[plugin][heavysheets]")
{
    for (const auto& sheet : heavySheets)
    {
        INFO (sheet.description);
        const auto played = play (sheet, 1.5);

        INFO (played.error);
        REQUIRE (played.built);

        UNSCOPED_INFO ("peak " << played.peak << ", RMS " << played.rmsDb << " dBFS, held " << played.held);
        CHECK (played.finite);
        CHECK (played.held == 0);
        CHECK (played.peak > 1.0e-3); // it ran
        CHECK (played.peak < 4.0);    // and nothing ran away

        // Within half a decibel of what it measured when it was added.
        CHECK (std::abs (played.rmsDb - sheet.outputRmsDb) < 0.5);
    }
}

TEST_CASE ("Heavy amp sheets: write them out", "[.heavysheets]")
{
    // Into HEAVY_SHEETS_DIR, or a temporary folder, as .celsch files to open in Céline.
    auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("HEAVY_SHEETS_DIR", {}));
    if (folder == juce::File())
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("heavy-amp-sheets");

    REQUIRE (folder.createDirectory());

    for (const auto& sheet : heavySheets)
        REQUIRE (folder.getChildFile (juce::String (sheet.file) + ".celsch").replaceWithText (documentOf (sheet), false, false, "\n"));

    std::printf ("Heavy amp sheets written to %s\n", folder.getFullPathName().toRawUTF8());
}

TEST_CASE ("Heavy amp sheets: encode a folder", "[.heavysheets]")
{
    // The entries for heavySheets[] from the .celsch files in HEAVY_SHEETS_DIR, gzipped
    // and in base64. Descriptions and output levels are left to fill in.
    const auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("HEAVY_SHEETS_DIR", {}));
    REQUIRE (folder.isDirectory());

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.celsch");
    files.sort();

    for (const auto& file : files)
    {
        juce::MemoryOutputStream compressed;
        {
            juce::GZIPCompressorOutputStream gzip (compressed, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            const auto text = file.loadFileAsString();
            gzip.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
        }

        const auto encoded = juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize());
        std::printf ("        { \"%s\", \"\", 0.0,\n", file.getFileNameWithoutExtension().toRawUTF8());

        for (int i = 0; i < encoded.length(); i += 112)
            std::printf ("          \"%s\"\n", encoded.substring (i, i + 112).toRawUTF8());

        std::printf ("        },\n");
    }
}

//==============================================================================
// Engine diagnostics: measurements, not tests. Each prints a table and checks nothing.

namespace
{
    constexpr double diagnosticsRate = 48000.0;

    /** A circuit to measure, made at a rate. Prefab circuits come as a Circuit or as a
        struct holding one; the owner keeps whichever it was alive. */
    struct Made
    {
        std::shared_ptr<void> owner;
        Circuit* circuit = nullptr;
    };

    template <typename Factory>
    Made prefab (Factory make, double rate)
    {
        using Kind = decltype (make());
        auto owner = std::make_shared<Kind> (make());
        Circuit* circuit = nullptr;

        if constexpr (std::is_same_v<Kind, Circuit>)
            circuit = owner.get();
        else
            circuit = &owner->circuit;

        circuit->prepare (rate);
        return { owner, circuit };
    }

    /** A heavy sheet built straight to the engine, one channel, its knobs where the
        drawing has them. */
    Made sheetCircuit (const HeavySheet& sheet, double rate)
    {
        const auto xml = juce::parseXML (documentOf (sheet));
        if (xml == nullptr)
            return {};

        SchematicModel::Schematic schematic;
        schematic.restoreFromValueTree (juce::ValueTree::fromXml (*xml).getChildWithName ("SCHEMATIC"));

        auto built = std::make_shared<SchematicModel::BuildResult> (SchematicModel::buildCircuits (schematic, rate, 1));
        if (! built->isValid() || built->circuits.empty())
            return {};

        return { built, built->circuits.front().get() };
    }

    /** Every circuit the diagnostics measure, at the level each is driven at: the prefab
        circuits (twice where clean and driven hard behave differently), then the amps. */
    struct Subject
    {
        std::string name;
        float level;
        std::function<Made (double rate)> make;
    };

    std::vector<Subject> subjects()
    {
        std::vector<Subject> list = {
            { "Classic tone stack", 0.5f, [] (double r) { return prefab ([] { return Circuits::makeClassicToneStack(); }, r); } },
            { "Diode clipper (clean)", 0.05f, [] (double r) { return prefab (Circuits::makeDiodeClipper, r); } },
            { "Diode clipper (hard)", 1.0f, [] (double r) { return prefab (Circuits::makeDiodeClipper, r); } },
            { "Transistor booster (clean)", 0.05f, [] (double r) { return prefab (Circuits::makeTransistorBooster, r); } },
            { "Transistor booster (overdriven)", 0.5f, [] (double r) { return prefab (Circuits::makeTransistorBooster, r); } },
            { "Germanium fuzz", 0.3f, [] (double r) { return prefab (Circuits::makeGermaniumFuzz, r); } },
            { "12AX7 triode stage", 0.3f, [] (double r) { return prefab (Circuits::makeTriodeStage, r); } },
            { "EL34 pentode stage", 0.3f, [] (double r) { return prefab (Circuits::makePentodeStage, r); } },
            { "Valve rectifier supply", 0.3f, [] (double r) { return prefab ([] { return Circuits::makeValveRectifierSupply(); }, r); } },
            { "Three-stage valve preamp", 0.3f, [] (double r) { return prefab (Circuits::makeThreeStagePreamp, r); } },
            { "Mid-hump overdrive (clean)", 0.05f, [] (double r) { return prefab ([] { return Circuits::makeMidHumpOverdrive(); }, r); } },
            { "Mid-hump overdrive (hard)", 0.5f, [] (double r) { return prefab ([] { return Circuits::makeMidHumpOverdrive(); }, r); } },
        };

        for (const auto& sheet : heavySheets)
            list.push_back ({ sheet.description, 0.3f, [&sheet] (double r) { return sheetCircuit (sheet, r); } });

        return list;
    }

    std::vector<float> sine (float level, int samples, double rate = diagnosticsRate)
    {
        std::vector<float> block (static_cast<size_t> (samples));

        for (int i = 0; i < samples; ++i)
            block[static_cast<size_t> (i)] = level * static_cast<float> (std::sin (2.0 * std::numbers::pi * 440.0 * i / rate));

        return block;
    }

    /** A square at `level` switching every eight samples: a new operating point between
        one sample and the next, which is when Newton has furthest to go. */
    std::vector<float> square (float level, int samples)
    {
        std::vector<float> block (static_cast<size_t> (samples));

        for (int i = 0; i < samples; ++i)
            block[static_cast<size_t> (i)] = (i / 8) % 2 == 0 ? level : -level;

        return block;
    }

    struct Cost
    {
        double ticksPerSample = 0.0;
        double nanosecondsPerSample = 0.0;
        double iterationsPerSample = 0.0;
    };

    /** One block of a sine at `level`, repeated and timed; the fastest repeat is the one
        with no scheduler interruption in it. Iterations come from a separate pass, so
        reading them costs the timing nothing. */
    Cost measure (Circuit& circuit, float level, int samples = 512, int repeats = 400)
    {
        const auto block = sine (level, samples);
        auto run = block;

        for (int warm = 0; warm < 3; ++warm)
        {
            run = block;
            circuit.process (run.data(), samples);
        }

        double bestNs = 1.0e30, bestTicks = 1.0e30;

        for (int r = 0; r < repeats; ++r)
        {
            run = block;
            const auto wall = std::chrono::steady_clock::now();
            const auto ticks = CelineEngine::Profile::now();
            circuit.process (run.data(), samples);
            bestTicks = std::min (bestTicks, static_cast<double> (CelineEngine::Profile::now() - ticks));
            bestNs = std::min (bestNs, std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - wall).count());
        }

        long long iterations = 0;

        for (const float x : block)
        {
            circuit.process (x);
            iterations += circuit.getLastIterationCount();
        }

        return { bestTicks / samples, bestNs / samples, static_cast<double> (iterations) / samples };
    }
} // namespace

TEST_CASE ("Engine diagnostics: phase breakdown", "[.diagnostics]")
{
    namespace P = CelineEngine::Profile;
    constexpr int samples = 512;

    for (const auto& subject : subjects())
    {
        auto made = subject.make (diagnosticsRate);
        if (made.circuit == nullptr)
            continue;

        // A block to warm up with the counters off, then one counted.
        auto block = sine (subject.level, samples);
        made.circuit->process (block.data(), samples);

        P::reset();
        P::enabled = true;
        block = sine (subject.level, samples);
        made.circuit->process (block.data(), samples);
        P::enabled = false;

        std::printf ("\n%s -- ticks a sample, by phase\n", subject.name.c_str());

        for (int slot = 0; slot < P::SlotCount; ++slot)
            std::printf ("  %-30s %9.0f  (%5.2f calls a sample)\n", P::slotNames[slot],
                         P::ticks[slot] / samples, P::calls[slot] / samples);
    }

#if ! defined(CELINE_ENGINE_PROFILE)
    std::printf ("\n(every count is zero: build with CELINE_ENGINE_PROFILE defined)\n");
#endif
}

TEST_CASE ("Engine diagnostics: circuit cost", "[.diagnostics]")
{
    for (const bool fast : { false, true })
    {
        CircuitComponents::FastMath::setEnabled (fast);
        std::printf ("\n--- %s ---\n", fast ? "fast exp / log / pow" : "exact libm");

        for (const auto& subject : subjects())
        {
            auto made = subject.make (diagnosticsRate);
            if (made.circuit == nullptr)
                continue;

            const auto cost = measure (*made.circuit, subject.level);
            std::printf ("%-44s %8.1f ticks %7.1f ns %5.2f it  (n %d, m %d, terms %d)\n", subject.name.c_str(),
                         cost.ticksPerSample, cost.nanosecondsPerSample, cost.iterationsPerSample,
                         made.circuit->getSystemSize(), made.circuit->getPortCount(), made.circuit->getLinearTermCount());
        }
    }

    CircuitComponents::FastMath::setEnabled (false);
}

TEST_CASE ("Engine diagnostics: iteration counts", "[.diagnostics]")
{
    // Newton iterations a sample over a sine, as shares of the samples taking each
    // count. Linearising the devices is the largest phase of a valve sheet and runs
    // once an iteration, so this is what a change to the convergence test costs.
    for (const auto& subject : subjects())
    {
        auto made = subject.make (diagnosticsRate);
        if (made.circuit == nullptr)
            continue;

        std::array<int, 40> histogram {};
        long long total = 0;
        int highest = 0;
        const auto block = sine (subject.level, 4800);

        for (const float x : block)
        {
            made.circuit->process (x);
            const int iterations = made.circuit->getLastIterationCount();
            total += iterations;
            highest = std::max (highest, iterations);
            ++histogram[static_cast<size_t> (std::clamp (iterations, 0, static_cast<int> (histogram.size()) - 1))];
        }

        std::printf ("%-44s %5.2f mean, max %2d |", subject.name.c_str(), static_cast<double> (total) / static_cast<double> (block.size()), highest);

        for (size_t k = 0; k < histogram.size(); ++k)
            if (histogram[k] != 0)
                std::printf (" %zu:%4.1f%%", k, 100.0 * histogram[k] / static_cast<double> (block.size()));

        std::printf ("\n");
    }
}

TEST_CASE ("Engine diagnostics: oversampling budget", "[.diagnostics]")
{
    // The share of one 48 kHz output sample each circuit takes at 1x to 16x, measured at
    // the real rate: a sample need not cost the same at 768 kHz, since the companion
    // models, the reactive state and the iteration count all move with the rate. One
    // channel; a stereo sheet runs two circuits.
    constexpr double outputRate = 48000.0;
    const double budgetNs = 1.0e9 / outputRate;

    for (const auto& subject : subjects())
    {
        std::printf ("%-44s", subject.name.c_str());

        for (const double factor : { 1.0, 4.0, 8.0, 16.0 })
        {
            auto made = subject.make (outputRate * factor);
            if (made.circuit == nullptr)
                break;

            const auto cost = measure (*made.circuit, subject.level, 256, 120);
            std::printf ("  %2.0fx %6.1f%%", factor, 100.0 * factor * cost.nanosecondsPerSample / budgetNs);
        }

        std::printf ("\n");
    }
}

TEST_CASE ("Engine diagnostics: non-convergence rate", "[.diagnostics]")
{
    // How often a sample cannot be settled and is held (the last good one repeated):
    // the engine's one silent accuracy loss. Over a sine at each circuit's level, a 1 V
    // square, and a 10 V square -- what an input knob turned all the way can send.
    auto held = [] (Circuit& circuit, std::vector<float> block)
    {
        const int before = circuit.getNonConvergenceCount();
        circuit.process (block.data(), static_cast<int> (block.size()));
        return circuit.getNonConvergenceCount() - before;
    };

    constexpr int samples = 4096;
    std::printf ("%-44s %8s %8s %8s   (held of %d samples)\n", "", "sine", "1 V sq", "10 V sq", samples);

    for (const auto& subject : subjects())
    {
        auto made = subject.make (diagnosticsRate);
        if (made.circuit == nullptr)
            continue;

        const int onSine = held (*made.circuit, sine (subject.level, samples));
        const int onSquare = held (*made.circuit, square (1.0f, samples));
        const int onAbuse = held (*made.circuit, square (10.0f, samples));
        std::printf ("%-44s %8d %8d %8d\n", subject.name.c_str(), onSine, onSquare, onAbuse);
    }
}
