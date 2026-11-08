#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <ui/ToolbarWidgets.h>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <functional>

/* Bypass from the power button or the host is one switch, and switching mid-playback
   must not click. Same crossfade and tests as AURA and GALLERY: the circuit,
   oversampler and cabinet always run; BypassFade decides how much is heard. */

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

    /** A steady 220 Hz sine for `blocks` blocks, calling `atBlock` before each; returns
        the left channel. */
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

    /** The worst break in the waveform between two positions: the second difference, near
        zero for a smooth signal. */
    float worstCurvature (const std::vector<float>& out, int from, int to)
    {
        auto worst = 0.0f;

        for (int i = juce::jmax (2, from); i < juce::jmin (to, (int) out.size()); ++i)
            worst = juce::jmax (worst, std::abs (out[(size_t) i] - 2.0f * out[(size_t) i - 1]
                                                 + out[(size_t) i - 2]));

        return worst;
    }

    /** Toggles bypass every `period` blocks and returns the worst break relative to the
        rougher of the two settled states (a clipped sine has corners). */
    float worstToggleRatio (PluginProcessor& plugin)
    {
        constexpr int period = 30;
        constexpr int warmUp = 2 * period;

        // Bypassed for the first period, on for the second, each settled by its end.
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

    /** A decaying noise burst as long as the cabinet cap, as a real .wav: a response with
        a tail. */
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
    // Pressing the host's bypass lights the button.
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
    // The default diode clipper driven into its knee: level, shape and phase all differ
    // from the input.
    SECTION ("at the host's rate")
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);
        setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);

        CHECK (worstToggleRatio (plugin) < 3.0f);
    }

    // Oversampled, the dry signal is delayed to meet the circuit.
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
    // A cabinet not fed while bypassed would replay its last input on re-engage.
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

    // Bypassed and silent for most of a second: longer than the cabinet and the
    // circuit's capacitors.
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
    // Bypassed output is the input delayed by the reported latency, bit for bit, per side
    // in every channel mode.
    for (const auto factor : PluginProcessor::oversamplingFactors)
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

            if (factor > 1)
                CHECK (latency > 0);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            std::vector<float> inLeft, inRight, outLeft, outRight;
            juce::Random random { 9 };

            for (int b = 0; b < 20; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    // Different on each side, so a mono fold would fail.
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

            // Past the fade and latency, every sample is the input from that long ago.
            for (auto i = (size_t) (latency + 4096); i < outLeft.size(); ++i)
            {
                REQUIRE (juce::exactlyEqual (outLeft[i], inLeft[i - (size_t) latency]));
                REQUIRE (juce::exactlyEqual (outRight[i], inRight[i - (size_t) latency]));
            }
        }
    }
}

TEST_CASE ("Changing the oversampling while bypassed does not click", "[bypass][clicks]")
{
    // A new factor moves the dry delay, which crossfades to its new position rather than
    // skipping samples.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    setParameter (plugin, "bypass", 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    double phase = 0.0;
    const double step = juce::MathConstants<double>::twoPi * 100.0 / sampleRate;

    const auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, phase += step)
                for (int c = 0; c < 2; ++c)
                    buffer.setSample (c, i, 0.5f * (float) std::sin (phase));

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
    };

    run (40);
    const auto before = plugin.getLatencySamples();
    REQUIRE (plugin.setOversamplingFactor (16).isValid());
    REQUIRE (plugin.getLatencySamples() != before);
    run (40);

    // A 100 Hz sine's second difference is under 1e-4; skipped samples would put a step
    // a hundred times that.
    float worst = 0.0f;

    for (size_t i = 2; i < out.size(); ++i)
        worst = juce::jmax (worst, std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]));

    INFO ("worst second difference " << worst);
    CHECK (worst < 1.0e-3f);
}

TEST_CASE ("Pressing Rebuild while a note plays does not click", "[clicks]")
{
    // Rebuilding dips through silence rather than swapping mid-waveform. Every example
    // sheet, plus factor changes. Measured as the largest sample-to-sample step: a pop is
    // a step; the 30 ms dip isn't.
    const auto names = SchematicModel::Examples::getNames();
    const int fadeLength = juce::roundToInt (PluginProcessor::rebuildFadeSeconds * sampleRate);

    struct Change
    {
        const char* what;
        int from, to;
    };

    for (const auto change : { Change { "Rebuild at 1x", 1, 1 }, Change { "Rebuild at 4x", 4, 4 },
                               Change { "1x to 4x", 1, 4 }, Change { "4x to 1x", 4, 1 } })
    {
        for (int example = 0; example < names.size(); ++example)
        {
            for (const bool silent : { false, true })
            {
                PluginProcessor plugin;
                plugin.prepareToPlay (sampleRate, blockSize);
                SchematicModel::Examples::load (plugin.getSchematic(), example);
                REQUIRE (plugin.setOversamplingFactor (change.from).isValid());
                setParameter (plugin, "bypass", 0.0f);

                constexpr int switchAt = 40;

                const auto doSwitch = [&]
                {
                    if (change.from == change.to)
                        REQUIRE (plugin.rebuild().isValid());
                    else
                        REQUIRE (plugin.setOversamplingFactor (change.to).isValid());
                };

                std::vector<float> out;

                if (silent)
                {
                    juce::AudioBuffer<float> buffer (2, blockSize);
                    juce::MidiBuffer midi;

                    for (int block = 0; block < switchAt * 2; ++block)
                    {
                        if (block == switchAt)
                            doSwitch();

                        buffer.clear();
                        plugin.processBlock (buffer, midi);

                        for (int i = 0; i < blockSize; ++i)
                            out.push_back (buffer.getSample (0, i));
                    }
                }
                else
                {
                    out = playSine (plugin, switchAt * 2, [&] (int block)
                    {
                        if (block == switchAt)
                            doSwitch();
                    });
                }

                const auto worstStep = [&out] (int from, int to)
                {
                    auto worst = 0.0f;

                    for (int i = juce::jmax (1, from); i < juce::jmin (to, (int) out.size()); ++i)
                        worst = juce::jmax (worst, std::abs (out[(size_t) i] - out[(size_t) i - 1]));

                    return worst;
                };

                const auto baseline = worstStep ((switchAt / 2) * blockSize, switchAt * blockSize);
                const auto around = worstStep (switchAt * blockSize, (int) out.size());

                INFO (change.what << ", " << names[example] << ", " << (silent ? "silent" : "a note")
                      << ": baseline " << baseline << ", around the switch " << around);

                if (silent)
                {
                    CHECK (around < 1.0e-6f);
                    continue;
                }

                CHECK (around <= 1.5f * baseline);

                // At the turn, a few samples either side are near silent.
                const auto turn = (size_t) (switchAt * blockSize + fadeLength);
                auto peak = 0.0f, atTurn = 0.0f;

                for (auto i = (size_t) ((switchAt / 2) * blockSize); i < (size_t) (switchAt * blockSize); ++i)
                    peak = juce::jmax (peak, std::abs (out[i]));

                for (auto i = turn - 4; i <= turn + 4; ++i)
                    atTurn = juce::jmax (atTurn, std::abs (out[i]));

                INFO ("peak " << peak << ", at the turn " << atTurn);
                CHECK (atTurn < 0.01f * peak);
            }
        }
    }
}
