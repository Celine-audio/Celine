#include "InputFilePlayer.h"

#include <cmath>
#include <vector>

namespace
{
    constexpr int zeroCrossings = 64;

    /** Table resolution, in steps per zero crossing. Read with linear
        interpolation, whose error is under h''/8 per step squared -- about
        4e-7 of full scale here, below the filter's own -90 dB stopband. */
    constexpr int tableSteps = 1024;

    /** 0.1102 (A - 8.7) for A = 90 dB, Kaiser's own rule. */
    constexpr double kaiserBeta = 9.0;

    /** Where the cutoff sits, against the lower Nyquist. The transition band
        is centred on it, so this is what keeps the band that does not get
        through from folding back under the one that does. */
    constexpr double rolloff = 0.95;

    double besselI0 (double x)
    {
        // The power series converges for every x; at β = 9 it takes about 25
        // terms to stop moving in double precision.
        double sum = 1.0, term = 1.0;
        const double quarterSquare = 0.25 * x * x;

        for (int k = 1; k < 200; ++k)
        {
            term *= quarterSquare / (double (k) * double (k));
            sum += term;

            if (term < sum * 1.0e-17)
                break;
        }

        return sum;
    }

    /** The windowed sinc from 0 to `zeroCrossings`, plus one guard entry so
        the interpolation at the last step has a neighbour. Built once per
        process, on first use, by whichever worker gets there first. */
    const std::vector<double>& kernelTable()
    {
        static const std::vector<double> table = []
        {
            std::vector<double> t (static_cast<size_t> (zeroCrossings * tableSteps + 2), 0.0);
            const double norm = 1.0 / besselI0 (kaiserBeta);

            for (int i = 0; i <= zeroCrossings * tableSteps; ++i)
            {
                const double x = double (i) / tableSteps;
                const double r = x / zeroCrossings;
                const double sinc = i == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x)
                                                     / (juce::MathConstants<double>::pi * x);
                const double window = besselI0 (kaiserBeta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) * norm;

                t[static_cast<size_t> (i)] = sinc * window;
            }

            return t;
        }();

        return table;
    }
}

//==============================================================================
juce::AudioBuffer<float> InputFilePlayer::resample (const juce::AudioBuffer<float>& source,
                                                    double sourceRate, double destinationRate,
                                                    const std::atomic<bool>* cancel)
{
    if (sourceRate <= 0.0 || destinationRate <= 0.0
        || juce::exactlyEqual (sourceRate, destinationRate) || source.getNumSamples() == 0)
        return source;

    const auto& table = kernelTable();
    const auto inLength = source.getNumSamples();
    const auto outLength = juce::jmax (1, (int) std::llround ((double) inLength * destinationRate / sourceRate));

    // Source samples per output sample, and the cutoff in source samples' terms:
    // the source's own Nyquist when going up, the destination's when coming down.
    const double step = sourceRate / destinationRate;
    const double scale = juce::jmin (1.0, destinationRate / sourceRate) * rolloff;
    const double reach = zeroCrossings / scale;
    const double tableScale = scale * tableSteps;

    const auto channels = source.getNumChannels();
    juce::AudioBuffer<float> result (channels, outLength);

    // The weights depend only on where the output sample falls between the
    // input's, so they are worked out once and used for every channel -- half
    // the cost of a stereo file, against doing it per channel.
    std::vector<double> weights (static_cast<size_t> (2.0 * reach + 3.0));

    for (int n = 0; n < outLength; ++n)
    {
        if ((n & 4095) == 0 && cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return {};

        const double t = n * step;
        const int first = juce::jmax (0, (int) std::ceil (t - reach));
        const int last = juce::jmin (inLength - 1, (int) std::floor (t + reach));
        const int taps = juce::jmax (0, last - first + 1);

        jassert ((size_t) taps <= weights.size());

        for (int j = 0; j < taps; ++j)
        {
            const double position = std::abs (t - (first + j)) * tableScale;
            const auto index = (size_t) position;

            // At the very edge of the reach, rounding can land one step past the
            // table; the kernel is zero there anyway.
            if (index + 1 >= table.size())
            {
                weights[(size_t) j] = 0.0;
                continue;
            }

            const double fraction = position - (double) index;
            weights[(size_t) j] = (table[index] + (table[index + 1] - table[index]) * fraction) * scale;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* in = source.getReadPointer (channel) + first;
            double sum = 0.0;

            for (int j = 0; j < taps; ++j)
                sum += in[j] * weights[(size_t) j];

            result.setSample (channel, n, (float) sum);
        }
    }

    return result;
}

//==============================================================================
InputFilePlayer::InputFilePlayer() = default;

InputFilePlayer::~InputFilePlayer()
{
    // The job polls this, so a five-minute resample in flight gives up within a
    // few thousand samples instead of holding the plugin's destruction hostage.
    cancelled.store (true);
    worker.reset();
}

void InputFilePlayer::startJob()
{
    if (worker == nullptr)
        worker = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}
                                                         .withThreadName ("Celine file input")
                                                         .withNumberOfThreads (1));

    ++jobsPending;
    worker->addJob ([this]
    {
        bringUpToDate();
        --jobsPending;
    });
}

void InputFilePlayer::load (const juce::File& file)
{
    {
        const juce::ScopedLock lock (stateLock);
        wantedFile = file;
    }

    startJob();
}

void InputFilePlayer::prepare (double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    bool dropInstalled = false, jobNeeded = false;

    {
        const juce::ScopedLock lock (stateLock);
        wantedRate = sampleRate;

        // Dropped now rather than when the replacement is ready. The audio
        // thread is stopped while this runs, so this is the one moment the
        // buffer can go without anybody hearing it played at the wrong pitch.
        dropInstalled = installedRate > 0.0 && ! juce::exactlyEqual (installedRate, sampleRate);

        if (dropInstalled)
            installedRate = 0.0;

        // Also when nothing is installed yet: a load already running at the old
        // rate will see the rate move under it and decline to install, so
        // something has to come along behind it.
        jobNeeded = wantedFile != juce::File{} && ! juce::exactlyEqual (installedRate, sampleRate);
    }

    if (dropInstalled)
    {
        // Freed here, outside the spin lock -- see install().
        auto old = install (nullptr);
        old.reset();
    }

    if (jobNeeded)
        startJob();
}

std::unique_ptr<juce::AudioBuffer<float>> InputFilePlayer::install (std::unique_ptr<juce::AudioBuffer<float>> fresh)
{
    const auto samples = fresh != nullptr ? (juce::int64) fresh->getNumSamples() : 0;

    {
        // Held for a swap and four stores. The audio thread only ever tries for
        // it, so the worst this costs is one block of silence on the file.
        const juce::SpinLock::ScopedLockType lock (bufferLock);
        std::swap (current, fresh);
        position.store (0);
        length.store (samples);
        rewindPending.store (false);
        playing.store (false);
    }

    return fresh;
}

void InputFilePlayer::bringUpToDate()
{
    juce::File file;
    double rate = 0.0;

    {
        const juce::ScopedLock lock (stateLock);

        if (wantedFile == juce::File{}
            || (wantedFile == installedFile && juce::exactlyEqual (wantedRate, installedRate)))
            return;

        file = wantedFile;
        rate = wantedRate;
    }

    // The answer is built here, on the worker, and only the result is handed
    // over -- so a failure leaves the previous file playing as if nothing had
    // been asked.
    auto fail = [&] (const juce::String& why)
    {
        const juce::ScopedLock lock (stateLock);
        problem = why;
        ++loadsFinished;

        // Back to what is actually loaded, so a later prepare() does not go
        // looking for the file that failed.
        if (wantedFile == file)
            wantedFile = installedRate > 0.0 ? installedFile : juce::File{};
    };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        fail (file.existsAsFile() ? file.getFileName() + " is not an audio file this build can read."
                                  : "Input file not found: " + file.getFullPathName());
        return;
    }

    if (reader->sampleRate <= 0.0 || reader->numChannels < 1 || reader->lengthInSamples < 1)
    {
        fail (file.getFileName() + " has no audio in it.");
        return;
    }

    const auto cap = (juce::int64) std::floor (maximumSeconds * reader->sampleRate);
    const auto frames = juce::jmin (reader->lengthInSamples, cap);
    const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);

    juce::AudioBuffer<float> raw (channels, (int) frames);
    reader->read (&raw, 0, (int) frames, 0, true, channels > 1);

    auto resampled = resample (raw, reader->sampleRate, rate, &cancelled);

    if (cancelled.load())
        return;

    raw.setSize (0, 0);

    // Asked again before installing: if a different file or rate was asked for
    // while this one decoded, the job queued behind this one will do it, and
    // installing this would only be heard as a blip of the wrong thing.
    {
        const juce::ScopedLock lock (stateLock);

        if (wantedFile != file || ! juce::exactlyEqual (wantedRate, rate))
            return;
    }

    auto old = install (std::make_unique<juce::AudioBuffer<float>> (std::move (resampled)));
    old.reset();

    const juce::ScopedLock lock (stateLock);
    installedFile = file;
    installedRate = rate;
    installedSourceRate = reader->sampleRate;
    installedTruncated = reader->lengthInSamples > cap;
    problem.clear();
    ++loadsFinished;
}

bool InputFilePlayer::waitUntilLoaded (int timeoutMilliseconds) const
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMilliseconds;

    while (jobsPending.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;

        juce::Thread::sleep (2);
    }

    return true;
}

//==============================================================================
void InputFilePlayer::setFileMode (bool shouldPlayFile) noexcept
{
    if (! shouldPlayFile)
        stop();

    fileMode.store (shouldPlayFile);
}

void InputFilePlayer::play() noexcept
{
    // A file that has played to its end starts again from the top. The rewind is
    // stored before `playing`, and the audio thread reads them the other way
    // round, so a block that sees the one sees the other.
    if (length.load() > 0 && position.load() >= length.load())
        rewindPending.store (true);

    fileMode.store (true);
    playing.store (true);
}

void InputFilePlayer::stop() noexcept
{
    playing.store (false);
    rewindPending.store (true);
}

InputFilePlayer::Status InputFilePlayer::getStatus() const
{
    Status status;
    status.position = position.load();
    status.length = length.load();
    status.playing = playing.load();
    status.fileMode = fileMode.load();
    status.looping = looping.load();
    status.loading = jobsPending.load() > 0;

    const juce::ScopedLock lock (stateLock);
    status.file = installedFile;
    status.problem = problem;
    status.sampleRate = installedRate;
    status.sourceRate = installedSourceRate;
    status.truncated = installedTruncated;
    status.loadsFinished = loadsFinished;
    return status;
}

//==============================================================================
bool InputFilePlayer::render (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! fileMode.load (std::memory_order_relaxed))
        return false;

    auto silenceFrom = [&] (int start)
    {
        for (int c = 0; c < numChannels; ++c)
            if (channels[c] != nullptr && start < numSamples)
                juce::FloatVectorOperations::clear (channels[c] + start, numSamples - start);
    };

    const juce::SpinLock::ScopedTryLockType lock (bufferLock);

    if (! lock.isLocked() || current == nullptr)
    {
        silenceFrom (0);
        return true;
    }

    // `playing` first, then the rewind -- see play().
    const bool isPlaying = playing.load();
    auto at = position.load (std::memory_order_relaxed);

    if (rewindPending.exchange (false))
        at = 0;

    const auto total = (juce::int64) current->getNumSamples();
    const auto fileChannels = current->getNumChannels();
    int written = 0;

    if (isPlaying)
    {
        while (written < numSamples)
        {
            if (at >= total)
            {
                if (! looping.load (std::memory_order_relaxed))
                    break;

                at = 0;
            }

            const auto count = (int) juce::jmin ((juce::int64) (numSamples - written), total - at);

            for (int c = 0; c < numChannels; ++c)
            {
                if (channels[c] == nullptr)
                    continue;

                auto* destination = channels[c] + written;

                if (fileChannels == 1)
                {
                    juce::FloatVectorOperations::copy (destination, current->getReadPointer (0, (int) at), count);
                }
                else if (numChannels == 1)
                {
                    juce::FloatVectorOperations::copyWithMultiply (destination, current->getReadPointer (0, (int) at), 0.5f, count);
                    juce::FloatVectorOperations::addWithMultiply (destination, current->getReadPointer (1, (int) at), 0.5f, count);
                }
                else
                {
                    juce::FloatVectorOperations::copy (destination,
                                                       current->getReadPointer (juce::jmin (c, fileChannels - 1), (int) at),
                                                       count);
                }
            }

            at += count;
            written += count;
        }

        // Finished, said now rather than on the next block, so the Play button
        // goes out on the block the file did.
        if (at >= total && ! looping.load (std::memory_order_relaxed))
            playing.store (false);
    }

    silenceFrom (written);
    position.store (at, std::memory_order_relaxed);
    return true;
}
