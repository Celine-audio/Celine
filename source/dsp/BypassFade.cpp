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

    delayFadeLength = juce::jmax (1, juce::roundToInt (fadeSeconds * sampleRate));

    wet.reset (sampleRate, fadeSeconds);
}

void BypassFade::reset (bool bypassed)
{
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    currentDelay = -1;
    delayFadeRemaining = 0;
    wet.setCurrentAndTargetValue (bypassed ? 0.0f : 1.0f);
}

void BypassFade::pushDry (const juce::AudioBuffer<float>& input, int latencySamples) noexcept
{
    const auto numSamples = juce::jmin (input.getNumSamples(), dry.getNumSamples());
    const auto delay = juce::jlimit (0, mask, latencySamples);

    if (currentDelay < 0)
    {
        currentDelay = delay;
    }
    else if (delay != currentDelay)
    {
        // A second change landing inside a fade restarts it from the delay it was
        // heading for. That is a small step rather than none, but it takes two factor
        // changes within fadeSeconds, which the menu cannot produce.
        previousDelay = currentDelay;
        currentDelay = delay;
        delayFadeRemaining = delayFadeLength;
    }

    dryChannels = juce::jmin (input.getNumChannels(), dry.getNumChannels());

    for (int ch = 0; ch < dryChannels; ++ch)
    {
        auto& line = delayLines[(size_t) ch];
        const auto* in = input.getReadPointer (ch);
        auto* out = dry.getWritePointer (ch);
        auto write = writeIndex;

        // Every channel runs the same fade from the same point, so the image holds.
        auto remaining = delayFadeRemaining;

        for (int i = 0; i < numSamples; ++i)
        {
            line[(size_t) write] = in[i];
            const auto now = line[(size_t) ((write - currentDelay) & mask)];

            if (remaining > 0)
            {
                const auto before = line[(size_t) ((write - previousDelay) & mask)];
                const auto oldWeight = (float) remaining / (float) delayFadeLength;
                out[i] = now + oldWeight * (before - now);
                --remaining;
            }
            else
            {
                out[i] = now;
            }

            write = (write + 1) & mask;
        }
    }

    writeIndex = (writeIndex + numSamples) & mask;
    delayFadeRemaining = juce::jmax (0, delayFadeRemaining - numSamples);
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

void BypassFade::substituteDry (juce::AudioBuffer<float>& processed, bool bypassed) noexcept
{
    wet.setTargetValue (bypassed ? 0.0f : 1.0f);

    const auto numSamples = juce::jmin (processed.getNumSamples(), dry.getNumSamples());
    const auto channels = juce::jmin (processed.getNumChannels(), dryChannels);

    wet.skip (numSamples);

    for (int ch = 0; ch < channels; ++ch)
        processed.copyFrom (ch, 0, dry, ch, 0, numSamples);
}
