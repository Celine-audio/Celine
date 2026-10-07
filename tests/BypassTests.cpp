#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <ui/ToolbarWidgets.h>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <functional>

/*
    Bypass is pressed from two places -- the power button and the host's -- and has to
    be one switch whichever is used. And because it is pressed with the track running,
    in the middle of an A/B, switching it must not be heard as anything but the circuit
    going away and coming back.

    The same crossfade AURA and GALLERY use, and the same tests, measured against a
    drawn circuit: the circuit, its oversampler and the cabinet all run whatever the
    bypass says, and BypassFade decides how much of them is heard.
*/

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void setParameter (PluginProcessor& plugin, const juce::String& id, float value)
    {
        auto* parameter = plugin.apvts.getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** A steady 220 Hz sine through the plugin for `blocks` blocks, calling `atBlock`
        before each one, and the left channel of what comes out. */
    std::vector<float> playSine (PluginProcessor& plugin, int blocks, const std::function<void (int)>& atBlock)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double phase = 0.0;
        const auto advance = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = 0.5f * (float) std::sin (phase);
                phase += advance;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            atBlock (block);
            plugin.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        return out;
    }

    /** The worst break in the waveform between two sample positions. The second
        difference: near zero for any smooth signal, and a spike wherever the waveform
        actually steps. */
    float worstCurvature (const std::vector<float>& out, int from, int to)
    {
        auto worst = 0.0f;

        for (int i = juce::jmax (2, from); i < juce::jmin (to, (int) out.size()); ++i)
            worst = juce::jmax (worst, std::abs (out[(size_t) i] - 2.0f * out[(size_t) i - 1]
                                                 + out[(size_t) i - 2]));

        return worst;
    }

    /** A tone through the plugin with the bypass toggled every `period` blocks, and the
        worst break in the waveform relative to the tone's own. The baseline is the
        rougher of the two settled states, bypassed and not -- a clipped sine has
        corners a clean one has not, and a fade between them is not a click. */
    float worstToggleRatio (PluginProcessor& plugin)
    {
        constexpr int period = 30;
        constexpr int warmUp = 2 * period;

        // Bypassed for the first period and on for the second, each settled by its end.
        auto bypassed = true;
        setParameter (plugin, "bypass", 1.0f);

        const auto out = playSine (plugin, warmUp + period * 6, [&] (int block)
        {
            if (block >= period && block % period == 0)
            {
                bypassed = ! bypassed;
                setParameter (plugin, "bypass", bypassed ? 1.0f : 0.0f);
            }
        });

        const auto settledHalfOf = [&out] (int periodIndex)
        {
            return worstCurvature (out, (periodIndex * period + period / 2) * blockSize,
                                   (periodIndex + 1) * period * blockSize);
        };

        const auto baseline = juce::jmax (settledHalfOf (0), settledHalfOf (1));
        const auto toggling = worstCurvature (out, warmUp * blockSize, (int) out.size());

        INFO ("baseline " << baseline << ", toggling " << toggling);
        REQUIRE (baseline > 0.0f);

        return toggling / baseline;
    }

    /** A decaying burst of noise as long as a cabinet the plugin keeps, as a real
        .wav: a response with a tail, so a cabinet replaying its past has something to
        replay it through. */
    juce::File writeCabinet (const juce::File& folder)
    {
        const auto file = folder.getChildFile ("cab.wav");
        constexpr int length = PluginProcessor::cabinetImpulseSamples;

        juce::AudioBuffer<float> ir (1, length);
        juce::Random random { 5 };

        for (int i = 0; i < length; ++i)
            ir.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * 0.3f
                                    * std::exp (-6.0f * (float) i / (float) length));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (1)
                                 .withBitsPerSample (24);

        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (ir, 0, length);
        writer.reset();

        return file;
    }

    void loadCabinet (PluginProcessor& plugin, const juce::File& ir)
    {
        for (auto& element : plugin.getSchematic().getElements())
            if (element.type == SchematicModel::ElementType::Output)
            {
                element.cabEnabled = true;
                element.cabFile = ir.getFullPathName();
            }

        REQUIRE (plugin.refreshCabinet().isEmpty());
        REQUIRE (plugin.getCabinetLength() > 1);
    }

    juce::File makeFolder()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("celine-bypass-" + juce::Uuid().toString());
    }
}

//==============================================================================
TEST_CASE ("The host's bypass is the plugin's own", "[bypass]")
{
    PluginProcessor plugin;
    CHECK (plugin.getBypassParameter() == plugin.apvts.getParameter ("bypass"));
}

TEST_CASE ("The power button follows a bypass pressed in the host", "[bypass][gui]")
{
    // The host's bypass is the plugin's own parameter, so pressing it there has to
    // light the button here -- otherwise the toolbar says the circuit is running while
    // the host has switched it out.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        Celine::PowerButton* power = nullptr;

        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<Celine::PowerButton*> (child))
                power = button;

        REQUIRE (power != nullptr);
        REQUIRE_FALSE (power->isActive());

        plugin.getBypassParameter()->setValueNotifyingHost (1.0f);
        CHECK (power->getToggleState());
        CHECK (power->isActive());

        plugin.getBypassParameter()->setValueNotifyingHost (0.0f);
        CHECK_FALSE (power->isActive());
    });
}

//==============================================================================
TEST_CASE ("Toggling the bypass does not click", "[bypass][clicks]")
{
    // The diode clipper on the default sheet, driven into its knee: a different level,
    // a different shape and a different phase from what went in, so swapping one for the
    // other in a sample was a step in the waveform.
    SECTION ("at the host's rate")
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);

        CHECK (worstToggleRatio (plugin) < 3.0f);
    }

    // Oversampled, the circuit comes back later than it went in, and the dry signal
    // has to be delayed to meet it.
    SECTION ("oversampled four times")
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        REQUIRE (plugin.setOversamplingFactor (4).isValid());
        setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);

        CHECK (worstToggleRatio (plugin) < 3.0f);
    }

    SECTION ("with a cabinet, which bypass takes away too")
    {
        const auto folder = makeFolder();
        REQUIRE (folder.createDirectory());

        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        loadCabinet (plugin, writeCabinet (folder));

        CHECK (worstToggleRatio (plugin) < 3.0f);

        folder.deleteRecursively();
    }
}

TEST_CASE ("Coming out of bypass does not play what the cabinet heard before it", "[bypass][clicks]")
{
    // A cabinet that is not fed while bypassed keeps the input it last heard, and on
    // the way back in convolves it: the past, arriving over a signal that has since
    // gone quiet. It used to be skipped while bypassed, which is exactly that.
    const auto folder = makeFolder();
    REQUIRE (folder.createDirectory());

    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    loadCabinet (plugin, writeCabinet (folder));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 23 };

    for (int block = 0; block < 40; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (channel, i, random.nextFloat() - 0.5f);

        plugin.processBlock (buffer, midi);
    }

    setParameter (plugin, "bypass", 1.0f);

    // Bypassed, and silent, for most of a second: many times the cabinet's length, and
    // long enough for the circuit's own capacitors to have let go too.
    for (int block = 0; block < 80; ++block)
    {
        buffer.clear();
        plugin.processBlock (buffer, midi);
    }

    setParameter (plugin, "bypass", 0.0f);

    auto ghost = 0.0f;

    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        plugin.processBlock (buffer, midi);
        ghost = juce::jmax (ghost, buffer.getMagnitude (0, blockSize));
    }

    INFO ("loudest sample in silence after leaving bypass: " << ghost);
    CHECK (ghost < 1.0e-3f);

    folder.deleteRecursively();
}

//==============================================================================
TEST_CASE ("Bypassed, the plugin is the input delayed by the latency it reports", "[bypass]")
{
    // The host compensates for the oversampler's latency whether the plugin is
    // bypassed or not, so the bypassed signal has to arrive that late too. It used to
    // get there by riding through the halfband filters with the circuit, which lined
    // it up but filtered it; it is a delay line now, so bypassed is bit for bit what
    // went in. Each side is its own input, whatever the channel mode, since only the
    // engaged path collapses to mono.
    for (const auto factor : { 1, 2, 4 })
    {
        for (const auto mode : { 0, 3 }) // Stereo, Mono L+R
        {
            PluginProcessor plugin;
            plugin.prepareToPlay (sampleRate, blockSize);
            REQUIRE (plugin.setOversamplingFactor (factor).isValid());
            setParameter (plugin, "channels", (float) mode);
            setParameter (plugin, "bypass", 1.0f);

            const auto latency = plugin.getLatencySamples();
            INFO ("oversampled " << factor << "x, mode " << mode << ", latency " << latency);

            CHECK (latency <= PluginProcessor::maximumOversamplingLatency);

            if (factor == 4)
                CHECK (latency > 0);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            std::vector<float> inLeft, inRight, outLeft, outRight;
            juce::Random random { 9 };

            for (int b = 0; b < 20; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    // Different on each side, so a bypass that folded to mono could not pass.
                    const auto left = random.nextFloat() * 0.5f - 0.25f;
                    const auto right = random.nextFloat() * 0.5f - 0.25f;
                    buffer.setSample (0, i, left);
                    buffer.setSample (1, i, right);
                    inLeft.push_back (left);
                    inRight.push_back (right);
                }

                plugin.processBlock (buffer, midi);

                for (int i = 0; i < blockSize; ++i)
                {
                    outLeft.push_back (buffer.getSample (0, i));
                    outRight.push_back (buffer.getSample (1, i));
                }
            }

            // Past the fade and the latency, every sample out is the one that went in
            // that long ago -- bit for bit, since bypass must not touch it at all.
            for (auto i = (size_t) (latency + 4096); i < outLeft.size(); ++i)
            {
                REQUIRE (juce::exactlyEqual (outLeft[i], inLeft[i - (size_t) latency]));
                REQUIRE (juce::exactlyEqual (outRight[i], inRight[i - (size_t) latency]));
            }
        }
    }
}
