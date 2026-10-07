#include "BypassFade.h"

void BypassFade::prepare (int numChannels, int maxBlockSize, int maxDelaySamples, double sampleRate)
{
    const auto channels = juce::jmax (1, numChannels);

    dry.setSize (channels, juce::jmax (1, maxBlockSize));

    // A power of two, so the read position wraps with a mask; one more than the longest
    // delay, so a delay of exactly that still reads a sample that has been written.
    const auto length = juce::nextPowerOfTwo (juce::jmax (1, maxDelaySamples) + 1);
    delayLines.assign ((size_t) channels, std::vector<float> ((size_t) length, 0.0f));
    mask = length - 1;

    wet.reset (sampleRate, fadeSeconds);
}

void BypassFade::reset (bool bypassed)
{
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    wet.setCurrentAndTargetValue (bypassed ? 0.0f : 1.0f);
}

void BypassFade::pushDry (const juce::AudioBuffer<float>& input, int latencySamples) noexcept
{
    const auto numSamples = juce::jmin (input.getNumSamples(), dry.getNumSamples());
    const auto delay = juce::jlimit (0, mask, latencySamples);

    dryChannels = juce::jmin (input.getNumChannels(), dry.getNumChannels());

    for (int ch = 0; ch < dryChannels; ++ch)
    {
        auto& line = delayLines[(size_t) ch];
        const auto* in = input.getReadPointer (ch);
        auto* out = dry.getWritePointer (ch);
        auto write = writeIndex;

        for (int i = 0; i < numSamples; ++i)
        {
            line[(size_t) write] = in[i];
            out[i] = line[(size_t) ((write - delay) & mask)];
            write = (write + 1) & mask;
        }
    }

    writeIndex = (writeIndex + numSamples) & mask;
}

void BypassFade::mix (juce::AudioBuffer<float>& processed, bool bypassed) noexcept
{
    wet.setTargetValue (bypassed ? 0.0f : 1.0f);

    const auto numSamples = juce::jmin (processed.getNumSamples(), dry.getNumSamples());
    const auto channels = juce::jmin (processed.getNumChannels(), dryChannels);

    if (! wet.isSmoothing())
    {
        // Settled: all processed needs nothing doing, and all dry is the copy.
        if (! bypassed)
            return;

        for (int ch = 0; ch < channels; ++ch)
            processed.copyFrom (ch, 0, dry, ch, 0, numSamples);

        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // One value per sample for every channel, so the image does not lean while the
        // fade runs.
        const auto amount = wet.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto d = dry.getSample (ch, i);
            processed.setSample (ch, i, d + amount * (processed.getSample (ch, i) - d));
        }
    }
}
