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
