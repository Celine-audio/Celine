#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
    /** A one-pole lowpass impulse, written as a real .wav. Not white noise or a
        click: the test has to be able to tell that the convolution *happened*,
        and a filter with an obvious effect on a bright signal is the cheapest
        way to see it. */
    juce::File writeImpulse (const juce::File& folder, const juce::String& name,
                             int length, double sampleRate = 48000.0)
    {
        const auto file = folder.getChildFile (name);

        juce::AudioBuffer<float> ir (1, length);
        float state = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const float in = i == 0 ? 1.0f : 0.0f;
            state += 0.15f * (in - state);
            ir.setSample (0, i, state);
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (1)
                                 .withBitsPerSample (24);

        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (ir, 0, length);
        writer.reset();

        return file;
    }

    SchematicModel::Element* findOutput (SchematicModel::Schematic& schematic)
    {
        for (auto& element : schematic.getElements())
            if (element.type == SchematicModel::ElementType::Output)
                return &element;

        return nullptr;
    }

    /** Whether the cabinet is loaded.

        It no longer needs waiting for. The response is built on the thread that
        asked for it and handed to the engine there, so by the time refreshCabinet
        returns the filter is set and the audio thread picks it up at its next
        frame boundary -- within 256 samples rather than whenever a background
        thread got to it. This used to pump blocks for up to a second: measured at
        about 275 of them before a cabinet became audible, which is a second and a
        half of a session sounding wrong after loading one.

        Kept as a function rather than inlined so the tests below still read as
        asking a question about the cabinet, and so this note stays attached to
        the reason they no longer have to wait for an answer. */
    bool waitForCabinet (PluginProcessor& plugin)
    {
        return plugin.getCabinetLength() > 1;
    }

    /** One block of the same noise through a fresh instance, with the cabinet
        either in or out.

        A fresh processor per capture rather than one toggled between states: the
        circuit carries capacitor charge from block to block, so the second
        measurement of a shared instance would differ from the first for reasons
        that have nothing to do with the cabinet. */
    juce::AudioBuffer<float> capture (const juce::File& ir, bool cabOn)
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (48000.0, 512);

        for (auto& element : plugin.getSchematic().getElements())
        {
            if (element.type == SchematicModel::ElementType::Output)
            {
                element.cabEnabled = cabOn;
                element.cabFile = ir.getFullPathName();
            }
        }

        plugin.refreshCabinet();

        if (cabOn && ! waitForCabinet (plugin))
            return {};

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (1234);

        // Two blocks: the first primes the convolution tail and the circuit.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 512; ++s)
                    buffer.setSample (c, s, random.nextFloat() * 2.0f - 1.0f);

            plugin.processBlock (buffer, midi);
        }

        juce::AudioBuffer<float> out (1, 512);
        out.copyFrom (0, 0, buffer, 0, 0, 512);
        return out;
    }

    /** Mean absolute difference between two captures. */
    double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double total = 0.0;

        for (int s = 0; s < a.getNumSamples(); ++s)
            total += std::abs (a.getSample (0, s) - b.getSample (0, s));

        return total / a.getNumSamples();
    }
}

TEST_CASE ("The Output terminal's cabinet is in the signal path", "[cab]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    const auto ir = writeImpulse (folder, "cab.wav", PluginProcessor::cabinetImpulseSamples);

    const auto off = capture (ir, false);
    const auto on = capture (ir, true);

    REQUIRE (on.getNumSamples() == 512);
    REQUIRE (off.getNumSamples() == 512);

    // What is under test is that the convolution reaches the output at all --
    // *how* it sounds is a property of the impulse response, not of this code,
    // and asserting on the spectrum here only measures whichever example sheet
    // happens to be the default.
    const auto changed = difference (on, off);
    const auto level = off.getRMSLevel (0, 0, 512);

    INFO ("mean |on - off| " << changed << " against a dry RMS of " << level);
    CHECK (changed > level * 0.2);

    // And switching it out has to give back exactly what was there before --
    // the cabinet is a branch in processBlock, not a state the signal path
    // keeps.
    const auto offAgain = capture (ir, false);
    CHECK (difference (off, offAgain) == Catch::Approx (0.0).margin (1.0e-7));

    folder.deleteRecursively();
}

TEST_CASE ("A missing impulse response is a warning, not silence", "[cab]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto* output = findOutput (plugin.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = "/nowhere/at/all/missing-cab.wav";

    // The whole point of the graceful failure: it says so, it does not stop the
    // build, and the setting survives so the file can come back.
    const auto problem = plugin.refreshCabinet();
    CHECK (problem.isNotEmpty());
    CHECK (problem.contains ("missing-cab.wav"));
    CHECK (output->cabEnabled);

    // Audio still arrives.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < 512; ++s)
            buffer.setSample (c, s, 0.25f);

    plugin.processBlock (buffer, midi);

    CHECK (buffer.getMagnitude (0, 512) > 0.0f);
}

TEST_CASE ("A file that isn't audio is refused by name", "[cab]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    const auto notAudio = folder.getChildFile ("lies.wav");
    notAudio.replaceWithText ("this is not a wav file");

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto* output = findOutput (plugin.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = notAudio.getFullPathName();

    const auto problem = plugin.refreshCabinet();
    INFO (problem);
    CHECK (problem.contains ("lies.wav"));

    folder.deleteRecursively();
}

TEST_CASE ("The cabinet is capped at 2048 samples at 48 kHz", "[cab]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    // Four times the cap, so truncation is unmistakable.
    const auto ir = writeImpulse (folder, "long.wav", PluginProcessor::cabinetImpulseSamples * 4);

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto* output = findOutput (plugin.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = ir.getFullPathName();
    CHECK (plugin.refreshCabinet().isEmpty());
    REQUIRE (waitForCabinet (plugin));

    INFO ("loaded length " << plugin.getCabinetLength());
    CHECK (plugin.getCabinetLength() == PluginProcessor::cabinetImpulseSamples);

    folder.deleteRecursively();
}

TEST_CASE ("A cabinet survives a save and load", "[cab]")
{
    PluginProcessor source;
    source.prepareToPlay (48000.0, 512);

    auto* output = findOutput (source.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = "/some/where/vintage30.wav";

    const auto document = source.createDocument();

    PluginProcessor restored;
    restored.prepareToPlay (48000.0, 512);
    REQUIRE (restored.restoreDocument (document));

    auto* restoredOutput = findOutput (restored.getSchematic());
    REQUIRE (restoredOutput != nullptr);

    CHECK (restoredOutput->cabEnabled);
    CHECK (restoredOutput->cabFile == "/some/where/vintage30.wav");

    // Switched off has to survive too -- it is a setting, not an absence.
    output->cabEnabled = false;
    const auto offDocument = source.createDocument();

    PluginProcessor second;
    second.prepareToPlay (48000.0, 512);
    REQUIRE (second.restoreDocument (offDocument));

    auto* secondOutput = findOutput (second.getSchematic());
    REQUIRE (secondOutput != nullptr);
    CHECK (! secondOutput->cabEnabled);
    CHECK (secondOutput->cabFile == "/some/where/vintage30.wav");
}

TEST_CASE ("The cabinet is the same duration at every sample rate", "[cab]")
{
    // The cap is 42.7 ms, not 2048 samples, and it has to stay 42.7 ms wherever
    // the session runs. It did not: the previous engine applied the cap while
    // reading the file, so the count was measured in the *file's* rate and then
    // resampled along with everything else -- a 48 kHz cabinet in a 96 kHz session
    // came out 8192 taps, 85 ms, twice the filter anybody had chosen. The cap is
    // applied after resampling now, which is what makes the duration the thing
    // that is fixed.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-rates");
    folder.createDirectory();

    // A 48 kHz file, longer than the cap, so every rate has to truncate it.
    const auto ir = writeImpulse (folder, "rates.wav", PluginProcessor::cabinetImpulseSamples * 4);

    for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (rate, 512);

        auto* output = findOutput (plugin.getSchematic());
        REQUIRE (output != nullptr);

        output->cabEnabled = true;
        output->cabFile = ir.getFullPathName();
        REQUIRE (plugin.refreshCabinet().isEmpty());
        REQUIRE (waitForCabinet (plugin));

        const auto expected = juce::roundToInt (PluginProcessor::cabinetImpulseSamples
                                                * rate / PluginProcessor::cabinetReferenceRate);

        const auto seconds = (double) plugin.getCabinetLength() / rate;

        INFO ("at " << rate << " Hz: " << plugin.getCabinetLength() << " taps, "
                    << seconds * 1000.0 << " ms");

        CHECK (plugin.getCabinetLength() == expected);

        // And the duration itself, which is the thing the cap is really about.
        CHECK (std::abs (seconds - PluginProcessor::cabinetImpulseSamples
                                       / PluginProcessor::cabinetReferenceRate) < 1.0e-4);
    }

    folder.deleteRecursively();
}

TEST_CASE ("A cabinet is audible in the block after it is loaded", "[cab]")
{
    // The previous engine handed the file to a background thread, and the filter
    // arrived whenever that thread got to it -- measured at about 275 blocks, a
    // second and a half, during which the plugin sounded like no cabinet at all.
    // The response is built on the calling thread now and picked up at the next
    // frame boundary, so one block is enough.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-prompt");
    folder.createDirectory();

    const auto ir = writeImpulse (folder, "prompt.wav", PluginProcessor::cabinetImpulseSamples);

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto* output = findOutput (plugin.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = ir.getFullPathName();

    REQUIRE (plugin.refreshCabinet().isEmpty());

    // No blocks pumped, no sleeping: the length is right as soon as the call returns.
    CHECK (plugin.getCabinetLength() == PluginProcessor::cabinetImpulseSamples);

    folder.deleteRecursively();
}

TEST_CASE ("Swapping the cabinet under a running audio thread", "[cab]")
{
    // The reason for the engine change. juce::dsp::Convolution documents that its
    // methods may not be interleaved -- a load has to be synchronised with
    // process(), "which in practice means making the load() call from the audio
    // thread" -- and loading from the message thread while audio ran is a data
    // race whose symptom is a heap-use-after-free rather than a sound. This engine
    // is built the other way round, and this is the test that says so; run it under
    // ThreadSanitizer and it has to stay quiet.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-cab-swap");
    folder.createDirectory();

    juce::Array<juce::File> cabs;
    for (int i = 0; i < 4; ++i)
        cabs.add (writeImpulse (folder, "swap" + juce::String (i) + ".wav",
                                256 + 256 * i));

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 256);

    auto* output = findOutput (plugin.getSchematic());
    REQUIRE (output != nullptr);

    output->cabEnabled = true;
    output->cabFile = cabs[0].getFullPathName();
    REQUIRE (plugin.refreshCabinet().isEmpty());

    std::atomic<bool> stopping { false }, sawNonFinite { false };
    std::atomic<std::int64_t> blocks { 0 };

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        juce::Random random { 55 };

        while (! stopping.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.25f);

            plugin.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        sawNonFinite.store (true);

            blocks.fetch_add (1);
        }
    });

    for (int round = 0; round < 40; ++round)
    {
        output->cabFile = cabs[round % cabs.size()].getFullPathName();
        plugin.refreshCabinet();
        juce::Thread::sleep (10);
    }

    stopping.store (true);
    audio.join();

    CHECK (blocks.load() > 100);
    CHECK_FALSE (sawNonFinite.load());

    folder.deleteRecursively();
}
