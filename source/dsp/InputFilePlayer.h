#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

/**
    An audio file played into the circuit in place of the host's input -- for
    benching, where the same take through two versions of a sheet is the only
    comparison worth making by ear.

    Deliberately **not part of the document**. A file on one machine is a path
    that does not exist on the next, and a sheet that started playing something
    when it was opened would be a preset with a side effect. So nothing here
    reaches createDocument() or the host's state: the player lives as long as
    the plugin instance does, survives the editor closing, and starts every
    session empty and on live input.

    Three threads, and the split between them is the design:

      Message thread  Asks for things: load a file, play, stop, loop, live or
                      file. Every call returns at once.

      Worker thread   Decodes and resamples. A long file at a foreign rate is
                      seconds of work, which the message thread must not spend
                      and the audio thread cannot. One thread, made the first
                      time a file is asked for, so an instance that never plays
                      one never has it; and one job at a time, so the jobs never
                      race each other for the decoded buffer.

      Audio thread    render(), which copies samples and nothing else. It takes
                      the buffer under a try-lock the worker holds only to swap
                      two pointers, and plays silence rather than wait.

    The file is resampled **once, to the host's rate**, on the worker, rather
    than on the fly: an offline resampler can afford a filter no realtime one
    could, and the circuit then sees exactly the samples a file already at the
    session rate would have given it. A file at the session rate is not touched
    at all, so it reaches the circuit bit-exact.
*/
class InputFilePlayer
{
public:
    /** The longest file taken, in seconds; anything longer is truncated, and
        the status says so. A bound on memory rather than on use: five minutes
        of stereo float is 115 MB at 48 kHz and 460 MB at 192 kHz, which is
        already more than a benching loop should cost. */
    static constexpr double maximumSeconds = 300.0;

    InputFilePlayer();
    ~InputFilePlayer();

    //==========================================================================
    // Message thread

    /** Decodes `file` in the background and, if it reads, replaces whatever
        was loaded, stopped and at the start. A file that fails leaves the
        previous one in place and says why in getStatus(). */
    void load (const juce::File& file);

    /** The rate render() will be called at. A loaded file at any other rate is
        dropped -- played as it stands it would come out at the wrong pitch --
        and resampled again in the background from the file on disk. Called from
        prepareToPlay, where the audio thread is not running. */
    void prepare (double sampleRate);

    /** File or live input. Going back to live stops the file, so that coming
        back to it starts from the top rather than from wherever it was left
        running unheard. */
    void setFileMode (bool shouldPlayFile) noexcept;
    bool isFileMode() const noexcept { return fileMode.load(); }

    void setLooping (bool shouldLoop) noexcept { looping.store (shouldLoop); }
    bool isLooping() const noexcept { return looping.load(); }

    /** Starts the file, from the top if it had played to the end. Switches to
        file input on the way: a Play that left the circuit listening to the
        host would be a button that does nothing you can hear. */
    void play() noexcept;

    /** Stops and goes back to the start. */
    void stop() noexcept;

    bool isPlaying() const noexcept { return playing.load(); }

    /** Everything the inspector shows, gathered in one go. */
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

        /** Counts finished loads, successful or not, so a caller can tell a new
            answer from the one it last reported. */
        int loadsFinished = 0;
    };

    Status getStatus() const;

    /** Blocks until no load is pending or running, or the time runs out.
        For tests and benchmarks, which need the file before the first block. */
    bool waitUntilLoaded (int timeoutMilliseconds) const;

    //==========================================================================
    // Audio thread

    /** In file mode, writes the file over `channels` -- or silence, when it is
        stopped, finished or still loading -- and returns true. In live mode,
        touches nothing and returns false.

        A mono file goes to every channel; a stereo file into one channel is
        their average, the same sum Mono L+R makes. Never allocates, never
        waits. */
    bool render (float* const* channels, int numChannels, int numSamples) noexcept;

    //==========================================================================
    /** Band-limited resampling: a Kaiser-windowed sinc, 64 zero crossings each
        side, the cutoff at 95% of the lower of the two Nyquists. Measured from
        44.1 to 48 kHz, every sample of a sine up to 20 kHz lands within -95 dB of
        the ideal one; from 96 down to 48 kHz, anything from 24.5 kHz up comes out
        at least 95 dB down instead of folding back into the band.

        Public because it is the one piece here with a right answer that can be
        checked against arithmetic. `cancel`, when given, is polled so a long
        job can be abandoned; the result is then empty. */
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source,
                                              double sourceRate, double destinationRate,
                                              const std::atomic<bool>* cancel = nullptr);

private:
    /** What the worker does every time it runs: bring the installed samples in
        line with the file and rate most recently asked for. One job rather than
        a "load" and a "resample", so whatever order the requests came in, the
        last one wins and a stale job finds nothing left to do. */
    void bringUpToDate();

    void startJob();

    /** Swaps `fresh` in, stopped and at the start. Returns the old buffer, so
        it is freed outside the lock. */
    std::unique_ptr<juce::AudioBuffer<float>> install (std::unique_ptr<juce::AudioBuffer<float>> fresh);

    //==========================================================================
    // The audio thread's side.

    juce::SpinLock bufferLock;
    std::unique_ptr<juce::AudioBuffer<float>> current;

    /** Written by the audio thread alone, except under `bufferLock`, where the
        audio thread cannot be. */
    std::atomic<juce::int64> position { 0 };

    /** How the message thread moves `position` without writing it. */
    std::atomic<bool> rewindPending { false };

    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<bool> fileMode { false };
    std::atomic<juce::int64> length { 0 };

    //==========================================================================
    // Everybody else's. Guarded by `stateLock`, which the audio thread never
    // takes.

    juce::CriticalSection stateLock;

    juce::File wantedFile, installedFile;
    double wantedRate = 44100.0, installedRate = 0.0, installedSourceRate = 0.0;
    bool installedTruncated = false;
    juce::String problem;
    int loadsFinished = 0;

    /** Jobs queued or running. Atomic so waitUntilLoaded can read it without
        the lock it would otherwise spin on. */
    std::atomic<int> jobsPending { 0 };

    std::atomic<bool> cancelled { false };

    /** Made on first use: see the class comment. Last, so it is destroyed
        first, waiting for its job while everything that job touches still
        exists. */
    std::unique_ptr<juce::ThreadPool> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputFilePlayer)
};
