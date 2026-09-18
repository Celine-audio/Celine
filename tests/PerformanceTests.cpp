#include "helpers/stress_fixture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace Stress;

namespace
{
    /** How long one block takes, as a share of the time that block represents.

        The number that decides whether a plugin drops out. A block of 256 samples at
        48 kHz is 5.33 ms of audio; if processing it takes 5.33 ms the plugin is using
        one whole core just to keep up, and anything else on the machine pushes it over.
        Reported as a percentage of realtime, per instance.

        The worst block matters more than the average: a host's deadline is per block,
        so one block at 400% is a click even if the average is 5%. */
    struct Cost
    {
        double meanPercent = 0.0;
        double worstPercent = 0.0;
        double p99Percent = 0.0;
    };

    Cost measure (PluginProcessor& plugin, double sampleRate, int blockSize, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { 5150 };

        // Warm up: first blocks pay for page faults and lazily-built tables.
        for (int b = 0; b < 32; ++b)
        {
            fillNoise (buffer, random);
            plugin.processBlock (buffer, midi);
        }

        std::vector<double> samples;
        samples.reserve ((size_t) blocks);

        for (int b = 0; b < blocks; ++b)
        {
            fillNoise (buffer, random);

            const auto start = std::chrono::steady_clock::now();
            plugin.processBlock (buffer, midi);
            const std::chrono::duration<double> elapsed { std::chrono::steady_clock::now() - start };

            samples.push_back (elapsed.count());
        }

        const auto blockSeconds = (double) blockSize / sampleRate;
        std::sort (samples.begin(), samples.end());

        Cost cost;
        cost.meanPercent = std::accumulate (samples.begin(), samples.end(), 0.0)
                         / (double) samples.size() / blockSeconds * 100.0;
        cost.worstPercent = samples.back() / blockSeconds * 100.0;
        cost.p99Percent = samples[(size_t) ((double) samples.size() * 0.99)] / blockSeconds * 100.0;

        return cost;
    }
}

TEST_CASE ("The cost of a block, as a share of realtime", "[performance]")
{
    // Debug builds are three to ten times slower than what ships, so the numbers here
    // are a ceiling rather than a forecast -- but a regression shows up in them just as
    // clearly, and the shape across block sizes is the same either way.
    std::printf ("\n  %-28s %8s %8s %8s\n", "", "mean", "p99", "worst");

    for (const auto rate : { 48000.0, 96000.0 })
    {
        for (const auto size : { 64, 128, 256, 512, 1024 })
        {
            PluginProcessor plugin;
            const Busy busy { plugin };
            plugin.prepareToPlay (rate, size);

            const auto cost = measure (plugin, rate, size, 300);

            std::printf ("  %5.0f kHz, %4d samples %s %7.1f%% %7.1f%% %7.1f%%\n",
                         rate / 1000.0, size, "      ",
                         cost.meanPercent, cost.p99Percent, cost.worstPercent);

            // A single instance taking more than a whole core at a sane buffer size
            // would drop on its own, before the session has anything else in it.
            if (size >= 128)
                CHECK (cost.meanPercent < 100.0);
        }
    }

    std::printf ("\n");
}

TEST_CASE ("No block is wildly more expensive than its neighbours", "[performance]")
{
    // What a click sounds like: not a high average, but one block that takes far longer
    // than the rest, because something was rebuilt inside the callback. The worst block
    // is compared against the median rather than the mean, so a handful of slow ones
    // cannot hide in the baseline.
    PluginProcessor plugin;
    const Busy busy { plugin };

    constexpr double rate = 48000.0;
    constexpr int size = 256;

    plugin.prepareToPlay (rate, size);

    juce::AudioBuffer<float> buffer (2, size);
    juce::MidiBuffer midi;
    juce::Random random { 5151 };

    for (int b = 0; b < 64; ++b)
    {
        fillNoise (buffer, random);
        plugin.processBlock (buffer, midi);
    }

    std::vector<double> times;

    for (int b = 0; b < 2000; ++b)
    {
        fillNoise (buffer, random);

        const auto start = std::chrono::steady_clock::now();
        plugin.processBlock (buffer, midi);
        const std::chrono::duration<double> elapsed { std::chrono::steady_clock::now() - start };

        times.push_back (elapsed.count());
    }

    auto sorted = times;
    std::sort (sorted.begin(), sorted.end());

    const auto median = sorted[sorted.size() / 2];
    const auto worst = sorted.back();
    const auto blockSeconds = (double) size / rate;

    std::printf ("\n  median %6.3f ms, worst %6.3f ms  (a block is %.3f ms of audio)\n"
                 "  worst is %.1fx the median\n\n",
                 median * 1000.0, worst * 1000.0, blockSeconds * 1000.0, worst / median);

    // The scheduler alone accounts for a good deal of scatter in a test process, so
    // this is looking for something structural: a rebuild landing inside a callback,
    // which shows up as tens of times the median rather than a few.
    CHECK (worst < median * 60.0);

    // And whatever the scatter, no single block may take longer than the audio it is
    // for, which is the point at which the host actually drops it.
    CHECK (worst < blockSeconds);
}
