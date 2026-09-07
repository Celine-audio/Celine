#pragma once

#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

/**
    The things a host does that a plugin has to survive.

    Not a list of what a well-behaved host does -- a list of what real ones actually do,
    which is a different and longer list: block sizes that change without warning and
    overrun what was prepared for, sample rates that change under a loaded plugin,
    processBlock arriving before prepareToPlay, buffers full of denormals or NaN left by
    whatever ran before, automation moving a parameter every block, and the editor
    opening and closing while all of it happens.
*/
namespace Stress
{
    inline void fillNoise (juce::AudioBuffer<float>& buffer, juce::Random& random, float level = 0.25f)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * level);
    }

    inline void fill (juce::AudioBuffer<float>& buffer, float value)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, value);
    }

    /** True when every sample is a real number. NaN and infinity both fail: once either
        reaches a filter with state, it stays there, and the track is silent-or-screaming
        until the plugin is reloaded. */
    inline bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    return false;
        }

        return true;
    }

    inline float peak (const juce::AudioBuffer<float>& buffer)
    {
        float worst = 0.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            worst = juce::jmax (worst, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        return worst;
    }

    /** The block sizes hosts really use, including the awkward ones: Pro Tools uses
        powers of two, Live can hand out anything at all when the buffer is being
        resized, and a host under load can overrun what it declared. */
    inline const std::vector<int>& blockSizes()
    {
        static const std::vector<int> sizes { 1, 2, 3, 7, 16, 31, 32, 64, 65, 128, 127,
                                              256, 441, 512, 480, 1024, 1023, 2048, 4096 };
        return sizes;
    }

    inline const std::vector<double>& sampleRates()
    {
        static const std::vector<double> rates { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        return rates;
    }
}
