#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** Fills a buffer with a tone and runs it through, one block at a time. */
    void render (PluginProcessor& plugin, juce::AudioBuffer<float>& buffer, int blockSize)
    {
        juce::MidiBuffer midi;

        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            const int count = juce::jmin (blockSize, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), start, count);
            plugin.processBlock (chunk, midi);
        }
    }

    juce::AudioBuffer<float> tone (int channels, int samples, double rate)
    {
        juce::AudioBuffer<float> b (channels, samples);

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < samples; ++i)
                b.setSample (c, i, static_cast<float> (
                    0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate)));

        return b;
    }
}

TEST_CASE ("A block larger than the host declared is still processed correctly",
           "[processor][blocksize]")
{
    // Hosts are supposed to honour the size they declared in prepareToPlay, and
    // mostly do. When one does not, the circuit must still run at the timestep
    // it was prepared for -- oversampling makes that a factor of four, so
    // getting it wrong retunes every capacitor in the drawing.
    constexpr double rate = 48000.0;
    constexpr int declared = 256;

    for (const int factor : { 1, 2, 4 })
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (rate, declared);
        plugin.setOversamplingFactor (factor);

        auto small = tone (2, 2048, rate);
        auto large = small;

        render (plugin, small, declared);

        plugin.prepareToPlay (rate, declared);
        plugin.setOversamplingFactor (factor);
        render (plugin, large, 2048);        // four times what it was told

        // Past the settling transient, the two have to agree: the same circuit
        // saw the same samples at the same rate either way.
        double worst = 0.0;

        for (int i = declared * 2; i < small.getNumSamples(); ++i)
            worst = std::max (worst, std::abs ((double) small.getSample (0, i)
                                               - large.getSample (0, i)));

        INFO ("oversampling " << factor << "x, worst sample difference " << worst);
        CHECK (worst < 1.0e-3);
    }
}
