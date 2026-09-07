#pragma once

#include "stress_common.h"

/**
    Puts this plugin into the busiest state it has, so the shared stress battery is
    exercising the DSP rather than an early return.

    Every plugin in the house has a path that does almost nothing until it has been
    given something to work with -- GALLERY with no cabinet loaded, AURA with no match
    -- and a stress test that never gets past that is a stress test of the early
    return. One of these lives in each plugin; the battery itself is identical.
*/
namespace Stress
{
    struct Busy
    {
        explicit Busy (PluginProcessor& plugin)
        {
            // Celine starts with a circuit already in it, and the solver runs from the
            // first block, so there is nothing to arrange -- but the oversampler and
            // the matrix are sized here rather than on the first callback.
            plugin.prepareToPlay (48000.0, 512);
        }
    };
}

namespace Stress
{
    /** Swaps the circuit under the audio thread, which is what loading a schematic
        does: the message thread builds a new solver and exchanges it under the spin
        lock the audio thread only ever tries. */
    inline void reload (PluginProcessor& plugin, int round)
    {
        // rebuild() is what pressing anything in the schematic editor ends up calling:
        // it builds a new solver on the message thread and exchanges it under the lock
        // the audio thread only ever tries.
        juce::ignoreUnused (round);
        plugin.rebuild();
    }
}
