#include "helpers/stress_fixture.h"
#include "helpers/realtime_guard.h"

using namespace Stress;

TEST_CASE ("The allocation guard is armed and actually catches one", "[realtime]")
{
    // Every allocation check in this suite is worthless if this is not true, so it is
    // asserted rather than assumed: a guard that silently failed to replace the
    // allocator would turn all of them into tests that pass for no reason.
    REQUIRE (RealtimeCheck::isArmed());

    {
        RealtimeGuard guard;
        CHECK (guard.allocations() == 0);
    }

    {
        RealtimeGuard guard;
        RealtimeCheck::makeOneAllocation();

        CHECK (guard.allocations() >= 1);
    }
}

TEST_CASE ("processBlock never allocates", "[realtime]")
{
    // The rule the audio thread lives by. malloc can take a lock another thread holds,
    // and under memory pressure it goes to the kernel; either way the block is late and
    // the user hears it. Checked in the states a block can actually find the plugin in.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random { 1 };

    const auto worstOver = [&] (int blocks)
    {
        std::size_t worst = 0;

        for (int b = 0; b < blocks; ++b)
        {
            fillNoise (buffer, random);

            RealtimeGuard guard;
            plugin.processBlock (buffer, midi);
            worst = juce::jmax (worst, guard.allocations());
        }

        return worst;
    };

    SECTION ("running")
    {
        CHECK (worstOver (40) == 0);
    }

    SECTION ("with the editor open, which turns the analysers on")
    {
        auto* editor = plugin.createEditorAndMakeActive();
        REQUIRE (editor != nullptr);

        CHECK (worstOver (40) == 0);

        plugin.editorBeingDeleted (editor);
        delete editor;
    }

    SECTION ("bypassed")
    {
        for (auto* parameter : plugin.getParameters())
            if (auto* named = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                if (named->paramID.containsIgnoreCase ("bypass"))
                    named->setValueNotifyingHost (1.0f);

        CHECK (worstOver (20) == 0);
    }

    SECTION ("while the bypass is fading")
    {
        // A 512-sample block is shorter than the 30 ms fade, so toggling every
        // block keeps it moving -- the one state where the dry copy and the
        // circuit are both being blended per sample.
        auto* bypass = plugin.getBypassParameter();
        REQUIRE (bypass != nullptr);

        std::size_t worst = 0;

        for (int b = 0; b < 20; ++b)
        {
            bypass->setValueNotifyingHost ((float) (b % 2));
            fillNoise (buffer, random);

            RealtimeGuard guard;
            plugin.processBlock (buffer, midi);
            worst = juce::jmax (worst, guard.allocations());
        }

        CHECK (worst == 0);
    }

    SECTION ("playing a file, looping, across its end")
    {
        // A short file, so forty blocks wrap it several times: the loop's
        // turnaround is the one branch here that a long file would never reach.
        juce::TemporaryFile wav (".wav");
        {
            juce::AudioBuffer<float> take (2, 3000);
            fillNoise (take, random);

            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> stream (wav.getFile().createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                              .withSampleRate (48000.0)
                                                              .withNumChannels (2)
                                                              .withBitsPerSample (24));
            REQUIRE (writer != nullptr);
            writer->writeFromAudioSampleBuffer (take, 0, take.getNumSamples());
        }

        auto& player = plugin.getInputFilePlayer();
        player.load (wav.getFile());
        REQUIRE (player.waitUntilLoaded (10000));
        player.setLooping (true);
        player.play();

        CHECK (worstOver (40) == 0);
        CHECK (player.isPlaying());
    }

    SECTION ("fed silence, where anything decaying goes denormal")
    {
        for (int b = 0; b < 200; ++b)
        {
            buffer.clear();

            RealtimeGuard guard;
            plugin.processBlock (buffer, midi);
            REQUIRE (guard.allocations() == 0);
        }
    }
}
