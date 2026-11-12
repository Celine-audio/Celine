#pragma once

#include "stress_common.h"

/** Puts this plugin into its busiest state, so the shared stress battery exercises the
    DSP rather than an early return. Each plugin has its own; the battery is shared. */
namespace Stress
{
    struct Busy
    {
        explicit Busy (PluginProcessor& plugin)
        {
            // Céline starts with a circuit, so there's nothing to arrange; this sizes the
            // oversampler and matrix up front.
            plugin.prepareToPlay (48000.0, 512);
        }
    };
}

namespace Stress
{
    /** Swaps the circuit under the audio thread, as loading a schematic does. */
    inline void reload (PluginProcessor& plugin, int round)
    {
        // rebuild() builds a new solver and swaps it in under the lock the audio thread only
        // tries.
        juce::ignoreUnused (round);
        plugin.rebuild();
    }
}
