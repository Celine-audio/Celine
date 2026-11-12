#include "helpers/stress_fixture.h"
#include "helpers/realtime_guard.h"

#include <atomic>
#include <thread>

using namespace Stress;

namespace
{
    /** Runs processBlock in a loop on its own thread while the test thread does
        message-thread work, recording failures for the test to report. A real thread,
        because the target is races. */
    class AudioThread
    {
    public:
        AudioThread (PluginProcessor& p, int blockSize)
            : plugin (p), buffer (2, blockSize)
        {
            thread = std::thread ([this] { run(); });
        }

        ~AudioThread()
        {
            stopping.store (true);

            if (thread.joinable())
                thread.join();
        }

        std::int64_t blocksRun() const noexcept { return blocks.load(); }
        bool sawNonFinite() const noexcept { return nonFinite.load(); }
        std::int64_t allocationsSeen() const noexcept { return allocations.load(); }

    private:
        void run()
        {
            juce::Random random { 42 };
            juce::MidiBuffer midi;

            while (! stopping.load())
            {
                fillNoise (buffer, random);

                {
                    RealtimeGuard guard;
                    plugin.processBlock (buffer, midi);
                    allocations.fetch_add ((std::int64_t) guard.allocations());
                }

                if (! isFinite (buffer))
                    nonFinite.store (true);

                blocks.fetch_add (1);
            }
        }

        PluginProcessor& plugin;
        juce::AudioBuffer<float> buffer;
        std::thread thread;
        std::atomic<bool> stopping { false }, nonFinite { false };
        std::atomic<std::int64_t> blocks { 0 }, allocations { 0 };
    };
}

TEST_CASE ("Parameter automation from another thread races nothing", "[concurrency]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    AudioThread audio { plugin, 256 };

    auto& parameters = plugin.getParameters();
    REQUIRE (! parameters.isEmpty());

    // Two seconds of moving every control while audio runs.
    const auto until = juce::Time::getMillisecondCounter() + 2000;
    juce::Random random { 43 };

    while (juce::Time::getMillisecondCounter() < until)
    {
        for (auto* parameter : parameters)
            parameter->setValueNotifyingHost (random.nextFloat());

        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }

    CHECK (audio.blocksRun() > 100);
    CHECK_FALSE (audio.sawNonFinite());
    CHECK (audio.allocationsSeen() == 0);
}

TEST_CASE ("Saving and restoring state under load races nothing", "[concurrency]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    AudioThread audio { plugin, 256 };

    const auto until = juce::Time::getMillisecondCounter() + 2000;

    while (juce::Time::getMillisecondCounter() < until)
    {
        juce::MemoryBlock round;
        plugin.getStateInformation (round);
        plugin.setStateInformation (round.getData(), (int) round.getSize());

        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }

    CHECK (audio.blocksRun() > 100);
    CHECK_FALSE (audio.sawNonFinite());
    CHECK (audio.allocationsSeen() == 0);
}

TEST_CASE ("The editor opening and closing under load races nothing", "[concurrency]")
{
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    AudioThread audio { plugin, 256 };

    for (int round = 0; round < 4; ++round)
    {
        auto* editor = plugin.createEditorAndMakeActive();
        REQUIRE (editor != nullptr);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

        plugin.editorBeingDeleted (editor);
        delete editor;

        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    CHECK (audio.blocksRun() > 100);
    CHECK_FALSE (audio.sawNonFinite());
    CHECK (audio.allocationsSeen() == 0);
}

TEST_CASE ("Re-preparing between runs leaves the plugin working", "[concurrency]")
{
    // Hosts stop the audio thread around prepareToPlay (and juce::dsp requires it), so
    // the test does too: stop, re-prepare, restart, and check it still works.
    PluginProcessor plugin;
    const Busy busy { plugin };

    for (const auto rate : { 44100.0, 48000.0, 96000.0, 48000.0 })
    {
        plugin.prepareToPlay (rate, 256);

        AudioThread audio { plugin, 256 };
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

        INFO ("at " << rate << " Hz");
        CHECK (audio.blocksRun() > 10);
        CHECK_FALSE (audio.sawNonFinite());
        CHECK (audio.allocationsSeen() == 0);
    }
}

TEST_CASE ("Reloading what the plugin is playing through, while it plays", "[concurrency]")
{
    // Loading while the transport rolls is genuinely concurrent: the host doesn't stop
    // audio, so the plugin must swap safely itself.
    PluginProcessor plugin;
    const Busy busy { plugin };
    plugin.prepareToPlay (48000.0, 256);

    AudioThread audio { plugin, 256 };

    for (int round = 0; round < 12; ++round)
    {
        Stress::reload (plugin, round);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    }

    CHECK (audio.blocksRun() > 100);
    CHECK_FALSE (audio.sawNonFinite());
    CHECK (audio.allocationsSeen() == 0);
}
