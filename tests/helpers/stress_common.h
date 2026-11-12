#pragma once

#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

/** Things real hosts do that a plugin has to survive: changing and overrunning block
    sizes, sample rate changes while loaded, processBlock before prepareToPlay, buffers of
    denormals or NaN, per-block automation, and the editor opening and closing meanwhile. */
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

    /** True when every sample is finite. */
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

    /** Block sizes hosts really use, including odd ones and overruns. */
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
