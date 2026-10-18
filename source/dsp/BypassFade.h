#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/**
    Bypass as a crossfade rather than a switch, against a dry path delayed to match the
    plugin's latency. AURA's, and every plugin in the house bypasses through one.

    A switch pops, and the house plugins found three reasons why, each of which had to
    go:

      - Processing that is not fed while bypassed resumes from stale state. A
        convolution, a filter or a delay line coming back in carries on from the input it
        last heard -- in AURA, a third of a second old, through up to 24 dB of boost, and
        out came a burst ten times the level of the signal. So the processor runs its DSP
        whatever the bypass says, and this decides only how much of it is heard.
      - Wet and dry swapped in one sample. The two are never in step -- a filter shifts
        the phase of everything it touches even where it leaves the level alone -- so the
        swap is a step in the waveform.
      - Latency. The host compensates for what the plugin reports whether it is bypassed
        or not, so a dry path that is not delayed by the same amount arrives early, and
        every toggle jumps by the latency.

    So this keeps a copy of the dry input delayed by the reported latency, and blends the
    two over fadeSeconds. Equal-gain, because the two are the same material and add
    coherently.

    Everything here is the audio thread's, except prepare() and reset().
*/
class BypassFade
{
public:
    /** Long enough that the blend is not heard as a click, short enough that pressing
        bypass still feels like pressing a switch. The same in every plugin, so the same
        button feels the same everywhere. */
    static constexpr double fadeSeconds = 0.03;

    /** maxDelaySamples bounds the latency pushDry() may be asked to match. Blocks larger
        than maxBlockSize are not accepted -- the processor hands them over in pieces. */
    void prepare (int numChannels, int maxBlockSize, int maxDelaySamples, double sampleRate);

    /** Clears the delay and lands the blend on `bypassed` at once, with no fade: for a
        fresh start, where there is nothing to fade from. */
    void reset (bool bypassed);

    int getMaxBlockSize() const noexcept { return dry.getNumSamples(); }

    /** Takes a copy of the input before it is processed, delayed by `latencySamples` so
        it lines up with what the processing will give back. */
    void pushDry (const juce::AudioBuffer<float>& input, int latencySamples) noexcept;

    /** Blends `processed` -- the same samples pushDry() was last given, after everything
        the plugin does to them, output trim included -- with that dry copy, moving
        towards all dry when `bypassed` and all processed when not. */
    void mix (juce::AudioBuffer<float>& processed, bool bypassed) noexcept;

private:
    juce::AudioBuffer<float> dry;
    std::vector<std::vector<float>> delayLines;
    int mask = 0;
    int writeIndex = 0;
    int dryChannels = 0;

    // 1 is all processed, 0 all dry.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wet;
};
