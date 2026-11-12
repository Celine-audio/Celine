#include "PartitionedConvolver.h"

#include <algorithm>

namespace
{
    /** Number of running totals in the dot product: 16 independent sums (four NEON
        vectors) let the compiler vectorise without -ffast-math and keep the units busy. */
    constexpr int accumulators = 16;

    /** Dot product of two runs of floats. */
    float dotProduct (const float* a, const float* b, int numValues) noexcept
    {
        float sums[accumulators] = {};

        int k = 0;

        for (; k + accumulators <= numValues; k += accumulators)
            for (int j = 0; j < accumulators; ++j)
                sums[j] += a[k + j] * b[k + j];

        // Never runs while partitionSize is a multiple of `accumulators`; kept so a change to
        // either doesn't drop values.
        auto remainder = 0.0f;

        for (; k < numValues; ++k)
            remainder += a[k] * b[k];

        // Pairwise reduction: fixed summation order, shallow tree.
        for (int width = accumulators / 2; width > 0; width /= 2)
            for (int j = 0; j < width; ++j)
                sums[j] += sums[j + width];

        return sums[0] + remainder;
    }

    /** acc += a * b over complex numbers. Written out because std::complex's operator*
        handles NaN/infinity cases and won't vectorise; same result for finite values. */
    void multiplyAccumulate (std::complex<float>* acc, const std::complex<float>* a,
                             const std::complex<float>* b, int count) noexcept
    {
        // Layout-compatible with float[2] ([complex.numbers.general]).
        auto* out = reinterpret_cast<float*> (acc);
        const auto* left = reinterpret_cast<const float*> (a);
        const auto* right = reinterpret_cast<const float*> (b);

        for (int i = 0; i < count; ++i)
        {
            const auto ar = left[i * 2], ai = left[i * 2 + 1];
            const auto br = right[i * 2], bi = right[i * 2 + 1];

            out[i * 2]     += ar * br - ai * bi;
            out[i * 2 + 1] += ar * bi + ai * br;
        }
    }
}

void PartitionedConvolver::prepare (int channelCount, int maximumIrLength)
{
    numChannels = juce::jmax (1, channelCount);

    // The first partition is the head; the rest is the tail.
    const auto tailLength = juce::jmax (0, maximumIrLength - partitionSize);
    numPartitions = (tailLength + partitionSize - 1) / partitionSize;

    // Overlap-save: each frame is transformed with the one before it; the second half of
    // the result is free of wrap-around.
    fftSize = partitionSize * 2;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));

    channels.clear();
    channels.resize ((size_t) numChannels);

    for (auto& channel : channels)
    {
        // Each sample is stored in both halves, so the latest partitionSize samples are
        // contiguous.
        channel.ring.assign ((size_t) partitionSize * 2, 0.0f);
        channel.previous.assign ((size_t) partitionSize, 0.0f);
        channel.tailFifo.assign ((size_t) partitionSize, 0.0f);
        channel.fdl.assign ((size_t) juce::jmax (1, numPartitions) * (size_t) fftSize, Complex {});
    }

    timeScratch.assign ((size_t) fftSize, Complex {});
    freqScratch.assign ((size_t) fftSize, Complex {});
    accumulator.assign ((size_t) fftSize, Complex {});

    designTime.assign ((size_t) fftSize, Complex {});
    designFreq.assign ((size_t) fftSize, Complex {});

    const auto headSize = (size_t) (numChannels * partitionSize);
    headActive.assign (headSize, 0.0f);
    headTarget.assign (headSize, 0.0f);
    headPending.assign (headSize, 0.0f);

    const auto tailSize = (size_t) numChannels * (size_t) numPartitions * (size_t) fftSize;
    tailActive.assign (tailSize, Complex {});
    tailTarget.assign (tailSize, Complex {});
    tailPending.assign (tailSize, Complex {});

    pendingReady.store (false);
    loaded.store (false);

    pendingPartitions = 0;
    pendingDiscardsHistory = false;
    activePartitions = 0;
    targetPartitions = 0;

    rampFramesLeft = 0;
    applied = false;
    writeIndex = 0;
    framePosition = 0;
}

void PartitionedConvolver::clearHistory() noexcept
{
    for (auto& channel : channels)
    {
        std::fill (channel.ring.begin(), channel.ring.end(), 0.0f);
        std::fill (channel.previous.begin(), channel.previous.end(), 0.0f);
        std::fill (channel.tailFifo.begin(), channel.tailFifo.end(), 0.0f);
        std::fill (channel.fdl.begin(), channel.fdl.end(), Complex {});
    }
}

void PartitionedConvolver::reset()
{
    clearHistory();

    writeIndex = 0;
    framePosition = 0;
}

void PartitionedConvolver::setImpulseResponse (const juce::AudioBuffer<float>& ir,
                                               bool discardHistory)
{
    if (fft == nullptr || ir.getNumChannels() <= 0)
        return;

    // Held for the whole write so a second call can't fill the pending buffers while the
    // audio thread swaps them. The audio thread only try-locks.
    const juce::SpinLock::ScopedLockType sl (swapLock);

    // The partitions this response reaches; the rest are written as zeros, which the old
    // response fades out to.
    const auto reachedTail = juce::jmax (0, ir.getNumSamples() - partitionSize);
    pendingPartitions = juce::jlimit (0, numPartitions,
                                      (reachedTail + partitionSize - 1) / partitionSize);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        // A mono response is used for every channel.
        const auto* taps = ir.getReadPointer (juce::jmin (channel, ir.getNumChannels() - 1));
        const auto numTaps = ir.getNumSamples();

        const auto tapAt = [&] (int index) { return index < numTaps ? taps[index] : 0.0f; };

        // Head, time-reversed so process() reads forwards.
        auto* head = headPending.data() + (size_t) (channel * partitionSize);

        for (int i = 0; i < partitionSize; ++i)
            head[i] = tapAt (partitionSize - 1 - i);

        // Tail, starting one partition in, matching the engine's one-frame delay.
        for (int partition = 0; partition < numPartitions; ++partition)
        {
            std::fill (designTime.begin(), designTime.end(), Complex {});

            // Zero-padded to the full frame so the circular convolution is linear.
            for (int i = 0; i < partitionSize; ++i)
                designTime[(size_t) i] = Complex { tapAt (partitionSize + partition * partitionSize + i), 0.0f };

            fft->perform (designTime.data(), designFreq.data(), false);

            auto* dest = tailPending.data()
                       + (size_t) ((channel * numPartitions + partition) * fftSize);
            std::copy (designFreq.begin(), designFreq.end(), dest);
        }
    }

    pendingDiscardsHistory = discardHistory;

    pendingReady.store (true);
    loaded.store (true);
}

void PartitionedConvolver::processFrame (int channelIndex) noexcept
{
    if (numPartitions <= 0)
        return;

    auto& channel = channels[(size_t) channelIndex];

    // [previous frame | this frame]; this frame is the ring's first half, just written by
    // convolveHead().
    for (int i = 0; i < partitionSize; ++i)
    {
        timeScratch[(size_t) i] = Complex { channel.previous[(size_t) i], 0.0f };
        timeScratch[(size_t) (partitionSize + i)] = Complex { channel.ring[(size_t) i], 0.0f };
    }

    std::copy (channel.ring.begin(), channel.ring.begin() + partitionSize,
               channel.previous.begin());

    fft->perform (timeScratch.data(), freqScratch.data(), false);

    // The newest spectrum goes in at writeIndex; partition k pairs with the spectrum k
    // frames older.
    std::copy (freqScratch.begin(), freqScratch.end(),
               channel.fdl.begin() + (std::ptrdiff_t) writeIndex * fftSize);

    // A response that fits in the head has no tail. The transform above is still needed,
    // for the history.
    if (activePartitions <= 0)
    {
        std::fill (channel.tailFifo.begin(), channel.tailFifo.end(), 0.0f);
        return;
    }

    // Real inputs give conjugate-symmetric spectra, so only the lower half is multiplied;
    // the rest is mirrored afterwards.
    const auto half = fftSize / 2;

    std::fill (accumulator.begin(), accumulator.begin() + half + 1, Complex {});

    const auto* filter = tailActive.data() + (size_t) (channelIndex * numPartitions * fftSize);

    for (int partition = 0; partition < activePartitions; ++partition)
    {
        // Modulo the prepared count: the history ring spans every prepared partition.
        const auto slot = (writeIndex - partition + numPartitions) % numPartitions;
        const auto* history = channel.fdl.data() + (size_t) (slot * fftSize);
        const auto* taps = filter + (size_t) (partition * fftSize);

        multiplyAccumulate (accumulator.data(), history, taps, half + 1);
    }

    for (int bin = 1; bin < half; ++bin)
        accumulator[(size_t) (fftSize - bin)] = std::conj (accumulator[(size_t) bin]);

    fft->perform (accumulator.data(), freqScratch.data(), true);

    // The first half has wrap-around; the second half is the output.
    for (int i = 0; i < partitionSize; ++i)
        channel.tailFifo[(size_t) i] = freqScratch[(size_t) (partitionSize + i)].real();
}

void PartitionedConvolver::pickUpPendingFilter() noexcept
{
    if (! pendingReady.load())
        return;

    // Try-lock: if the message thread is mid-update, pick up the filter later.
    const juce::SpinLock::ScopedTryLockType sl (swapLock);

    if (! sl.isLocked())
        return;

    if (pendingDiscardsHistory)
    {
        clearHistory();
        pendingDiscardsHistory = false;
    }

    // Start from wherever the head fade has reached.
    settleHeadRamp();

    headTarget.swap (headPending);
    tailTarget.swap (tailPending);
    targetPartitions = pendingPartitions;
    pendingReady.store (false);

    if (applied)
    {
        // During the crossfade, cover both the old and new responses' partitions.
        activePartitions = juce::jmax (activePartitions, targetPartitions);
        rampFramesLeft = rampFrames;
        headRampLeft = headRampSamples;
        return;
    }

    // The first filter goes in directly: nothing to fade from.
    headActive.swap (headTarget);
    tailActive.swap (tailTarget);
    activePartitions = targetPartitions;
    rampFramesLeft = 0;
    headRampLeft = 0;
    applied = true;
}

void PartitionedConvolver::settleHeadRamp() noexcept
{
    if (headRampLeft <= 0)
        return;

    const auto reached = 1.0f - (float) headRampLeft / (float) headRampSamples;

    for (size_t i = 0; i < headActive.size(); ++i)
        headActive[i] += (headTarget[i] - headActive[i]) * reached;

    headRampLeft = 0;
}

void PartitionedConvolver::advanceRamp() noexcept
{
    if (rampFramesLeft <= 0)
        return;

    const auto step = 1.0f / (float) rampFramesLeft;

    // Tail only (the head fades per sample in convolveHead), up to the longer of the two
    // responses.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto first = (size_t) channel * (size_t) numPartitions * (size_t) fftSize;
        const auto last = first + (size_t) activePartitions * (size_t) fftSize;

        for (auto i = first; i < last; ++i)
            tailActive[i] += (tailTarget[i] - tailActive[i]) * step;
    }

    --rampFramesLeft;

    // Fade done: drop the partitions only the old response reached.
    if (rampFramesLeft == 0)
        activePartitions = targetPartitions;
}

void PartitionedConvolver::convolveHead (int channelIndex, float* samples, int numSamples,
                                        int rampLeftAtStart) noexcept
{
    auto& channel = channels[(size_t) channelIndex];

    auto* ring = channel.ring.data();
    const auto* tail = channel.tailFifo.data();
    const auto* taps = headActive.data() + (size_t) (channelIndex * partitionSize);
    const auto* incoming = headTarget.data() + (size_t) (channelIndex * partitionSize);

    const auto crossfading = rampLeftAtStart > 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = framePosition + i;
        const auto input = samples[i];

        // Written to both halves, so the window is contiguous.
        ring[position] = input;
        ring[position + partitionSize] = input;

        // The window runs oldest to newest and the taps are reversed, so a dot product is the
        // convolution.
        const auto* window = ring + position + 1;

        auto head = dotProduct (window, taps, partitionSize);

        if (crossfading)
        {
            // During a head fade both filters are convolved and blended per sample.
            const auto left = juce::jmax (0, rampLeftAtStart - i);
            const auto reached = 1.0f - (float) left / (float) headRampSamples;

            head += (dotProduct (window, incoming, partitionSize) - head) * reached;
        }

        samples[i] = head + tail[position];
    }
}

void PartitionedConvolver::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (fft == nullptr || ! loaded.load())
        return;

    // Pick up a waiting filter before the first sample, since the head runs per sample.
    pickUpPendingFilter();

    const auto numSamples = buffer.getNumSamples();
    const auto channelsToDo = juce::jmin (numChannels, buffer.getNumChannels());

    for (int start = 0; start < numSamples;)
    {
        // Runs stop at frame boundaries, where the coefficients may change.
        const auto run = juce::jmin (numSamples - start, partitionSize - framePosition);

        // Captured once so every channel blends the same.
        const auto rampAtStart = headRampLeft;

        for (int ch = 0; ch < channelsToDo; ++ch)
            convolveHead (ch, buffer.getWritePointer (ch) + start, run, rampAtStart);

        if (headRampLeft > 0)
        {
            headRampLeft = juce::jmax (0, headRampLeft - run);

            // Element-wise copy: never reallocates on the audio thread.
            if (headRampLeft == 0)
                std::copy (headTarget.begin(), headTarget.end(), headActive.begin());
        }

        start += run;
        framePosition += run;

        if (framePosition < partitionSize)
            continue;

        // Frame complete: pick up any waiting filter, advance the fade, transform the frame.
        framePosition = 0;

        pickUpPendingFilter();
        advanceRamp();

        if (numPartitions > 0)
        {
            writeIndex = (writeIndex + 1) % numPartitions;

            for (int ch = 0; ch < channelsToDo; ++ch)
                processFrame (ch);
        }
    }
}
