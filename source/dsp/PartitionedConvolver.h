#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

/**
    FIR convolution with no latency of its own, whose filter can be replaced while
    audio is running without discarding the input it has already heard.

    Both of those matter here. juce::dsp::Convolution builds a fresh engine for every
    loadImpulseResponse(), and a fresh engine has an empty input history — so for the
    first impulse-response length of samples its output is missing everything that
    came before the swap. Crossfading into that is audible as a click, and it happens
    even when the response handed over is identical to the one already loaded. A
    matching EQ reshapes its curve from five different knobs, so it reloads
    constantly, and every reload clicked.

    The response is split in two. The first partition is convolved directly, sample by
    sample, which costs nothing in latency. The rest goes through a uniformly-
    partitioned overlap-save engine, which is inherently one partition behind — which
    is exactly the delay those taps needed anyway. Summed, the two reproduce the whole
    response with the first tap landing on the same sample that produced it.

    The input history lives in a frequency-domain delay line and a sample ring that
    the filter swap never touches; only the coefficients are exchanged, at a frame
    boundary, under a try-lock the audio thread will never wait on. And they are
    walked to rather than jumped to: convolution is linear and both filters share one
    history, so blending the coefficients is exactly a crossfade of the two outputs.
*/
class PartitionedConvolver
{
public:
    /** Frame size, and the length of the directly-convolved head. */
    static constexpr int partitionSize = 256;

    PartitionedConvolver() = default;

    /** Sizes everything for the given channel count and the longest response that
        will ever be handed over. Allocates; call it from prepareToPlay and never from
        the audio thread.

        A shorter response than the maximum is not padded out to it: see
        `activePartitions`, which is what keeps the cost proportional to the response
        actually loaded rather than to the room left for one. */
    void prepare (int numChannels, int maximumIrLength);

    /** Clears the input history and any part-finished output. Keeps the filter. */
    void reset();

    /** Message thread: hands over a new filter. One channel of the buffer per channel
        prepared; a mono buffer is used for every channel. Takes effect at the next
        frame boundary, walked to over roughly 40 ms.

        `discardHistory` throws away the input the engine has heard, and the caller has
        to know which it wants. Keeping the history is what makes an ordinary swap
        seamless -- the new filter carries on from the same past the old one was reading.
        Discarding it is for an engine that has been *idle*: one that stopped being
        called holds whatever was playing when it stopped, and a response loaded on top
        of that convolves it. Measured at -25 dBFS of the past, arriving on a signal that
        is now silent.

        The discard is done by the audio thread when it picks the filter up, not here:
        the history is the one thing in this class that thread owns outright. */
    void setImpulseResponse (const juce::AudioBuffer<float>& ir, bool discardHistory);

    /** Audio thread: filters in place. A no-op until a filter has been set. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Zero. The response's own group delay is the caller's to report. */
    int getLatencySamples() const noexcept { return 0; }

private:
    using Complex = juce::dsp::Complex<float>;

    void processFrame (int channel) noexcept;
    void convolveHead (int channel, float* samples, int numSamples, int rampLeftAtStart) noexcept;
    void pickUpPendingFilter() noexcept;
    void advanceRamp() noexcept;

    /** Audio thread: zeros everything the engine has heard. The ring and frame
        positions are left where they are -- zeroed history reads the same at any
        position -- so this is safe part way through a block. */
    void clearHistory() noexcept;

    /** Writes wherever the head crossfade has reached back into the active filter, so
        that a second swap arriving mid-walk starts from what is being heard rather than
        from what was being heard when the first one began. */
    void settleHeadRamp() noexcept;

    struct Channel
    {
        // The frame being filled, held twice over so that the last partitionSize
        // samples are always one contiguous run whatever the position in the frame.
        // Its first half is also the completed frame that processFrame() transforms:
        // there is no separate copy of that, because the ring already is one.
        std::vector<float> ring;        // 2 * partitionSize
        std::vector<float> previous;    // the frame before it
        std::vector<float> tailFifo;    // the frame of tail output being drained
        std::vector<Complex> fdl;       // numPartitions spectra of fftSize
    };

    std::unique_ptr<juce::dsp::FFT> fft;

    int fftSize = 0;
    int numPartitions = 0;
    int numChannels = 0;

    // How many of the prepared partitions actually carry taps. GALLERY prepares for the
    // longest response it will accept and then loads cabinets a twentieth of that, and
    // the tail loop is the whole cost of the engine -- so multiplying zeros for the
    // other nineteen twentieths, four slots over, is most of a core spent on nothing.
    //
    // Two counts, not one, because a filter is walked to rather than jumped to: while
    // the ramp runs, partitions the new response does not reach are still fading the
    // old one out, so the loop has to cover the union of the two. It drops to the new
    // count when the walk finishes -- and setImpulseResponse zero-fills everything past
    // the new response, so what those partitions fade towards is silence.
    int activePartitions = 0;
    int targetPartitions = 0;

    std::vector<Channel> channels;

    // Audio thread only, so process() never allocates.
    std::vector<Complex> timeScratch, freqScratch, accumulator;

    // Message thread only: setImpulseResponse transforms one partition at a time
    // through these rather than allocating a pair of frames per call.
    std::vector<Complex> designTime, designFreq;

    // The directly-convolved head, numChannels * partitionSize, stored time-reversed
    // so the sliding dot product runs forwards over contiguous memory.
    std::vector<float> headActive, headTarget, headPending;

    // The partitioned tail, numChannels * numPartitions * fftSize.
    std::vector<Complex> tailActive, tailTarget, tailPending;

    // Frames left in the walk from active to target. Roughly 40 ms: long enough that a
    // step in the curve is inaudible, short enough to finish inside the throttle that
    // paces rebuilds.
    static constexpr int rampFrames = 8;
    int rampFramesLeft = 0;

    /** The head is crossfaded sample by sample, where the tail is stepped frame by
        frame, and the difference matters more than it looks.

        Interpolating coefficients only at frame boundaries changes the filter in eight
        jumps of an eighth each. Between two similar responses that is inaudible; between
        a cabinet and silence, or across the shift an alignment drag makes, each jump is
        a step in the output every 256 samples -- which is what a filter swap sounded
        like. The head carries the transient and most of the energy, so crossfading it
        properly is what removes the artefact; the tail is the quiet remainder and a step
        in it does not carry. */
    static constexpr int headRampSamples = rampFrames * partitionSize;
    int headRampLeft = 0;

    // Audio thread only: whether the active coefficients are a real filter yet.
    bool applied = false;

    juce::SpinLock swapLock;
    std::atomic<bool> pendingReady { false };
    std::atomic<bool> loaded { false };

    /** Partitions the response waiting in `tailPending` reaches, and whether picking it
        up should throw the input history away with it. Both written under swapLock
        beside the coefficients they describe. */
    int pendingPartitions = 0;
    bool pendingDiscardsHistory = false;

    int writeIndex = 0;

    // How far into the current frame we are, 0 to partitionSize - 1. This was two
    // separate counters, one for the ring and one for the frame; they were incremented
    // together from the same start and so could never differ.
    int framePosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartitionedConvolver)
};
