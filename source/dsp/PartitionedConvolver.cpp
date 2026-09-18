#include "PartitionedConvolver.h"

#include <algorithm>

namespace
{
    /** How many running totals the dot product below keeps.

        Sixteen, which is four NEON vectors. Float addition is not associative, so a
        reduction into one total is a dependency chain the compiler is forbidden to
        reorder -- and therefore to vectorise -- without -ffast-math, which this build
        does not use. Four totals is enough to vectorise, but leaves a single vector
        accumulator whose next multiply-add waits on the last one: four cycles of
        latency to do four cycles' worth of work, with three of the four units idle.

        Four independent vectors keep them fed. Measured over a 256-tap head: 61 ns a
        dot product with four totals, 17 ns with sixteen. */
    constexpr int accumulators = 16;

    /** Dot product of two runs of floats. */
    float dotProduct (const float* a, const float* b, int numValues) noexcept
    {
        float sums[accumulators] = {};

        int k = 0;

        for (; k + accumulators <= numValues; k += accumulators)
            for (int j = 0; j < accumulators; ++j)
                sums[j] += a[k + j] * b[k + j];

        // partitionSize is a multiple of the width above, so this never runs. Here so
        // that a change to either does not silently drop values.
        auto remainder = 0.0f;

        for (; k < numValues; ++k)
            remainder += a[k] * b[k];

        // Pairwise, which keeps the summation order fixed and the tree shallow.
        for (int width = accumulators / 2; width > 0; width /= 2)
            for (int j = 0; j < width; ++j)
                sums[j] += sums[j + width];

        return sums[0] + remainder;
    }

    /** acc += a * b, over a run of complex numbers.

        Written out rather than left to std::complex's own operator*, which carries the
        branch the standard requires for recovering an infinity from a NaN intermediate
        -- correct, and unvectorisable. For finite operands the arithmetic below is the
        same operations in the same order, so it agrees bit for bit; over a 257-bin run
        it is 103 ns against 259. */
    void multiplyAccumulate (std::complex<float>* acc, const std::complex<float>* a,
                             const std::complex<float>* b, int count) noexcept
    {
        // Layout-compatible with float[2] by [complex.numbers.general].
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

    // The head takes the first partition; everything past it goes to the tail.
    const auto tailLength = juce::jmax (0, maximumIrLength - partitionSize);
    numPartitions = (tailLength + partitionSize - 1) / partitionSize;

    // Overlap-save: a frame of partitionSize new samples is transformed together with
    // the partitionSize before it, and the second half of the result is the part free
    // of wrap-around.
    fftSize = partitionSize * 2;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));

    channels.clear();
    channels.resize ((size_t) numChannels);

    for (auto& channel : channels)
    {
        // Twice the length, holding each sample in both halves, so the most recent
        // partitionSize samples are always one contiguous run.
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

    // Held for the whole write rather than only the publish at the end, or a second
    // call could fill the pending buffers while the audio thread swapped them out from
    // under it. The audio thread only ever try-locks, so it never waits on this.
    const juce::SpinLock::ScopedLockType sl (swapLock);

    // What this response actually reaches, which is what the audio thread loops over
    // once the walk to it has finished. Anything past it is still written -- as zeros,
    // by tapAt below -- because that is what the old response fades out into.
    const auto reachedTail = juce::jmax (0, ir.getNumSamples() - partitionSize);
    pendingPartitions = juce::jlimit (0, numPartitions,
                                      (reachedTail + partitionSize - 1) / partitionSize);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        // A mono impulse response is used for every channel.
        const auto* taps = ir.getReadPointer (juce::jmin (channel, ir.getNumChannels() - 1));
        const auto numTaps = ir.getNumSamples();

        const auto tapAt = [&] (int index) { return index < numTaps ? taps[index] : 0.0f; };

        // Head, time-reversed so the sliding dot product in process() reads forwards.
        auto* head = headPending.data() + (size_t) (channel * partitionSize);

        for (int i = 0; i < partitionSize; ++i)
            head[i] = tapAt (partitionSize - 1 - i);

        // Tail, starting one partition in: those are the taps the overlap-save
        // engine's own one-frame delay lines up with.
        for (int partition = 0; partition < numPartitions; ++partition)
        {
            std::fill (designTime.begin(), designTime.end(), Complex {});

            // Zero-padded to the full frame so the circular convolution behaves like
            // a linear one.
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

    // [previous frame | this frame], the window overlap-save needs. "This frame" is
    // the ring's first half, which is where convolveHead() has just finished laying
    // down exactly these partitionSize samples in order.
    for (int i = 0; i < partitionSize; ++i)
    {
        timeScratch[(size_t) i] = Complex { channel.previous[(size_t) i], 0.0f };
        timeScratch[(size_t) (partitionSize + i)] = Complex { channel.ring[(size_t) i], 0.0f };
    }

    std::copy (channel.ring.begin(), channel.ring.begin() + partitionSize,
               channel.previous.begin());

    fft->perform (timeScratch.data(), freqScratch.data(), false);

    // Newest spectrum goes in at writeIndex; partition k pairs with the spectrum k
    // frames older, which is what makes this a convolution across frames.
    std::copy (freqScratch.begin(), freqScratch.end(),
               channel.fdl.begin() + (std::ptrdiff_t) writeIndex * fftSize);

    // A response short enough to fit in the head has no tail to sum. The transform
    // above still had to happen -- it is this frame's entry in the history a longer
    // response loaded later reads back through.
    if (activePartitions <= 0)
    {
        std::fill (channel.tailFifo.begin(), channel.tailFifo.end(), 0.0f);
        return;
    }

    // Both operands are transforms of real sequences, so both are conjugate-symmetric
    // and so is their product. Only the lower half is worth multiplying; the rest is
    // filled in by reflection afterwards, which halves the work here.
    const auto half = fftSize / 2;

    std::fill (accumulator.begin(), accumulator.begin() + half + 1, Complex {});

    const auto* filter = tailActive.data() + (size_t) (channelIndex * numPartitions * fftSize);

    for (int partition = 0; partition < activePartitions; ++partition)
    {
        // Modulo the *prepared* count, not the active one: the history is a ring the
        // whole prepared length, and reading it as though it were shorter would pair
        // this frame's spectrum with the wrong one.
        const auto slot = (writeIndex - partition + numPartitions) % numPartitions;
        const auto* history = channel.fdl.data() + (size_t) (slot * fftSize);
        const auto* taps = filter + (size_t) (partition * fftSize);

        multiplyAccumulate (accumulator.data(), history, taps, half + 1);
    }

    for (int bin = 1; bin < half; ++bin)
        accumulator[(size_t) (fftSize - bin)] = std::conj (accumulator[(size_t) bin]);

    fft->perform (accumulator.data(), freqScratch.data(), true);

    // The first half is contaminated by wrap-around; the second half is the answer.
    for (int i = 0; i < partitionSize; ++i)
        channel.tailFifo[(size_t) i] = freqScratch[(size_t) (partitionSize + i)].real();
}

void PartitionedConvolver::pickUpPendingFilter() noexcept
{
    if (! pendingReady.load())
        return;

    // A try-lock, so a message thread mid-update costs nothing but a retry later.
    const juce::SpinLock::ScopedTryLockType sl (swapLock);

    if (! sl.isLocked())
        return;

    if (pendingDiscardsHistory)
    {
        clearHistory();
        pendingDiscardsHistory = false;
    }

    // Whatever the head fade has reached becomes the new starting point, or a swap
    // arriving mid-walk would jump back to where the previous one began.
    settleHeadRamp();

    headTarget.swap (headPending);
    tailTarget.swap (tailPending);
    targetPartitions = pendingPartitions;
    pendingReady.store (false);

    if (applied)
    {
        // The union of old and new for the duration of the walk: the partitions only
        // the outgoing response reaches are still sounding it, and dropping them here
        // rather than when the ramp lands would cut its tail off mid-fade.
        activePartitions = juce::jmax (activePartitions, targetPartitions);
        rampFramesLeft = rampFrames;
        headRampLeft = headRampSamples;
        return;
    }

    // The first filter arrives over silence: nothing to walk away from, and every
    // reason to be exact from the first sample. Swapped rather than copied to keep it
    // off the audio thread's budget.
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

    // The tail only -- the head is crossfaded per sample in convolveHead -- and only as
    // far as either response reaches, since past that both operands are zero.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto first = (size_t) channel * (size_t) numPartitions * (size_t) fftSize;
        const auto last = first + (size_t) activePartitions * (size_t) fftSize;

        for (auto i = first; i < last; ++i)
            tailActive[i] += (tailTarget[i] - tailActive[i]) * step;
    }

    --rampFramesLeft;

    // Landed: everything the outgoing response reached and the incoming one does not
    // has now been walked to silence, so it can stop being multiplied.
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

        // Written to both halves, so the window below is always contiguous.
        ring[position] = input;
        ring[position + partitionSize] = input;

        // The window runs oldest to newest and the taps are stored reversed to match,
        // so a plain dot product is the convolution. This is the part that costs no
        // latency.
        const auto* window = ring + position + 1;

        auto head = dotProduct (window, taps, partitionSize);

        if (crossfading)
        {
            // Twice the arithmetic, over eight frames, in exchange for a filter that
            // changes continuously rather than in eight audible steps.
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

    // Before the first sample, not only at frame boundaries: the head is convolved
    // as the samples arrive, so a filter that has been waiting since the last block
    // has to be in place for this one's first sample rather than its 256th.
    pickUpPendingFilter();

    const auto numSamples = buffer.getNumSamples();
    const auto channelsToDo = juce::jmin (numChannels, buffer.getNumChannels());

    for (int start = 0; start < numSamples;)
    {
        // Runs stop at frame boundaries. Inside one the coefficients cannot change and
        // every channel walks the same positions, which is what lets the channel loop
        // sit outside the sample loop.
        const auto run = juce::jmin (numSamples - start, partitionSize - framePosition);

        // Captured before the channel loop so both channels blend identically.
        const auto rampAtStart = headRampLeft;

        for (int ch = 0; ch < channelsToDo; ++ch)
            convolveHead (ch, buffer.getWritePointer (ch) + start, run, rampAtStart);

        if (headRampLeft > 0)
        {
            headRampLeft = juce::jmax (0, headRampLeft - run);

            // Copied element-wise rather than assigned, so no implementation of vector
            // assignment can decide to reallocate on the audio thread.
            if (headRampLeft == 0)
                std::copy (headTarget.begin(), headTarget.end(), headActive.begin());
        }

        start += run;
        framePosition += run;

        if (framePosition < partitionSize)
            continue;

        // A frame has completed: take any filter that has been waiting, step the walk
        // towards it, and transform the frame the head has just finished laying down.
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
