#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginEditor.h"
#include "Schematic/ExampleSchematics.h"

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
    apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    jassert (bypassParameter != nullptr);

    channelModeParameter = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("channels"));
    jassert (channelModeParameter != nullptr);

    // Parameter values processBlock reads, resolved here so the audio thread never builds
    // an id.
    bypassValue = apvts.getRawParameterValue ("bypass");
    inputGainValue = apvts.getRawParameterValue ("input");
    outputGainValue = apvts.getRawParameterValue ("output");

    jassert (bypassValue != nullptr && inputGainValue != nullptr && outputGainValue != nullptr);

    for (int i = 0; i < maxLiveControls; ++i)
    {
        controlValues[(size_t) i] = apvts.getRawParameterValue (getControlParameterId (i));
        jassert (controlValues[(size_t) i] != nullptr);
    }

    SchematicModel::Examples::load (schematic, SchematicModel::Examples::defaultIndex);

    // Build now, so an editor opened before playback finds a circuit.
    rebuild();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input gain: one volt of circuit voltage per unit of full scale, so a 0 dBFS sine is
    // 1 V peak on the input net.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "input", 1 },
        "Input (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.1f),
        0.0f));

    // A fixed pool of knob parameters; the drawn controls map onto it in order.
    for (int i = 0; i < maxLiveControls; ++i)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { getControlParameterId (i), 1 },
            "Knob " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
            0.5f));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "output", 1 },
        "Output (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 },
        "Bypass",
        false));

    // Stereo runs one circuit per channel; the mono modes run one and copy its output.
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "channels", 1 },
        "Channels",
        juce::StringArray { "Stereo", "Mono L", "Mono R", "Mono L+R" },
        0));

    return { params.begin(), params.end() };
}

PluginProcessor::~PluginProcessor()
{
}

//==============================================================================
juce::String PluginProcessor::getBuildVersion()
{
    // From the VERSION file, via CMake.
    return VERSION;
}

int PluginProcessor::compareVersions (const juce::String& a, const juce::String& b)
{
    const auto left = juce::StringArray::fromTokens (a, ".", "");
    const auto right = juce::StringArray::fromTokens (b, ".", "");

    for (int i = 0; i < juce::jmax (left.size(), right.size()); ++i)
    {
        // Missing components count as zero; getIntValue() stops at the first non-digit, so
        // "3-rc1" reads as 3.
        const int l = i < left.size() ? left[i].trim().getIntValue() : 0;
        const int r = i < right.size() ? right[i].trim().getIntValue() : 0;

        if (l != r)
            return l < r ? -1 : 1;
    }

    return 0;
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    // fromUTF8: String(const char*) would read the name as ASCII and mangle the "é".
    return juce::String::fromUTF8 (JucePlugin_Name);
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);

    // Named: VST3 validation fails a plugin whose only program has no name.
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

// ==============================================================================
// Building and swapping
// ==============================================================================

SchematicModel::BuildResult PluginProcessor::rebuild()
{
    return rebuildAt (oversamplingFactor, nullptr);
}

PluginProcessor::PreparedOversampler PluginProcessor::makeOversampler (int factor) const
{
    PreparedOversampler prepared;

    if (factor > 1)
    {
        // Polyphase IIR halfbands: much cheaper than FIR; the phase shift doesn't matter here.
        const auto stages = static_cast<size_t> (juce::findHighestSetBit (static_cast<juce::uint32> (factor)));

        prepared.filters = std::make_unique<juce::dsp::Oversampling<float>> (
            2, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);

        prepared.filters->initProcessing (static_cast<size_t> (preparedBlockSize));
        prepared.filters->reset();

        prepared.latency = juce::roundToInt (prepared.filters->getLatencyInSamples());
    }

    return prepared;
}

void PluginProcessor::installOversamplerNow (PreparedOversampler& fresh, int factor)
{
    // Swapped under the lock so the audio thread never uses a destroyed oversampler. The
    // old one is freed with `fresh`, off-lock.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);

        // A circuit fading out on the shared filters keeps them until its fade ends.
        if (fadeOutRemaining > 0 && ! fadingOwnOversampler)
        {
            fadingOversampler = std::move (oversampler);
            fadingOwnOversampler = true;
        }

        fresh.filters.swap (oversampler);
    }

    oversamplingFactor = factor;

    // After the swap, so the reported latency matches the running filters.
    setLatencySamples (fresh.latency);
}

void PluginProcessor::cancelRebuildFade()
{
    std::vector<std::unique_ptr<Circuit>> stale;
    std::unique_ptr<juce::dsp::Oversampling<float>> staleFilters;

    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);
        stale.swap (fadingCircuits);
        staleFilters.swap (fadingOversampler);
        fadingOwnOversampler = false;
        fadeOutRemaining = 0;
        fadeInRemaining = 0;
    }
}

SchematicModel::BuildResult PluginProcessor::rebuildAt (int factor, PreparedOversampler* fresh)
{
    const auto channelCount = juce::jmax (1, getTotalNumOutputChannels());

    // The expensive work (stamping, factorising, bias solve) happens here, off-lock, with
    // the old circuit still running. The circuit runs at the oversampled rate.
    const double rate = currentSampleRate * factor;
    auto result = SchematicModel::buildCircuits (schematic, rate, channelCount, buildOptions);

    // Model substitutions from the last load, put at the front of the build messages.
    if (auto substitutions = schematic.takeLoadWarnings(); ! substitutions.isEmpty())
    {
        std::vector<SchematicModel::Diagnostic> combined;
        combined.reserve (static_cast<size_t> (substitutions.size()) + result.diagnostics.size());

        for (const auto& text : substitutions)
        {
            SchematicModel::Diagnostic d;
            d.severity = SchematicModel::Diagnostic::Severity::Warning;
            d.text = text;
            combined.push_back (std::move (d));
        }

        for (auto& existing : result.diagnostics)
            combined.push_back (std::move (existing));

        result.diagnostics = std::move (combined);
    }

    if (! result.isValid())
    {
        // On failure keep the running circuit. A new oversampler still goes in.
        if (fresh != nullptr)
            installOversamplerNow (*fresh, factor);

        return result;
    }

    // Copied off-lock, since copying allocates.
    auto incomingControls = result.controls;
    auto incomingProbes = result.probes;
    auto incomingCurrentProbes = result.currentProbes;

    // Freed after the lock is released.
    std::vector<std::unique_ptr<Circuit>> outgoingCircuits, finishedCircuits;
    std::unique_ptr<juce::dsp::Oversampling<float>> outgoingFilters, finishedFilters;

    // At the host rate: the fade is applied after downsampling.
    const int fadeLength = juce::jmax (1, juce::roundToInt (rebuildFadeSeconds * currentSampleRate));

    // The locked part: swaps and moves only, no allocation or destruction.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);
        rebuildFadeLength = fadeLength;

        if (circuits.empty())
        {
            // Nothing playing (first build, or after a failure): no fade out needed.
            circuits.swap (result.circuits);

            if (fresh != nullptr)
            {
                outgoingFilters = std::move (oversampler);
                oversampler = std::move (fresh->filters);
            }
        }
        else if (fadeOutRemaining > 0)
        {
            // Already fading out an older circuit: let that finish; the unheard one waiting to
            // fade in is replaced by this one.
            outgoingCircuits.swap (circuits);
            circuits.swap (result.circuits);

            if (fresh != nullptr)
            {
                // The fading circuit keeps its own filters.
                if (fadingOwnOversampler)
                    outgoingFilters = std::move (oversampler);
                else
                    fadingOversampler = std::move (oversampler);

                fadingOwnOversampler = true;
                oversampler = std::move (fresh->filters);
            }
        }
        else
        {
            // Start fading out from the current level, so an interrupted fade in turns round
            // without a step.
            finishedCircuits.swap (fadingCircuits);
            finishedFilters = std::move (fadingOversampler);

            fadingCircuits.swap (circuits);
            circuits.swap (result.circuits);

            // A new factor: the old circuit fades out on its own filters and the new one uses the
            // new ones. Same factor: they share one set.
            fadingOwnOversampler = fresh != nullptr;

            if (fresh != nullptr)
            {
                fadingOversampler = std::move (oversampler);
                oversampler = std::move (fresh->filters);
            }

            fadeOutRemaining = juce::jmax (1, fadeLength - fadeInRemaining);
            fadeInRemaining = 0;
            rebuildFadeFinished.store (false);
        }

        controls.swap (incomingControls);

        // Swapped with the circuits: probe node names belong to the circuit they came from.
        probes.swap (incomingProbes);
        currentProbes.swap (incomingCurrentProbes);
    }

    // The old contents are freed when these locals go out of scope, off-lock.

    if (fresh != nullptr)
    {
        oversamplingFactor = factor;

        // Report the new latency now; the shift happens under the fade out.
        setLatencySamples (fresh->latency);
    }

    prepareScopes();

    // Report controls that don't fit the parameter pool.
    if (static_cast<int> (controls.size()) > maxLiveControls)
    {
        const int dropped = static_cast<int> (controls.size()) - maxLiveControls;

        juce::StringArray names;

        for (size_t i = static_cast<size_t> (maxLiveControls); i < controls.size(); ++i)
            names.add (controls[i].name);

        result.add (SchematicModel::Diagnostic::Severity::Warning,
                    juce::String (dropped) + (dropped == 1 ? " control has" : " controls have")
                        + " no knob: " + names.joinIntoString (", ")
                        + ". There are " + juce::String (maxLiveControls)
                        + " knob parameters and the drawing uses more; they still work at the value"
                          " they were drawn with.");
    }

    // Load the cabinet with the sheet.
    if (const auto problem = refreshCabinet(); problem.isNotEmpty())
        result.add (SchematicModel::Diagnostic::Severity::Warning, problem, "Cab");

    // The circuits were moved out; the controls stay for the caller.
    result.circuits.clear();
    return result;
}

void PluginProcessor::releaseFadedCircuits()
{
    if (! rebuildFadeFinished.exchange (false))
        return;

    std::vector<std::unique_ptr<Circuit>> finished;
    std::unique_ptr<juce::dsp::Oversampling<float>> finishedFilters;

    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);

        // A newer fade out may have started since the flag was set.
        if (fadeOutRemaining == 0)
        {
            finished.swap (fadingCircuits);
            finishedFilters.swap (fadingOversampler);
            fadingOwnOversampler = false;
        }
    }
}

std::vector<SchematicModel::LiveControl> PluginProcessor::getLiveControls() const
{
    const juce::SpinLock::ScopedLockType lock (circuitLock);
    return controls;
}

// ==============================================================================
// Rounds down to a supported power of two (the halfband chain only does powers of
// two).
static int normaliseOversamplingFactor (int factor) noexcept
{
    factor = juce::jlimit (1, PluginProcessor::maximumOversamplingFactor, factor);
    return 1 << juce::findHighestSetBit (static_cast<juce::uint32> (factor));
}

SchematicModel::BuildResult PluginProcessor::setOversamplingFactor (int factor)
{
    factor = normaliseOversamplingFactor (factor);

    // A new factor needs new circuits (the discretisation depends on the timestep). They
    // swap in with the filters, through the rebuild fade.
    auto filters = makeOversampler (factor);
    return rebuildAt (factor, &filters);
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = juce::jmax (1, samplesPerBlock);

    // At the host rate, sized for the longest response at the highest rate. Allocates,
    // so only here.
    cabinet.prepare (juce::jmax (1, getTotalNumOutputChannels()), maximumCabinetSamples);

    // prepare() reset the convolver: forget the file so refreshCabinet() reloads it.
    loadedCabinetFile = juce::File{};
    cabinetLength.store (0);

    // A file at another rate is resampled again in the background.
    inputFile.prepare (sampleRate);

    // Swap in directly: audio is stopped, nothing to fade.
    {
        auto filters = makeOversampler (oversamplingFactor);
        installOversamplerNow (filters, oversamplingFactor);
    }

    rebuild();
    cancelRebuildFade();

    // After rebuild() fills `probes`; column widths depend on the sample rate.
    prepareScopes();

    // Sized for the largest latency of any factor. Starts at the current bypass state.
    jassert (getLatencySamples() <= maximumOversamplingLatency);

    bypassFade.prepare (juce::jmax (1, getTotalNumOutputChannels()), preparedBlockSize,
                        maximumOversamplingLatency, sampleRate);
    bypassFade.reset (bypassValue->load() > 0.5f);
}

void PluginProcessor::prepareScopes()
{
    // Takes the circuit lock (the audio thread only tries it). Callers must not hold it:
    // SpinLock isn't recursive.
    const juce::SpinLock::ScopedLockType lock (circuitLock);

    // Samples per column at the circuit's actual (oversampled) rate, so the time span
    // doesn't change with the factor.
    const auto rate = currentSampleRate * oversamplingFactor;

    const auto count = juce::jmin (static_cast<int> (probes.size()), maxScopes);

    for (int i = 0; i < maxScopes; ++i)
    {
        // Per probe window; unused slots get a non-zero default (it is a divisor).
        const auto seconds = i < count ? probes[static_cast<size_t> (i)].windowSeconds
                                       : scopeWindowSeconds;

        samplesPerScopeColumn[static_cast<size_t> (i)] = juce::jmax (
            1, juce::roundToInt (rate * seconds / ScopeTrace::columns));
    }

    // The current readout keeps the default window.
    samplesPerCurrentWindow = juce::jmax (
        1, juce::roundToInt (rate * scopeWindowSeconds));

    for (int i = 0; i < maxScopes; ++i)
    {
        auto& trace = scopeTraces[static_cast<size_t> (i)];
        const bool used = i < count;

        // Clear every slot, so moved or removed probes don't leave stale pictures.
        for (int c = 0; c < ScopeTrace::columns; ++c)
        {
            trace.minimum[c].store (0.0f, std::memory_order_relaxed);
            trace.maximum[c].store (0.0f, std::memory_order_relaxed);
        }

        trace.writeColumn.store (0, std::memory_order_relaxed);
        trace.dcAverage.store (0.0f, std::memory_order_relaxed);
        trace.peakToPeak.store (0.0f, std::memory_order_relaxed);

        trace.live.store (used, std::memory_order_relaxed);
        scopeAccumulators[static_cast<size_t> (i)] = {};

        // Published under the lock so getScopeTrace() needn't read `probes`.
        scopeElementIds[static_cast<size_t> (i)].store (
            used ? probes[static_cast<size_t> (i)].elementId : 0, std::memory_order_relaxed);
    }

    // The nets were re-extracted, so reset the accumulator.
    currentAccumulator = {};
    inspectedLive.store (false, std::memory_order_relaxed);
}

void PluginProcessor::refreshScopeTiming()
{
    // Update each probe's time base before prepareScopes reads them. Under the lock: the
    // audio thread reads this vector.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);

        for (auto& probe : probes)
            if (const auto* element = schematic.findElement (probe.elementId))
                probe.windowSeconds = element->scopeSeconds;
    }

    // Outside the lock: prepareScopes() takes it itself.
    prepareScopes();
}

void PluginProcessor::setInspectedElement (int elementId) noexcept
{
    if (inspectedElement.exchange (elementId, std::memory_order_relaxed) == elementId)
        return;

    // Cleared immediately, so the old part's reading never shows for the new one.
    inspectedLive.store (false, std::memory_order_relaxed);
    inspectedCurrent.store (0.0f, std::memory_order_relaxed);
    inspectedPower.store (0.0f, std::memory_order_relaxed);
    inspectedPeakCurrent.store (0.0f, std::memory_order_relaxed);
}

bool PluginProcessor::readInspectedCurrent (float& current, float& power,
                                            float& peak) const noexcept
{
    if (! inspectedLive.load (std::memory_order_relaxed))
        return false;

    current = inspectedCurrent.load (std::memory_order_relaxed);
    power = inspectedPower.load (std::memory_order_relaxed);
    peak = inspectedPeakCurrent.load (std::memory_order_relaxed);
    return true;
}

void PluginProcessor::sampleInspectedCurrent (Circuit& circuit) noexcept
{
    const auto wanted = inspectedElement.load (std::memory_order_relaxed);

    if (wanted == 0)
        return;

    const SchematicModel::CurrentProbe* probe = nullptr;

    for (const auto& candidate : currentProbes)
        if (candidate.elementId == wanted)
        {
            probe = &candidate;
            break;
        }

    if (probe == nullptr || probe->resistance <= 0.0)
        return;

    // Ohm's law across the part's two nets.
    const auto volts = circuit.getNodeVoltage (probe->indexA)
                     - circuit.getNodeVoltage (probe->indexB);
    const auto amps = volts / probe->resistance;

    currentAccumulator.sumCurrent += amps;
    currentAccumulator.sumPower += volts * amps;
    currentAccumulator.peakCurrent = juce::jmax (currentAccumulator.peakCurrent, std::abs (amps));

    if (++currentAccumulator.count < samplesPerCurrentWindow)
        return;

    const auto scale = 1.0 / static_cast<double> (currentAccumulator.count);

    inspectedCurrent.store (static_cast<float> (currentAccumulator.sumCurrent * scale),
                            std::memory_order_relaxed);
    inspectedPower.store (static_cast<float> (currentAccumulator.sumPower * scale),
                          std::memory_order_relaxed);
    inspectedPeakCurrent.store (static_cast<float> (currentAccumulator.peakCurrent),
                                std::memory_order_relaxed);
    inspectedLive.store (true, std::memory_order_relaxed);

    currentAccumulator = {};
}

const PluginProcessor::ScopeTrace* PluginProcessor::getScopeTrace (int elementId) const noexcept
{
    // Matched against the atomic ids, not `probes`, so the paint path never takes the
    // lock. Id 0 never matches.
    if (elementId == 0)
        return nullptr;

    for (int i = 0; i < maxScopes; ++i)
        if (scopeElementIds[static_cast<size_t> (i)].load (std::memory_order_relaxed) == elementId)
            return &scopeTraces[static_cast<size_t> (i)];

    return nullptr;
}

void PluginProcessor::sampleScopes (Circuit& circuit) noexcept
{
    const auto count = juce::jmin (static_cast<int> (probes.size()), maxScopes);

    for (int i = 0; i < count; ++i)
    {
        const auto& probe = probes[static_cast<size_t> (i)];

        // Differential, by node index (no name lookups per sample).
        const auto value = static_cast<float> (circuit.getNodeVoltage (probe.positiveIndex)
                                             - circuit.getNodeVoltage (probe.referenceIndex));

        auto& accumulator = scopeAccumulators[static_cast<size_t> (i)];

        if (accumulator.count == 0)
        {
            accumulator.minimum = value;
            accumulator.maximum = value;
        }
        else
        {
            accumulator.minimum = juce::jmin (accumulator.minimum, value);
            accumulator.maximum = juce::jmax (accumulator.maximum, value);
        }

        if (++accumulator.count < samplesPerScopeColumn[static_cast<size_t> (i)])
            continue;

        auto& trace = scopeTraces[static_cast<size_t> (i)];
        const int column = trace.writeColumn.load (std::memory_order_relaxed);

        trace.minimum[column].store (accumulator.minimum, std::memory_order_relaxed);
        trace.maximum[column].store (accumulator.maximum, std::memory_order_relaxed);

        // Published after the column is written.
        trace.writeColumn.store ((column + 1) % ScopeTrace::columns, std::memory_order_release);

        // Refreshed once per column, averaged over the whole window.
        float lowest = accumulator.minimum, highest = accumulator.maximum;
        double total = 0.0;

        for (int c = 0; c < ScopeTrace::columns; ++c)
        {
            const auto low = trace.minimum[c].load (std::memory_order_relaxed);
            const auto high = trace.maximum[c].load (std::memory_order_relaxed);

            lowest = juce::jmin (lowest, low);
            highest = juce::jmax (highest, high);
            total += 0.5 * (low + high);
        }

        trace.dcAverage.store (static_cast<float> (total / ScopeTrace::columns),
                               std::memory_order_relaxed);
        trace.peakToPeak.store (highest - lowest, std::memory_order_relaxed);

        accumulator = {};
    }
}

namespace
{
    /** Resamples a response to a new rate, the same way JUCE's convolution loader did, so
        existing cabinets sound the same. */
    juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& source,
                                         double sourceRate, double destinationRate)
    {
        if (sourceRate <= 0.0 || destinationRate <= 0.0
            || juce::approximatelyEqual (sourceRate, destinationRate))
            return source;

        const auto ratio = sourceRate / destinationRate;

        juce::AudioBuffer<float> original = source;
        juce::MemoryAudioSource memory (original, false);
        juce::ResamplingAudioSource resampler (&memory, false, source.getNumChannels());

        const auto length = juce::roundToInt (juce::jmax (1.0, source.getNumSamples() / ratio));

        resampler.setResamplingRatio (ratio);
        resampler.prepareToPlay (length, sourceRate);

        juce::AudioBuffer<float> result (source.getNumChannels(), length);
        resampler.getNextAudioBlock ({ &result, 0, result.getNumSamples() });

        return result;
    }

    /** Scales a response so its loudest channel has an eighth of unit energy, as
        juce::dsp::Convolution's Normalise::yes does. */
    void normalise (juce::AudioBuffer<float>& buffer)
    {
        float loudest = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* samples = buffer.getReadPointer (channel);
            float sum = 0.0f;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                sum += samples[i] * samples[i];

            loudest = juce::jmax (loudest, sum);
        }

        if (loudest < 1.0e-8f)
            return;

        buffer.applyGain (0.125f / std::sqrt (loudest));
    }
}

juce::String PluginProcessor::refreshCabinet()
{
    // The first Output terminal with something set; it owns the cabinet.
    const SchematicModel::Element* output = nullptr;

    for (const auto& element : schematic.getElements())
    {
        if (element.type == SchematicModel::ElementType::Output)
        {
            output = &element;
            break;
        }
    }

    if (output == nullptr || ! output->cabEnabled || output->cabFile.isEmpty())
    {
        cabinetActive = false;
        loadedCabinetFile = juce::File{};
        return {};
    }

    const juce::File file (output->cabFile);

    if (! file.existsAsFile())
    {
        cabinetActive = false;
        loadedCabinetFile = juce::File{};

        // The setting stays on, so the cabinet is used again if the file returns.
        return "Cab impulse response not found: " + file.getFullPathName()
             + " -- the switch is still on, and audio passes through without it.";
    }

    if (file == loadedCabinetFile)
    {
        cabinetActive = true;
        return {};
    }

    // Read here, so a bad file is an error this function can report.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 1)
    {
        cabinetActive = false;
        loadedCabinetFile = juce::File{};
        return file.getFileName() + " is not an audio file this build can read.";
    }

    juce::AudioBuffer<float> response (juce::jlimit (1, 2, (int) reader->numChannels),
                                       (int) juce::jmin (reader->lengthInSamples,
                                                         (juce::int64) maximumCabinetSamples));
    reader->read (&response, 0, response.getNumSamples(), 0, true, reader->numChannels > 1);

    response = resampleTo (response, reader->sampleRate, currentSampleRate);

    // The tap cap is a duration (2048 taps at 48 kHz), applied after resampling.
    const auto taps = juce::jmax (1, juce::roundToInt (cabinetImpulseSamples
                                                       * currentSampleRate / cabinetReferenceRate));

    if (response.getNumSamples() > taps)
        response.setSize (response.getNumChannels(), taps, true, true, true);

    // Normalised, matching JUCE's convolver, so levels stay the same.
    normalise (response);

    cabinet.setImpulseResponse (response, false);
    cabinetLength.store (response.getNumSamples());

    loadedCabinetFile = file;
    cabinetActive = true;
    return {};
}

void PluginProcessor::releaseResources()
{
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void PluginProcessor::runStereo (std::vector<std::unique_ptr<Circuit>>& set, bool live,
                                 float* left, float* right, int count, const BlockState& state)
{
    float* channels[2] = { left, right };

    for (int channel = 0; channel < 2; ++channel)
    {
        if (channels[channel] == nullptr || static_cast<size_t> (channel) >= set.size())
            continue;

        auto& circuit = *set[static_cast<size_t> (channel)];
        auto* samples = channels[channel];

        // Only the live circuit takes the controls and feeds the probes.
        if (live)
            applyControls (circuit, state);

        // Measured on the left channel only.
        const bool measured = live && channel == 0;
        const bool probed = measured && ! probes.empty();

        for (int s = 0; s < count; ++s)
        {
            const float dry = samples[s];
            const float wet = circuit.process (dry * state.inputGain) * state.outputGain;

            if (probed)
                sampleScopes (circuit);

            if (measured)
                sampleInspectedCurrent (circuit);

            // The circuit runs even when bypassed; the bypass fade decides what is heard.
            samples[s] = wet;
        }
    }
}

void PluginProcessor::runMono (std::vector<std::unique_ptr<Circuit>>& set, bool live,
                               float* left, float* right, int count, const BlockState& state)
{
    if (set.empty())
        return;

    auto& circuit = *set[0];

    // See runStereo.
    if (live)
        applyControls (circuit, state);

    const bool haveTwoInputs = right != nullptr;

    for (int s = 0; s < count; ++s)
    {
        // Read both channels before writing either.
        const float dryLeft = left[s];
        const float dryRight = haveTwoInputs ? right[s] : dryLeft;

        const float source = state.mode == ChannelMode::MonoLeft  ? dryLeft
                           : state.mode == ChannelMode::MonoRight ? dryRight
                                                                  : 0.5f * (dryLeft + dryRight);

        const float wet = circuit.process (source * state.inputGain) * state.outputGain;

        if (live)
        {
            if (! probes.empty())
                sampleScopes (circuit);

            sampleInspectedCurrent (circuit);
        }

        // Written regardless of bypass; the bypass fade blends towards its own dry copy.
        left[s] = wet;

        if (right != nullptr)
            right[s] = wet;
    }
}

void PluginProcessor::applyRebuildFade (juce::AudioBuffer<float>& piece, bool outgoing) noexcept
{
    const auto numSamples = piece.getNumSamples();
    const auto channels = piece.getNumChannels();
    const auto length = (float) rebuildFadeLength;

    if (outgoing)
    {
        // Fade down to silence; the piece was cut to end where this does.
        for (int i = 0; i < numSamples && fadeOutRemaining > 0; ++i, --fadeOutRemaining)
        {
            const auto gain = (float) fadeOutRemaining / length;

            for (int c = 0; c < channels; ++c)
                piece.setSample (c, i, piece.getSample (c, i) * gain);
        }

        // Silent: the new circuit starts from here, and the old one can be freed.
        if (fadeOutRemaining == 0)
        {
            fadeInRemaining = rebuildFadeLength;
            rebuildFadeFinished.store (true);
        }

        return;
    }

    // Fade up from silence, one gain per sample for all channels.
    for (int i = 0; i < numSamples && fadeInRemaining > 0; ++i, --fadeInRemaining)
    {
        const auto gain = 1.0f - (float) fadeInRemaining / length;

        for (int c = 0; c < channels; ++c)
            piece.setSample (c, i, piece.getSample (c, i) * gain);
    }
}

void PluginProcessor::applyControls (Circuit& circuit, const BlockState& state)
{
    for (int i = 0; i < state.controlCount; ++i)
        controls[static_cast<size_t> (i)].apply (circuit, state.controls[i]);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // The input file replaces the host input before anything reads it (bypass included),
    // and before the try-lock, so a missed lock passes the file through. All channels, so
    // a standalone without an input device still plays it.
    inputFile.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());

    // Never wait: a missed lock costs one dry block. Dry through the bypass fade, not by
    // returning early, so the latency stays consistent.
    const juce::SpinLock::ScopedTryLockType lock (circuitLock);
    const bool circuitReady = lock.isLocked() && ! circuits.empty();

    BlockState state;
    state.bypassed = bypassValue->load() > 0.5f;

    if (circuitReady)
    {
        state.mode = static_cast<ChannelMode> (channelModeParameter->getIndex());
        state.inputGain = juce::Decibels::decibelsToGain (inputGainValue->load());
        state.outputGain = juce::Decibels::decibelsToGain (outputGainValue->load());

        // Applied once per block; the circuit only re-factorises when a value changes.
        for (int i = 0; i < maxLiveControls; ++i)
            state.controls[i] = controlValues[(size_t) i]->load();

        state.controlCount = juce::jmin (static_cast<int> (controls.size()), maxLiveControls);

        // Back to stereo: reset the idle right circuit, whose capacitors still hold old
        // charge.
        if (state.mode != lastChannelMode)
        {
            if (state.mode == ChannelMode::Stereo)
                for (size_t c = 1; c < circuits.size(); ++c)
                    circuits[c]->reset();

            lastChannelMode = state.mode;
        }
    }

    // Blocks larger than declared in prepareToPlay are processed in pieces.
    const auto numSamples = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> whole (buffer);

    // The latency the host compensates for, matched by the bypass dry path.
    const auto latency = getLatencySamples();

    for (int start = 0, count = 0; start < numSamples; start += count)
    {
        // During a rebuild fade out: the old circuit, cut so the piece ends where silence
        // begins.
        const bool outgoing = circuitReady && fadeOutRemaining > 0;

        count = juce::jmin (preparedBlockSize, numSamples - start);

        if (outgoing)
            count = juce::jmin (count, fadeOutRemaining);

        auto piece = whole.getSubBlock (static_cast<size_t> (start), static_cast<size_t> (count));

        // A view onto the piece without allocating.
        float* channels[2] = { piece.getChannelPointer (0),
                               piece.getNumChannels() > 1 ? piece.getChannelPointer (1) : nullptr };

        juce::AudioBuffer<float> view (channels,
                                       (int) juce::jmin ((size_t) 2, piece.getNumChannels()),
                                       (int) piece.getNumSamples());

        // Taken before processing: the delayed input is what bypass returns.
        bypassFade.pushDry (view, latency);

        if (! circuitReady)
        {
            bypassFade.substituteDry (view, state.bypassed);
            continue;
        }

        auto& set = outgoing ? fadingCircuits : circuits;
        auto* filters = outgoing && fadingOwnOversampler ? fadingOversampler.get() : oversampler.get();

        auto run = [&] (juce::dsp::AudioBlock<float>& block, int samples)
        {
            auto* left = block.getChannelPointer (0);
            auto* right = block.getNumChannels() > 1 ? block.getChannelPointer (1) : nullptr;

            if (state.mode == ChannelMode::Stereo)
                runStereo (set, ! outgoing, left, right, samples, state);
            else
                runMono (set, ! outgoing, left, right, samples, state);
        };

        if (filters == nullptr)
        {
            run (piece, count);
        }
        else
        {
            auto upsampled = filters->processSamplesUp (piece);
            run (upsampled, static_cast<int> (upsampled.getNumSamples()));
            filters->processSamplesDown (piece);
        }

        // Before the cabinet, so its tail continues smoothly through the dip.
        if (outgoing || fadeInRemaining > 0)
            applyRebuildFade (view, outgoing);

        // The cabinet last, at the host rate. Runs even when bypassed, so it never replays
        // stale input when re-engaged.
        if (cabinetActive.load (std::memory_order_relaxed))
            cabinet.process (view);

        // Crossfade to the dry signal (cabinet included in what is bypassed).
        bypassFade.mix (view, state.bypassed);
    }
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
juce::ValueTree PluginProcessor::createDocument()
{
    // Parameters and drawing together.
    juce::ValueTree document (documentType);
    document.appendChild (apvts.copyState(), nullptr);
    document.appendChild (schematic.toValueTree(), nullptr);

    // Always written (unlike the build options, omitted at their defaults).
    document.setProperty (documentVersionProperty, getBuildVersion(), nullptr);

    if (buildOptions.fastMath)
        document.setProperty ("fastMath", true, nullptr);

    if (oversamplingFactor != 1)
        document.setProperty ("oversampling", oversamplingFactor, nullptr);

    return document;
}

bool PluginProcessor::restoreDocument (const juce::ValueTree& document)
{
    // Every load route goes through here; anything that isn't ours is refused.
    if (! document.hasType (documentType))
        return false;

    const auto drawing = document.getChildWithName ("SCHEMATIC");

    if (! drawing.isValid())
        return false;

    // Older sheets may carry removed options (capacitance, Early effect, Newton
    // prediction switches); they are ignored.
    buildOptions.fastMath = document.getProperty ("fastMath", false);

    // Set before rebuilding, so the circuits are built at the right rate.
    const int documentFactor = normaliseOversamplingFactor (static_cast<int> (document.getProperty ("oversampling", 1)));
    auto documentFilters = makeOversampler (documentFactor);

    if (const auto parameters = document.getChildWithName (apvts.state.getType()); parameters.isValid())
        apvts.replaceState (parameters);

    schematic.restoreFromValueTree (drawing);

    // Kept aside and restored after this rebuild, so the editor's next rebuild still
    // reports them.
    auto pending = schematic.takeLoadWarnings();

    // Only a document from a newer build is flagged; the editor reports it (see
    // PluginEditor::schematicChangedExternally()).
    const auto saved = document.getProperty (documentVersionProperty, "").toString();
    documentFromNewerBuild = saved.isNotEmpty() && compareVersions (saved, getBuildVersion()) > 0;

    // Through the rebuild fade, so restoring while playing doesn't pop.
    rebuildAt (documentFactor, &documentFilters);

    for (const auto& warning : pending)
        schematic.addLoadWarning (warning);

    // The restored parameters are the truth; make the parts agree.
    adoptControlPositions();

    // Tell an open editor to reload.
    if (onSchematicReplaced != nullptr)
        onSchematicReplaced();

    return true;
}

void PluginProcessor::adoptControlPositions()
{
    const auto count = juce::jmin (static_cast<int> (controls.size()), maxLiveControls);

    for (int i = 0; i < count; ++i)
    {
        auto* parameter = apvts.getRawParameterValue (getControlParameterId (i));

        if (parameter == nullptr)
            continue;

        // Every part the control works, so ganged parts keep the same position.
        for (const auto elementId : controls[static_cast<size_t> (i)].elementIds)
            if (auto* element = schematic.findElement (elementId))
                element->setControlPosition (parameter->load());
    }
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto document = createDocument();
    document.setProperty ("editorWidth", editorWidth.load(), nullptr);
    document.setProperty ("editorHeight", editorHeight.load(), nullptr);

    document.setProperty ("presetName", presetName, nullptr);
    document.setProperty ("presetIsFactory", presetIsFactory, nullptr);
    document.setProperty ("presetModified", presetModified, nullptr);
    document.setProperty ("presetFile", presetFile.getFullPathName(), nullptr);

    if (auto xml = document.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto document = juce::ValueTree::fromXml (*xml);

        if (document.hasProperty ("editorWidth"))
        {
            editorWidth = static_cast<int> (document["editorWidth"]);
            editorHeight = static_cast<int> (document["editorHeight"]);
        }

        if (document.hasProperty ("presetName"))
        {
            presetName = document["presetName"].toString();
            presetIsFactory = static_cast<bool> (document["presetIsFactory"]);
            presetModified = static_cast<bool> (document["presetModified"]);

            const auto path = document["presetFile"].toString();
            presetFile = path.isEmpty() ? juce::File{} : juce::File (path);
        }

        restoreDocument (document);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
