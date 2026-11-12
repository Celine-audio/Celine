#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

/** Zero-latency FIR convolution whose filter can be swapped while running without
    losing the input history.

    The first partition is convolved directly; the rest goes through a uniformly
    partitioned overlap-save engine, which is one partition late, exactly where those
    taps belong. A new filter only replaces the coefficients, at a frame boundary, and
    is crossfaded in. */
class PartitionedConvolver
{
public:
    /** Frame size, and the length of the directly-convolved head. */
    static constexpr int partitionSize = 256;

    PartitionedConvolver() = default;

    /** Allocates for the channel count and the longest response that will be set. Call
        from prepareToPlay. Shorter responses only cost what they use (activePartitions). */
    void prepare (int numChannels, int maximumIrLength);

    /** Clears the input history and any part-finished output. Keeps the filter. */
    void reset();

    /** Message thread: sets a new filter, one buffer channel per prepared channel (mono is
        shared). Takes effect at the next frame boundary, crossfaded over about 40 ms.

        `discardHistory` clears the input history when the audio thread picks the filter
        up. Use it when the engine has been idle, so stale input is not convolved. */
    void setImpulseResponse (const juce::AudioBuffer<float>& ir, bool discardHistory);

    /** Audio thread: filters in place. A no-op until a filter has been set. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Zero; the response's own delay is the caller's to report. */
    int getLatencySamples() const noexcept { return 0; }

private:
    using Complex = juce::dsp::Complex<float>;

    void processFrame (int channel) noexcept;
    void convolveHead (int channel, float* samples, int numSamples, int rampLeftAtStart) noexcept;
    void pickUpPendingFilter() noexcept;
    void advanceRamp() noexcept;

    /** Audio thread: zeros the input history. Safe mid-block. */
    void clearHistory() noexcept;

    /** Writes the current point of the head crossfade into the active filter, so a second
        swap starts from what is being heard. */
    void settleHeadRamp() noexcept;

    struct Channel
    {
        // The frame being filled, stored twice so the last partitionSize samples are always
        // contiguous. Its first half is the completed frame processFrame() transforms.
        std::vector<float> ring;
        std::vector<float> previous;    // the frame before it
        std::vector<float> tailFifo;    // the frame of tail output being drained
        std::vector<Complex> fdl;       // numPartitions spectra of fftSize
    };

    std::unique_ptr<juce::dsp::FFT> fft;

    int fftSize = 0;
    int numPartitions = 0;
    int numChannels = 0;

    // Partitions that carry taps, so short responses don't pay for the full length.
    // While a crossfade runs, the loop covers the union of the old and new counts.
    int activePartitions = 0;
    int targetPartitions = 0;

    std::vector<Channel> channels;

    // Audio thread only.
    std::vector<Complex> timeScratch, freqScratch, accumulator;

    // Message thread only: scratch for setImpulseResponse.
    std::vector<Complex> designTime, designFreq;

    // The directly convolved head, numChannels * partitionSize, time-reversed so the dot
    // product runs forwards.
    std::vector<float> headActive, headTarget, headPending;

    // The partitioned tail, numChannels * numPartitions * fftSize.
    std::vector<Complex> tailActive, tailTarget, tailPending;

    // Frames in the crossfade between active and target (about 40 ms).
    static constexpr int rampFrames = 8;
    int rampFramesLeft = 0;

    /** The head is crossfaded per sample, the tail per frame: stepping the head would
        click every 256 samples. */
    static constexpr int headRampSamples = rampFrames * partitionSize;
    int headRampLeft = 0;

    // Audio thread only: whether a real filter is active yet.
    bool applied = false;

    juce::SpinLock swapLock;
    std::atomic<bool> pendingReady { false };
    std::atomic<bool> loaded { false };

    /** Partitions reached by the pending response, and whether to drop the history when it
        is picked up. Written under swapLock. */
    int pendingPartitions = 0;
    bool pendingDiscardsHistory = false;

    int writeIndex = 0;

    // Position in the current frame, 0 to partitionSize - 1.
    int framePosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartitionedConvolver)
};
