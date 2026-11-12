#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/** Bypass as a short crossfade between the processed signal and a copy of the dry
    input delayed by the plugin's latency, so toggling it never clicks.

    The processing keeps running while bypassed; this only decides how much of it is
    heard. Audio thread only, except prepare() and reset(). */
class BypassFade
{
public:
    /** Crossfade length. */
    static constexpr double fadeSeconds = 0.03;

    /** maxDelaySamples is the largest latency pushDry() will be asked to match. Blocks
        must not exceed maxBlockSize. */
    void prepare (int numChannels, int maxBlockSize, int maxDelaySamples, double sampleRate);

    /** Clears the delay and jumps straight to `bypassed`, without fading. */
    void reset (bool bypassed);

    int getMaxBlockSize() const noexcept { return dry.getNumSamples(); }

    /** Stores the input, delayed by `latencySamples` to line up with the processed output.
        A change of latency is crossfaded over fadeSeconds rather than jumped. */
    void pushDry (const juce::AudioBuffer<float>& input, int latencySamples) noexcept;

    /** Blends `processed` with the delayed dry copy, towards dry when `bypassed`. */
    void mix (juce::AudioBuffer<float>& processed, bool bypassed) noexcept;

    /** Writes the delayed dry copy over `processed`, for a block that could not be
        processed, and advances the fade as mix() would. */
    void substituteDry (juce::AudioBuffer<float>& processed, bool bypassed) noexcept;

private:
    juce::AudioBuffer<float> dry;
    std::vector<std::vector<float>> delayLines;
    int mask = 0;
    int writeIndex = 0;
    int dryChannels = 0;

    // The delay being read, and the one being faded away from while it changes.
    // Negative: none set since reset().
    int currentDelay = -1;
    int previousDelay = 0;
    int delayFadeLength = 1;
    int delayFadeRemaining = 0;

    // 1 is all processed, 0 all dry.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wet;
};
