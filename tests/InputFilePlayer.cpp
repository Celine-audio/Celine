#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <dsp/InputFilePlayer.h>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace
{
    /** Writes `audio` as a 32-bit float .wav, so what comes back off the disk is
        bit for bit what went on: every comparison below is exact or it is not
        testing the player. */
    juce::File writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (audio.getNumChannels())
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer.reset();

        return file;
    }

    /** A ramp, so every sample says where in the file it came from. */
    juce::AudioBuffer<float> ramp (int channels, int length)
    {
        juce::AudioBuffer<float> audio (channels, length);

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < length; ++i)
                audio.setSample (c, i, (float) (i + 1) / (float) length * (c == 0 ? 1.0f : -1.0f));

        return audio;
    }

    juce::AudioBuffer<float> sine (double frequency, double rate, int length)
    {
        juce::AudioBuffer<float> audio (1, length);

        for (int i = 0; i < length; ++i)
            audio.setSample (0, i, (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate));

        return audio;
    }

    /** The largest error against a sine at `rate`, away from the ends, where
        the filter runs off the edge of the file and is not meant to be exact.
        Against the ideal sine itself rather than a fitted one: fitting phase
        and amplitude would hide exactly the errors this is looking for. */
    double worstErrorAgainstSine (const juce::AudioBuffer<float>& audio, double frequency, double rate)
    {
        double worst = 0.0;
        const int margin = 400;

        for (int i = margin; i < audio.getNumSamples() - margin; ++i)
        {
            const double ideal = std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
            worst = juce::jmax (worst, std::abs (audio.getSample (0, i) - ideal));
        }

        return worst;
    }

    double rmsAwayFromEnds (const juce::AudioBuffer<float>& audio)
    {
        double sum = 0.0;
        const int margin = 400;
        int count = 0;

        for (int i = margin; i < audio.getNumSamples() - margin; ++i, ++count)
            sum += audio.getSample (0, i) * (double) audio.getSample (0, i);

        return std::sqrt (sum / juce::jmax (1, count));
    }

    struct Scratch
    {
        juce::TemporaryFile folder;
        Scratch() { folder.getFile().createDirectory(); }
        ~Scratch() { folder.getFile().deleteRecursively(); }
        juce::File operator/ (const juce::String& name) const { return folder.getFile().getChildFile (name); }
    };

    /** One render into a fresh block, every channel first set to a value the
        file cannot hold, so a sample the player forgot to write shows up. */
    juce::AudioBuffer<float> renderBlock (InputFilePlayer& player, int channels, int samples, bool& replaced)
    {
        juce::AudioBuffer<float> block (channels, samples);

        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::fill (block.getWritePointer (c), 7.0f, samples);

        replaced = player.render (block.getArrayOfWritePointers(), channels, samples);
        return block;
    }

    juce::AudioBuffer<float> renderBlock (InputFilePlayer& player, int channels, int samples)
    {
        bool replaced = false;
        return renderBlock (player, channels, samples, replaced);
    }

    bool isSilent (const juce::AudioBuffer<float>& block, int from = 0)
    {
        for (int c = 0; c < block.getNumChannels(); ++c)
            for (int i = from; i < block.getNumSamples(); ++i)
                if (! juce::exactlyEqual (block.getSample (c, i), 0.0f))
                    return false;

        return true;
    }
}

//==============================================================================
TEST_CASE ("A file already at the session rate is not resampled at all", "[file input]")
{
    const auto audio = sine (1000.0, 48000.0, 4800);
    const auto out = InputFilePlayer::resample (audio, 48000.0, 48000.0);

    REQUIRE (out.getNumSamples() == audio.getNumSamples());

    for (int i = 0; i < audio.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (out.getSample (0, i), audio.getSample (0, i)));
}

TEST_CASE ("Resampling a file lands on the sine it should", "[file input]")
{
    // Against the ideal sine at the new rate, sample for sample. Up from 44.1 kHz,
    // which is what most files a bench is fed are, and down from 96, the other
    // common mismatch; low, mid and near the top of the band each way.
    struct Case { double from, to, frequency, tolerance; };

    for (const auto& c : { Case { 44100.0, 48000.0,  1000.0, 1.0e-4 },
                           Case { 44100.0, 48000.0, 10000.0, 1.0e-4 },
                           Case { 44100.0, 48000.0, 18000.0, 1.0e-4 },
                           Case { 96000.0, 48000.0,  1000.0, 1.0e-4 },
                           Case { 96000.0, 48000.0, 18000.0, 1.0e-4 },
                           Case { 48000.0, 96000.0, 15000.0, 1.0e-4 } })
    {
        CAPTURE (c.from, c.to, c.frequency);

        const auto in = sine (c.frequency, c.from, (int) (c.from * 0.25));
        const auto out = InputFilePlayer::resample (in, c.from, c.to);

        CHECK (out.getNumSamples() == (int) std::llround (in.getNumSamples() * c.to / c.from));
        CHECK (worstErrorAgainstSine (out, c.frequency, c.to) < c.tolerance);
    }
}

TEST_CASE ("Coming down in rate, what will not fit is filtered out rather than folded back", "[file input]")
{
    // 30 kHz in a 96 kHz file has nowhere to go at 48: kept, it would land at 18
    // kHz as a tone the file never had. The filter's stopband is about -90 dB.
    const auto in = sine (30000.0, 96000.0, 24000);
    const auto out = InputFilePlayer::resample (in, 96000.0, 48000.0);

    const auto level = juce::Decibels::gainToDecibels (rmsAwayFromEnds (out) * std::sqrt (2.0));
    CAPTURE (level);
    CHECK (level < -85.0);
}

//==============================================================================
TEST_CASE ("The file player plays once, loops, and stops", "[file input]")
{
    Scratch scratch;
    const int length = 1000;
    const auto audio = ramp (1, length);
    const auto file = writeWav (scratch / "ramp.wav", audio, 48000.0);

    InputFilePlayer player;
    player.prepare (48000.0);
    player.load (file);
    REQUIRE (player.waitUntilLoaded (10000));

    const auto status = player.getStatus();
    REQUIRE (status.problem.isEmpty());
    REQUIRE (status.length == length);
    CHECK (status.file == file);
    CHECK (juce::exactlyEqual (status.sourceRate, 48000.0));

    SECTION ("live input is left alone")
    {
        bool replaced = true;
        const auto block = renderBlock (player, 2, 64, replaced);

        CHECK_FALSE (replaced);
        CHECK (juce::exactlyEqual (block.getSample (0, 0), 7.0f));
    }

    SECTION ("file input before Play is silence, not the live input")
    {
        player.setFileMode (true);
        bool replaced = false;
        const auto block = renderBlock (player, 2, 64, replaced);

        CHECK (replaced);
        CHECK (isSilent (block));
    }

    SECTION ("once: every sample, then silence, then Play starts it over")
    {
        player.play();
        CHECK (player.isFileMode());

        juce::AudioBuffer<float> heard (1, 1536);
        int at = 0;

        for (int b = 0; b < 6; ++b, at += 256)
        {
            const auto block = renderBlock (player, 1, 256);
            heard.copyFrom (0, at, block, 0, 0, 256);
        }

        for (int i = 0; i < length; ++i)
            REQUIRE (juce::exactlyEqual (heard.getSample (0, i), audio.getSample (0, i)));

        for (int i = length; i < heard.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (heard.getSample (0, i), 0.0f));

        CHECK_FALSE (player.isPlaying());
        CHECK (player.getStatus().position == length);

        player.play();
        const auto again = renderBlock (player, 1, 16);
        CHECK (juce::exactlyEqual (again.getSample (0, 0), audio.getSample (0, 0)));
    }

    SECTION ("the end is noticed on the block the file finishes in")
    {
        player.play();
        renderBlock (player, 1, 600);
        CHECK (player.isPlaying());
        renderBlock (player, 1, 400); // exactly to the last sample
        CHECK_FALSE (player.isPlaying());
    }

    SECTION ("looping goes straight from the last sample to the first")
    {
        player.setLooping (true);
        player.play();
        renderBlock (player, 1, 900);

        const auto block = renderBlock (player, 1, 300);

        for (int i = 0; i < 100; ++i)
            REQUIRE (juce::exactlyEqual (block.getSample (0, i), audio.getSample (0, 900 + i)));

        for (int i = 100; i < 300; ++i)
            REQUIRE (juce::exactlyEqual (block.getSample (0, i), audio.getSample (0, i - 100)));

        CHECK (player.isPlaying());
    }

    SECTION ("Stop silences it and goes back to the start")
    {
        player.play();
        renderBlock (player, 1, 300);
        player.stop();

        CHECK (isSilent (renderBlock (player, 1, 64)));
        CHECK (player.getStatus().position == 0);

        player.play();
        const auto block = renderBlock (player, 1, 16);
        CHECK (juce::exactlyEqual (block.getSample (0, 0), audio.getSample (0, 0)));
    }

    SECTION ("going back to live input stops the file")
    {
        player.play();
        renderBlock (player, 1, 300);
        player.setFileMode (false);

        CHECK_FALSE (player.isPlaying());
        CHECK_FALSE (player.isFileMode());

        bool replaced = true;
        renderBlock (player, 1, 64, replaced);
        CHECK_FALSE (replaced);
    }
}

TEST_CASE ("A file's channels reach the circuit's the way the channel modes would", "[file input]")
{
    Scratch scratch;
    const auto stereo = ramp (2, 200);
    const auto mono = ramp (1, 200);

    InputFilePlayer player;
    player.prepare (48000.0);

    SECTION ("stereo into stereo, left to left and right to right")
    {
        player.load (writeWav (scratch / "stereo.wav", stereo, 48000.0));
        REQUIRE (player.waitUntilLoaded (10000));
        player.play();

        const auto block = renderBlock (player, 2, 200);

        for (int i = 0; i < 200; ++i)
        {
            REQUIRE (juce::exactlyEqual (block.getSample (0, i), stereo.getSample (0, i)));
            REQUIRE (juce::exactlyEqual (block.getSample (1, i), stereo.getSample (1, i)));
        }
    }

    SECTION ("stereo into one channel, as their average")
    {
        player.load (writeWav (scratch / "stereo.wav", stereo, 48000.0));
        REQUIRE (player.waitUntilLoaded (10000));
        player.play();

        const auto block = renderBlock (player, 1, 200);

        // The ramp's two sides are equal and opposite, so the average is zero --
        // which a player that took the left alone would not give.
        CHECK (isSilent (block));
    }

    SECTION ("mono into both")
    {
        player.load (writeWav (scratch / "mono.wav", mono, 48000.0));
        REQUIRE (player.waitUntilLoaded (10000));
        player.play();

        const auto block = renderBlock (player, 2, 200);

        for (int i = 0; i < 200; ++i)
        {
            REQUIRE (juce::exactlyEqual (block.getSample (0, i), mono.getSample (0, i)));
            REQUIRE (juce::exactlyEqual (block.getSample (1, i), mono.getSample (0, i)));
        }
    }
}

TEST_CASE ("A file that will not load leaves the last one playing and says why", "[file input]")
{
    Scratch scratch;
    const auto good = writeWav (scratch / "good.wav", ramp (1, 500), 48000.0);
    const auto bad = scratch / "not-audio.wav";
    bad.replaceWithText ("This is a schematic, honest.");

    InputFilePlayer player;
    player.prepare (48000.0);
    player.load (good);
    REQUIRE (player.waitUntilLoaded (10000));

    const auto before = player.getStatus().loadsFinished;
    player.load (bad);
    REQUIRE (player.waitUntilLoaded (10000));

    const auto status = player.getStatus();
    CHECK (status.loadsFinished == before + 1);
    CHECK (status.problem.contains ("not-audio.wav"));
    CHECK (status.file == good);
    CHECK (status.length == 500);

    player.load (scratch / "nowhere.wav");
    REQUIRE (player.waitUntilLoaded (10000));
    CHECK (player.getStatus().problem.contains ("not found"));
    CHECK (player.getStatus().length == 500);
}

TEST_CASE ("Choosing a file switches to it only once it has read", "[file input]")
{
    // Switched on up front, a file that fails -- with nothing loaded before it
    // -- leaves the circuit listening to silence.
    Scratch scratch;
    const auto bad = scratch / "not-audio.wav";
    bad.replaceWithText ("This is a schematic, honest.");

    InputFilePlayer player;
    player.prepare (48000.0);

    player.load (bad, true);
    REQUIRE (player.waitUntilLoaded (10000));
    CHECK_FALSE (player.isFileMode());

    bool replaced = true;
    renderBlock (player, 2, 64, replaced);
    CHECK_FALSE (replaced);

    player.load (writeWav (scratch / "good.wav", ramp (1, 500), 48000.0), true);
    REQUIRE (player.waitUntilLoaded (10000));
    CHECK (player.isFileMode());

    // Going back to live while a load is still on its way is the later word.
    player.setFileMode (false);
    player.load (writeWav (scratch / "other.wav", ramp (1, 300), 48000.0), true);
    player.setFileMode (false);
    REQUIRE (player.waitUntilLoaded (10000));
    CHECK_FALSE (player.isFileMode());
    CHECK (player.getStatus().length == 300);
}

TEST_CASE ("A new session rate resamples the file rather than playing it at the wrong pitch", "[file input]")
{
    Scratch scratch;
    const auto file = writeWav (scratch / "tone.wav", sine (1000.0, 44100.0, 4410), 44100.0);

    InputFilePlayer player;
    player.prepare (48000.0);
    player.load (file);
    REQUIRE (player.waitUntilLoaded (10000));
    CHECK (player.getStatus().length == 4800);
    CHECK (juce::exactlyEqual (player.getStatus().sourceRate, 44100.0));

    player.prepare (96000.0);
    REQUIRE (player.waitUntilLoaded (10000));

    const auto status = player.getStatus();
    CHECK (status.length == 9600);
    CHECK (juce::exactlyEqual (status.sampleRate, 96000.0));
    CHECK (status.file == file);
}

//==============================================================================
TEST_CASE ("The plugin plays the file in place of its input, and bypass gives the file back", "[file input]")
{
    Scratch scratch;
    juce::Random random { 7 };

    juce::AudioBuffer<float> take (2, 4096);

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < take.getNumSamples(); ++i)
            take.setSample (c, i, random.nextFloat() * 0.5f - 0.25f);

    const auto file = writeWav (scratch / "take.wav", take, 48000.0);

    PluginProcessor plugin;

    // Bypassed before preparing, so the fade starts where it stands and the
    // output is the dry copy from the very first sample.
    plugin.getBypassParameter()->setValueNotifyingHost (1.0f);
    plugin.prepareToPlay (48000.0, 512);

    auto& player = plugin.getInputFilePlayer();
    player.load (file);
    REQUIRE (player.waitUntilLoaded (10000));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto feedLiveNoise = [&]
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, random.nextFloat() - 0.5f);
    };

    SECTION ("file input, stopped: the live input does not get through")
    {
        player.setFileMode (true);

        for (int b = 0; b < 4; ++b)
        {
            feedLiveNoise();
            plugin.processBlock (buffer, midi);
            REQUIRE (isSilent (buffer));
        }
    }

    SECTION ("playing, bypassed: the file comes out bit for bit")
    {
        REQUIRE (plugin.getLatencySamples() == 0);
        player.play();

        for (int b = 0; b < 8; ++b)
        {
            feedLiveNoise();
            plugin.processBlock (buffer, midi);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    REQUIRE (juce::exactlyEqual (buffer.getSample (c, i), take.getSample (c, b * 512 + i)));
        }

        CHECK_FALSE (player.isPlaying());
    }

    SECTION ("live input again, and the live input is what comes out")
    {
        player.play();
        player.setFileMode (false);

        feedLiveNoise();
        const auto live = buffer;
        plugin.processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                REQUIRE (juce::exactlyEqual (buffer.getSample (c, i), live.getSample (c, i)));
    }
}

TEST_CASE ("Nothing about the file player is saved, in the sheet or the session", "[file input]")
{
    Scratch scratch;
    PluginProcessor plugin;

    const auto document = plugin.createDocument().toXmlString();
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    auto& player = plugin.getInputFilePlayer();
    player.load (writeWav (scratch / "take.wav", ramp (2, 2000), 44100.0));
    REQUIRE (player.waitUntilLoaded (10000));
    player.setLooping (true);
    player.play();

    CHECK (plugin.createDocument().toXmlString() == document);

    juce::MemoryBlock after;
    plugin.getStateInformation (after);
    CHECK (after == state);
}
