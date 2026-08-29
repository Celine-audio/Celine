#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

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

    /** Pumps blocks until the background loader has installed the impulse
        response, and says whether it did.

        `loadImpulseResponse` hands the file to a background thread and returns
        nothing, and the new engine is only picked up inside `process()` -- so a
        test that measures straight after loading measures the *old* one. Note
        the comparison is against 1, not 0: an unloaded Convolution is a
        one-sample impulse, i.e. a pass-through, so "no IR" and "an IR of length
        one" are the same state and zero never appears. Getting that wrong is
        what made the first version of these tests pass a filter that was not
        actually running. */
    bool waitForCabinet (PluginProcessor& plugin)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int attempt = 0; attempt < 200; ++attempt)
        {
            if (plugin.getCabinetLength() > 1)
                return true;

            buffer.clear();
            plugin.processBlock (buffer, midi);
            juce::Thread::sleep (5);
        }

        return false;
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
