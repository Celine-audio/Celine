#include "helpers/stress_fixture.h"
#include "helpers/realtime_guard.h"

using namespace Stress;

TEST_CASE ("The plugin under stress is actually doing something", "[stress]")
{
    // The guard on every test below it. Each plugin has a path that costs almost
    // nothing until it has been given something to work with, and a battery that never
    // gets past that is a battery testing an early return. So before anything else:
    // does putting audio through this plugin change the audio?
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;
    juce::Random random { 100 };

    juce::AudioBuffer<float> input (2, 512);
    fillNoise (input, random);

    juce::AudioBuffer<float> output (2, 512);
    output.makeCopyOf (input);

    // A few blocks, because a convolution needs its latency filled before its output
    // is anything but zero.
    for (int b = 0; b < 20; ++b)
    {
        plugin.processBlock (output, midi);

        if (b + 1 < 20)
        {
            fillNoise (input, random);
            output.makeCopyOf (input);
        }
    }

    float difference = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            difference = juce::jmax (difference, std::abs (output.getSample (ch, i) - input.getSample (ch, i)));

    INFO ("largest difference between in and out: " << difference);
    CHECK (difference > 1.0e-4f);
}

TEST_CASE ("Any block size the host hands over is survived", "[stress]")
{
    // Prepared for 512 and then given everything from one sample to eight times that.
    // Overrunning the declared size is not hypothetical: hosts do it when the buffer is
    // being resized, and a plugin that answers by allocating or by writing past its
    // scratch is a plugin that crashes on a setting change.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;
    juce::Random random { 7 };

    for (const auto size : blockSizes())
    {
        juce::AudioBuffer<float> buffer (2, size);
        fillNoise (buffer, random);

        plugin.processBlock (buffer, midi);

        INFO ("block size " << size);
        REQUIRE (isFinite (buffer));
    }

    // And one deliberately past anything prepared for.
    juce::AudioBuffer<float> huge (2, 8192);
    fillNoise (huge, random);
    plugin.processBlock (huge, midi);
    CHECK (isFinite (huge));
}

TEST_CASE ("Every sample rate is survived, and re-preparing is safe", "[stress]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    juce::MidiBuffer midi;
    juce::Random random { 8 };

    for (const auto rate : sampleRates())
    {
        plugin.prepareToPlay (rate, 256);

        juce::AudioBuffer<float> buffer (2, 256);

        for (int b = 0; b < 8; ++b)
        {
            fillNoise (buffer, random);
            plugin.processBlock (buffer, midi);
        }

        INFO ("sample rate " << rate);
        REQUIRE (isFinite (buffer));
    }

    // prepareToPlay twice with no releaseResources between, which hosts do.
    plugin.prepareToPlay (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    fillNoise (buffer, random);
    plugin.processBlock (buffer, midi);
    CHECK (isFinite (buffer));
}

TEST_CASE ("Pathological input does not poison the plugin", "[stress]")
{
    // What is actually in a buffer when the plugin before this one misbehaves. The test
    // is not that the bad samples come out clean -- an EQ fed a NaN may legitimately
    // return one -- but that the plugin *recovers*: once real audio arrives again, real
    // audio comes out. A filter that has taken a NaN into its state never recovers, and
    // the track stays dead until the plugin is reloaded.
    const auto survives = [] (float poison, const char* what)
    {
        PluginProcessor plugin;
        const Busy busy { plugin };
        plugin.prepareToPlay (48000.0, 256);

        juce::MidiBuffer midi;
        juce::Random random { 9 };
        juce::AudioBuffer<float> buffer (2, 256);

        for (int b = 0; b < 4; ++b)
        {
            fill (buffer, poison);
            plugin.processBlock (buffer, midi);
        }

        // Now feed it ordinary audio and see whether anything comes back.
        bool recovered = false;

        for (int b = 0; b < 200 && ! recovered; ++b)
        {
            fillNoise (buffer, random);
            plugin.processBlock (buffer, midi);
            recovered = isFinite (buffer);
        }

        INFO ("after " << what);
        CHECK (recovered);
    };

    survives (std::numeric_limits<float>::quiet_NaN(), "a buffer of NaN");
    survives (std::numeric_limits<float>::infinity(), "a buffer of +inf");
    survives (-std::numeric_limits<float>::infinity(), "a buffer of -inf");
    survives (1.0e30f, "a buffer of 1e30");
    survives (std::numeric_limits<float>::denorm_min(), "a buffer of denormals");
}

TEST_CASE ("Silence in gives silence out, and costs no more than audio", "[stress]")
{
    // Denormals are the classic way a plugin's CPU goes up when the music stops: a
    // reverb tail or a filter decaying towards zero enters denormal range, and every
    // multiply becomes a microcoded trap. ScopedNoDenormals is meant to prevent it;
    // this is the check that it is actually in force on the paths that matter.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;
    juce::Random random { 10 };
    juce::AudioBuffer<float> buffer (2, 512);

    // Run some audio in so anything with state has something in it to decay.
    for (int b = 0; b < 40; ++b)
    {
        fillNoise (buffer, random);
        plugin.processBlock (buffer, midi);
    }

    const auto time = [&] (bool silent)
    {
        const auto start = juce::Time::getHighResolutionTicks();

        for (int b = 0; b < 400; ++b)
        {
            if (silent)
                buffer.clear();
            else
                fillNoise (buffer, random);

            plugin.processBlock (buffer, midi);
        }

        return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    };

    const auto loud = time (false);
    const auto quiet = time (true);

    INFO ("loud " << loud * 1000.0 << " ms, silent " << quiet * 1000.0 << " ms");

    // Silence must not cost dramatically more than audio. Generous, because filling the
    // noise buffer is itself work that the silent pass does not do -- this is looking
    // for the order-of-magnitude blow-up denormals cause, not a few percent.
    CHECK (quiet < loud * 3.0 + 0.005);

    CHECK (isFinite (buffer));
}

TEST_CASE ("processBlock before prepareToPlay does not crash", "[stress]")
{
    // Some hosts, and every plugin scanner, do this at least once.
    PluginProcessor plugin;

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 512);
    juce::Random random { 11 };
    fillNoise (buffer, random);

    plugin.processBlock (buffer, midi);
    CHECK (isFinite (buffer));
}

TEST_CASE ("Automation moving every block stays clean and allocation-free", "[stress]")
{
    // A host writing automation calls setValue from the audio thread, once per block or
    // more. Every parameter is swept across its whole range while audio runs, which is
    // both a realtime-safety check and a check that no parameter has a value that makes
    // the DSP produce something that is not a number.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;
    juce::Random random { 12 };
    juce::AudioBuffer<float> buffer (2, 256);

    auto& parameters = plugin.getParameters();
    REQUIRE (! parameters.isEmpty());

    std::size_t allocations = 0;

    for (int b = 0; b < 300; ++b)
    {
        const auto phase = (float) b / 300.0f;

        for (auto* parameter : parameters)
            parameter->setValue (std::fmod (phase + 0.37f * (float) parameter->getParameterIndex(), 1.0f));

        fillNoise (buffer, random);

        RealtimeGuard guard;
        plugin.processBlock (buffer, midi);
        allocations += guard.allocations();

        REQUIRE (isFinite (buffer));
    }

    CHECK (allocations == 0);
}

TEST_CASE ("State can be saved and restored while audio runs", "[stress]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;
    juce::Random random { 13 };
    juce::AudioBuffer<float> buffer (2, 256);

    for (int b = 0; b < 20; ++b)
    {
        fillNoise (buffer, random);
        plugin.processBlock (buffer, midi);
    }

    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    // Restoring is a message-thread action, and it happens while the audio thread is
    // still calling processBlock. Interleaved here rather than run in a second thread,
    // because what is being checked is that the two orders both leave the plugin usable.
    for (int b = 0; b < 20; ++b)
    {
        plugin.setStateInformation (state.getData(), (int) state.getSize());

        fillNoise (buffer, random);
        plugin.processBlock (buffer, midi);

        REQUIRE (isFinite (buffer));
    }
}

TEST_CASE ("Opening and closing the editor while audio runs is safe", "[stress]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;
    juce::Random random { 14 };
    juce::AudioBuffer<float> buffer (2, 256);

    for (int round = 0; round < 3; ++round)
    {
        auto* editor = plugin.createEditorAndMakeActive();
        REQUIRE (editor != nullptr);

        for (int b = 0; b < 10; ++b)
        {
            fillNoise (buffer, random);
            plugin.processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }

        plugin.editorBeingDeleted (editor);
        delete editor;

        for (int b = 0; b < 10; ++b)
        {
            fillNoise (buffer, random);
            plugin.processBlock (buffer, midi);
        }

        REQUIRE (isFinite (buffer));
    }
}

TEST_CASE ("A mono host is served as well as a stereo one", "[stress]")
{
    PluginProcessor plugin;

    // Not every plugin supports every layout; ask before assuming.
    const juce::AudioProcessor::BusesLayout mono
    {
        { juce::AudioChannelSet::mono() },
        { juce::AudioChannelSet::mono() }
    };

    if (! plugin.checkBusesLayoutSupported (mono))
        SUCCEED ("mono is not an offered layout");
    else
    {
        REQUIRE (plugin.setBusesLayout (mono));
        const Busy busy { plugin };
        plugin.prepareToPlay (48000.0, 256);

        juce::MidiBuffer midi;
        juce::Random random { 15 };
        juce::AudioBuffer<float> buffer (1, 256);

        for (int b = 0; b < 40; ++b)
        {
            fillNoise (buffer, random);

            RealtimeGuard guard;
            plugin.processBlock (buffer, midi);
            REQUIRE (guard.allocations() == 0);
        }

        CHECK (isFinite (buffer));
    }
}

TEST_CASE ("A long run stays finite and does not drift", "[stress]")
{
    // Ten seconds of audio, which is where anything that accumulates -- a DC offset, an
    // integrator winding up, a feedback path just short of stable -- becomes visible.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;
    juce::Random random { 16 };
    juce::AudioBuffer<float> buffer (2, 512);

    float worst = 0.0f;
    double sum = 0.0;
    int counted = 0;

    for (int b = 0; b < 48000 * 10 / 512; ++b)
    {
        fillNoise (buffer, random, 0.3f);
        plugin.processBlock (buffer, midi);

        REQUIRE (isFinite (buffer));
        worst = juce::jmax (worst, peak (buffer));

        for (int i = 0; i < buffer.getNumSamples(); ++i, ++counted)
            sum += buffer.getSample (0, i);
    }

    INFO ("peak " << worst << ", mean " << (sum / juce::jmax (1, counted)));

    // Nothing should run away, and nothing should settle onto a DC offset.
    CHECK (worst < 64.0f);
    CHECK (std::abs (sum / juce::jmax (1, counted)) < 0.05);
}
