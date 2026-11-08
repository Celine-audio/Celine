#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

/** Plays an audio file into the circuit instead of the host input, for comparing sheets.

    Not saved in the document or the plugin state: every session starts on live input.

    Threads:
    - message thread: requests (load, play, stop, loop, file/live); all return at once;
    - one worker thread, created on first use: decodes and resamples, one job at a time;
    - audio thread: render() copies samples under a try-lock, silence if it is busy.

    Files are resampled once to the host rate; a file already at that rate is untouched. */
class InputFilePlayer
{
public:
    /** Longest file accepted, in seconds; longer files are truncated. */
    static constexpr double maximumSeconds = 300.0;

    InputFilePlayer();
    ~InputFilePlayer();

    // ==========================================================================
    // Message thread

    /** Decodes `file` in the background and, if it reads, replaces the current file
        (stopped, at the start). On failure the previous file stays and getStatus() says why.

        `switchToFileWhenLoaded` turns file input on once the file has loaded. A
        setFileMode() before then cancels it. */
    void load (const juce::File& file, bool switchToFileWhenLoaded = false);

    /** Sets the rate render() runs at. A loaded file at another rate is dropped and
        resampled again in the background. Call from prepareToPlay. */
    void prepare (double sampleRate);

    /** File or live input. Switching to live stops the file. */
    void setFileMode (bool shouldPlayFile) noexcept;
    bool isFileMode() const noexcept { return fileMode.load(); }

    void setLooping (bool shouldLoop) noexcept { looping.store (shouldLoop); }
    bool isLooping() const noexcept { return looping.load(); }

    /** Starts the file (from the top if it had ended) and switches to file input. */
    void play() noexcept;

    /** Stops and rewinds. */
    void stop() noexcept;

    bool isPlaying() const noexcept { return playing.load(); }

    /** Everything the inspector shows. */
    struct Status
    {
        juce::File file;              ///< What is loaded, or none.
        juce::String problem;         ///< Why the last load failed, or empty.
        juce::int64 position = 0;     ///< In samples at `sampleRate`.
        juce::int64 length = 0;       ///< Zero when nothing is loaded.
        double sampleRate = 0.0;      ///< The rate the loaded samples are at.
        double sourceRate = 0.0;      ///< The rate the file was at.
        bool truncated = false;       ///< The file was longer than maximumSeconds.
        bool loading = false;
        bool playing = false;
        bool fileMode = false;
        bool looping = false;

        /** Counts finished loads, successful or not, so a caller can spot a new result. */
        int loadsFinished = 0;
    };

    Status getStatus() const;

    /** Blocks until no load is pending, or the timeout passes. For tests and benchmarks. */
    bool waitUntilLoaded (int timeoutMilliseconds) const;

    // ==========================================================================
    // Audio thread

    /** In file mode, writes the file (or silence when stopped, finished or loading) over
        `channels` and returns true. In live mode, does nothing and returns false. A mono
        file goes to every channel; stereo into one channel is averaged. Never allocates
        or waits. */
    bool render (float* const* channels, int numChannels, int numSamples) noexcept;

    //==========================================================================
    /** Kaiser-windowed sinc resampling, 64 zero crossings, cutoff at 95% of the lower
        Nyquist. `cancel`, if given, is polled; the result is empty if cancelled. */
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source,
                                              double sourceRate, double destinationRate,
                                              const std::atomic<bool>* cancel = nullptr);

private:
    /** The worker's job: bring the installed samples in line with the latest requested
        file and rate. The last request wins. */
    void bringUpToDate();

    void startJob();

    /** Swaps `fresh` in, stopped at the start, and returns the old buffer to free outside
        the lock. Call with `stateLock` held. */
    std::unique_ptr<juce::AudioBuffer<float>> install (std::unique_ptr<juce::AudioBuffer<float>> fresh);

    // ==========================================================================
    // Audio thread side.

    juce::SpinLock bufferLock;
    std::unique_ptr<juce::AudioBuffer<float>> current;

    /** Written only by the audio thread, or under `bufferLock`. */
    std::atomic<juce::int64> position { 0 };

    /** Lets the message thread rewind without writing `position`. */
    std::atomic<bool> rewindPending { false };

    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<bool> fileMode { false };
    std::atomic<juce::int64> length { 0 };

    // ==========================================================================
    // Everything else, guarded by `stateLock` (never taken by the audio thread).

    juce::CriticalSection stateLock;

    juce::File wantedFile, installedFile;
    double wantedRate = 44100.0, installedRate = 0.0, installedSourceRate = 0.0;
    bool installedTruncated = false;
    juce::String problem;
    int loadsFinished = 0;

    /** Jobs queued or running. */
    std::atomic<int> jobsPending { 0 };

    std::atomic<bool> cancelled { false };

    /** Set by load(); see there. */
    std::atomic<bool> switchToFileOnLoad { false };

    /** Created on first use. Declared last so it is destroyed first. */
    std::unique_ptr<juce::ThreadPool> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputFilePlayer)
};
