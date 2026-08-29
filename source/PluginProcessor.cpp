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

    // Every value processBlock reads, resolved here so the audio thread never
    // builds a parameter id. See the declarations for why that mattered.
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

    // Build it now, at the default rate, rather than waiting for
    // prepareToPlay(). An editor opened before playback starts would otherwise
    // find no circuit and show no knobs.
    rebuild();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Not part of any circuit: how hard the signal hits the input node. One volt
    // of circuit voltage per unit of full scale, so a 0 dBFS sine puts 1 V peak
    // on the input net and the numbers in the editor mean what they say.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "input", 1 },
        "Input (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.1f),
        0.0f));

    // The generic knob pool. Parameters have to be declared once, at
    // construction, but a schematic's controls appear and disappear as it is
    // drawn -- so a fixed set is declared here and the live controls are mapped
    // onto it in order. That is what makes a drawn pot automatable by the host.
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

    // Stereo runs a circuit per channel. The mono settings run one and copy its
    // output to both, which halves the cost of the simulation -- worth having,
    // since most of what gets drawn here is a pedal or a preamp that was mono in
    // the first place.
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
    // The same string the VERSION file carries, handed down by CMake.
    return VERSION;
}

int PluginProcessor::compareVersions (const juce::String& a, const juce::String& b)
{
    const auto left = juce::StringArray::fromTokens (a, ".", "");
    const auto right = juce::StringArray::fromTokens (b, ".", "");

    for (int i = 0; i < juce::jmax (left.size(), right.size()); ++i)
    {
        // Absent components read as zero, so 1.0 and 1.0.0 are one build. And
        // getIntValue() stops at the first non-digit, which is what makes a
        // suffixed component like "3-rc1" compare as 3 rather than as nothing.
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
    // fromUTF8, not the implicit conversion: juce::String(const char*) decodes
    // as ASCII, one byte to one codepoint, so the two UTF-8 bytes behind the e
    // in "Céline" arrive as "Ã©" and that is the name the host displays. JUCE
    // asserts on it in a debug build, which is the only reason it isn't silent.
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

    // Named rather than empty. There is one program and nothing selects between
    // them, so the name is never shown -- but Steinberg's VST3 validator fails a
    // plugin whose only program has no name, and a plugin that fails validation
    // is one some hosts decline to load at all.
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
// Building and swapping
//==============================================================================

SchematicModel::BuildResult PluginProcessor::rebuild()
{
    const auto channelCount = juce::jmax (1, getTotalNumOutputChannels());

    // The expensive part -- stamping, factorising, and a Newton solve for the
    // bias point of every channel -- happens here, with no lock held and the
    // previous circuit still running.
    // The circuit runs at the oversampled rate, which is the only place the
    // factor enters the engine at all -- everything downstream just sees a
    // higher sample rate.
    auto result = SchematicModel::buildCircuits (schematic,
                                                 currentSampleRate * oversamplingFactor,
                                                 channelCount, buildOptions);

    // A load can substitute a model it could not find, and that has to reach the
    // console -- which listens to the build, and a load is always followed by
    // one. Drained rather than copied, since the substitution is a fact about
    // the load and not about the sheet. Prepended, because it explains anything
    // the build goes on to say about that part.
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
        return result; // leave whatever was running alone

    // Copied here, off-lock, because a copy allocates: every LiveControl owns a
    // vector and a string, and so does every probe. The lock below is for
    // swapping pointers, and these are the only reason it ever held anything
    // else. `result` keeps its own copies -- the caller reads them for the UI.
    auto incomingControls = result.controls;
    auto incomingProbes = result.probes;
    auto incomingCurrentProbes = result.currentProbes;

    // Holds whatever was running until the lock is released. Destroying a
    // Circuit frees a dozen vectors and two solvers, and doing that inside the
    // lock is the same mistake as building inside it: every microsecond held is
    // another chance the audio thread gives up and passes a block dry.
    std::vector<std::unique_ptr<Circuit>> outgoingCircuits;

    // And this is the only part the audio thread has to be kept out of: four
    // swaps, no allocation, no destructor.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);
        outgoingCircuits.swap (circuits);
        circuits.swap (result.circuits);
        controls.swap (incomingControls);

        // In step with the circuits, and for the same reason: a probe's node
        // names only mean anything for the circuit they were extracted from.
        probes.swap (incomingProbes);
        currentProbes.swap (incomingCurrentProbes);
    }

    // outgoingCircuits and the three incoming* locals now hold the old contents,
    // and destruct on the way out of this function -- off-lock, which was the
    // whole point.

    prepareScopes();

    // A sheet can draw more pots than there are parameters to attach them to.
    // Saying which ones went, and how many, beats a knob that silently never
    // appears -- that reads as a bug in the drawing.
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

    // The cabinet comes along with the sheet. It needs no rebuild of its own,
    // but every route that replaces the drawing -- a preset load, the host
    // restoring a session, prepareToPlay -- lands here, and a patch whose cab
    // only appeared after you pressed a button would be a patch that loaded
    // wrong.
    if (const auto problem = refreshCabinet(); problem.isNotEmpty())
        result.add (SchematicModel::Diagnostic::Severity::Warning, problem, "Cab");

    // Hand the controls back to the caller for the UI, having moved the circuits
    // out from under it.
    result.circuits.clear();
    return result;
}

std::vector<SchematicModel::LiveControl> PluginProcessor::getLiveControls() const
{
    const juce::SpinLock::ScopedLockType lock (circuitLock);
    return controls;
}

//==============================================================================
void PluginProcessor::prepareOversampler()
{
    // Built here, off-lock, exactly like rebuild() builds its circuits: this
    // allocates and designs filters, and none of that may happen while the audio
    // thread is held up.
    std::unique_ptr<juce::dsp::Oversampling<float>> prepared;
    int latency = 0;

    if (oversamplingFactor > 1)
    {
        // Polyphase IIR halfband: much cheaper than an equiripple FIR and the
        // phase distortion it trades for that is irrelevant in front of a guitar
        // amp, which is a minimum-phase device from end to end anyway.
        const auto stages = oversamplingFactor == 4 ? 2 : 1;

        prepared = std::make_unique<juce::dsp::Oversampling<float>> (
            2, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);

        prepared->initProcessing (static_cast<size_t> (preparedBlockSize));
        prepared->reset();

        latency = juce::roundToInt (prepared->getLatencyInSamples());
    }

    // The swap itself is the only part the audio thread has to be kept out of.
    // Assigning straight over `oversampler` without this destroyed the object
    // the audio thread was in the middle of calling -- a use-after-free that
    // crashed Ableton the moment the oversampling factor was changed during
    // playback. `prepared` leaves holding the old one, which then destructs
    // below, off-lock.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);
        prepared.swap (oversampler);
    }

    // After the swap, so the host is never told about a latency the running
    // filter chain doesn't have.
    setLatencySamples (latency);
}

// 1, 2 or 4, never 3: the filter chain only comes in powers of two, so a 3 --
// reachable from a hand-edited document -- would prepare the circuits at one
// rate and the halfbands at another. Rounded down: cheaper, and a hand-written
// 3 was almost certainly meant to be one of its neighbours.
static int normaliseOversamplingFactor (int factor) noexcept
{
    factor = juce::jlimit (1, 4, factor);
    return factor == 3 ? 2 : factor;
}

SchematicModel::BuildResult PluginProcessor::setOversamplingFactor (int factor)
{
    oversamplingFactor = normaliseOversamplingFactor (factor);
    prepareOversampler();

    // The circuits have to be re-prepared: the DK discretisation is built around
    // a fixed timestep, so running four times as fast is a different matrix.
    return rebuild();
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = juce::jmax (1, samplesPerBlock);

    // Prepared at the *host's* rate, not the oversampled one -- see the member.
    cabinet.prepare ({ sampleRate,
                       static_cast<juce::uint32> (preparedBlockSize),
                       static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels())) });

    // prepare() resets the convolver, and the impulse response has to be
    // resampled to whatever rate we have just been given anyway. Forgetting what
    // was loaded is what makes refreshCabinet() reload rather than take its
    // "already have this one" short cut.
    loadedCabinetFile = juce::File{};

    prepareOversampler();
    rebuild();

    // After rebuild(), which is what fills `probes` -- and again here because
    // the column width follows the sample rate we have just been handed.
    prepareScopes();
}

void PluginProcessor::prepareScopes()
{
    // Reads `probes` and writes state the audio thread reads between samples, so
    // it takes the same lock the circuits are swapped under -- and the audio
    // thread only *tries* that, so the cost is one dry block.
    //
    // Callers must not already hold it: juce::SpinLock is not recursive.
    const juce::SpinLock::ScopedLockType lock (circuitLock);

    // The window is a duration, so the number of samples behind each column
    // follows whatever rate the circuit is actually running at -- which is the
    // oversampled one, since that is the loop the readings are taken in. Without
    // this the picture would cover four times less time at 4x than at 1x, and
    // appear to speed up when you changed a setting that has nothing to do with
    // it.
    const auto rate = currentSampleRate * oversamplingFactor;

    const auto count = juce::jmin (static_cast<int> (probes.size()), maxScopes);

    for (int i = 0; i < maxScopes; ++i)
    {
        // Per scope, from that probe's own window. A slot with no probe still
        // gets a sane figure rather than a zero, since a divisor of nought
        // reached from the audio thread is not a thing to leave lying about.
        const auto seconds = i < count ? probes[static_cast<size_t> (i)].windowSeconds
                                       : scopeWindowSeconds;

        samplesPerScopeColumn[static_cast<size_t> (i)] = juce::jmax (
            1, juce::roundToInt (rate * seconds / ScopeTrace::columns));
    }

    // The current readout keeps the default window whatever the scopes are set
    // to: it is not one of them, and a part's mean current has no reason to
    // retime itself because a probe elsewhere on the sheet was zoomed in.
    samplesPerCurrentWindow = juce::jmax (
        1, juce::roundToInt (rate * scopeWindowSeconds));

    for (int i = 0; i < maxScopes; ++i)
    {
        auto& trace = scopeTraces[static_cast<size_t> (i)];
        const bool used = i < count;

        // Always cleared, used or not: a probe that has just been moved
        // somewhere else must not spend a second still showing where it used to
        // be, and a slot that has stopped being used must not leave a picture
        // behind for the next scope to inherit.
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

        // Published here, under the lock, so getScopeTrace() can match a slot to
        // a part without touching `probes` at all. See the member.
        scopeElementIds[static_cast<size_t> (i)].store (
            used ? probes[static_cast<size_t> (i)].elementId : 0, std::memory_order_relaxed);
    }

    // The inspected part's nets have just been re-extracted too, so whatever was
    // accumulating belonged to the old circuit.
    currentAccumulator = {};
    inspectedLive.store (false, std::memory_order_relaxed);
}

void PluginProcessor::refreshScopeTiming()
{
    // The probes carry each scope's timebase, so they are what has to be brought
    // up to date before prepareScopes reads them. Node names are untouched:
    // nothing about the topology changed, which is why this is not a rebuild.
    //
    // Under the lock, because this writes into the vector the audio thread reads
    // node names from. The fields do not overlap today, but that rests on the
    // current layout of ScopeProbe.
    {
        const juce::SpinLock::ScopedLockType lock (circuitLock);

        for (auto& probe : probes)
            if (const auto* element = schematic.findElement (probe.elementId))
                probe.windowSeconds = element->scopeSeconds;
    }

    // Outside the lock: prepareScopes() takes it itself, and SpinLock is not
    // recursive.
    prepareScopes();
}

void PluginProcessor::setInspectedElement (int elementId) noexcept
{
    if (inspectedElement.exchange (elementId, std::memory_order_relaxed) == elementId)
        return;

    // Cleared on the way in rather than on the way out: the readout must not
    // spend a window showing the part you just stopped looking at, which at a
    // 40 ms average is long enough to read and believe.
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

    // Ohm's law across the two nets the part landed on. This is why only
    // resistances are in the list: for anything else the terminal voltages are
    // not enough to say what the current is.
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
    // Matched against the atomic mirror rather than `probes`, which the message
    // thread mutates and the audio thread reads. This runs from the editor's
    // paint path several times a frame, so taking the lock here would mean
    // handing the audio thread a fresh chance to drop a block on every repaint.
    // 0 never matches a real element, so an unused slot declines on its own.
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

        // Differential, so a probe reads across a part as readily as against
        // ground -- wire the reference pin to ground and it is the same thing.
        //
        // By index, not by name: the name form hashes a juce::String and probes
        // a map, and this runs twice per scope per sample.
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

        // Published after the column it describes, so a reader that sees the new
        // index is looking at a column that has already been written.
        trace.writeColumn.store ((column + 1) % ScopeTrace::columns, std::memory_order_release);

        // The two numbers, refreshed once a column rather than once a sample.
        // Averaged over the whole visible window rather than over this column,
        // or the "DC" reading would follow the waveform.
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

juce::String PluginProcessor::refreshCabinet()
{
    // The first Output terminal that has anything to say. A sheet can hold
    // several -- unwired Output symbols all name one net, which is the whole
    // point of terminals naming their net -- but there is one output path, so
    // the first one drawn owns the cabinet and the rest are ignored.
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

        // Kept switched on deliberately. The setting is the user's and the file
        // may well come back -- a shared sheet, a moved folder, an unmounted
        // drive -- so turning it off for them would lose the only record of
        // which cabinet the patch meant.
        return "Cab impulse response not found: " + file.getFullPathName()
             + " -- the switch is still on, and audio passes through without it.";
    }

    if (file == loadedCabinetFile)
    {
        cabinetActive = true;
        return {};
    }

    // Checked before loading rather than after, because loadImpulseResponse
    // hands the file to a background thread and returns nothing: a .wav that is
    // not really a .wav would otherwise fail silently and leave the convolver
    // holding whatever was in it before.
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            cabinetActive = false;
            loadedCabinetFile = juce::File{};
            return file.getFileName() + " is not an audio file this build can read.";
        }
    }

    // The cap is a duration, not a buffer size -- 2048 taps at 48 kHz is 42.7 ms
    // of cabinet, and it has to stay 42.7 ms at any other rate. JUCE applies
    // `size` after its own resampling, so scaling it here is what keeps the
    // filter the same filter. See cabinetImpulseSamples.
    const auto maxSamples = static_cast<size_t> (juce::jmax (
        1, juce::roundToInt (cabinetImpulseSamples * currentSampleRate / cabinetReferenceRate)));

    cabinet.loadImpulseResponse (file,
                                 juce::dsp::Convolution::Stereo::yes,
                                 // No trimming: a cabinet impulse response is
                                 // normally *already* the length it wants to be,
                                 // and silently cropping its head would move the
                                 // whole filter.
                                 juce::dsp::Convolution::Trim::no,
                                 maxSamples,
                                 // Normalised, because an arbitrary file's level
                                 // is arbitrary and an un-normalised one can
                                 // arrive twenty dB hot.
                                 juce::dsp::Convolution::Normalise::yes);

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

void PluginProcessor::runStereo (float* left, float* right, int count, const BlockState& state)
{
    float* channels[2] = { left, right };

    for (int channel = 0; channel < 2; ++channel)
    {
        if (channels[channel] == nullptr || static_cast<size_t> (channel) >= circuits.size())
            continue;

        auto& circuit = *circuits[static_cast<size_t> (channel)];
        auto* samples = channels[channel];
        applyControls (circuit, state);

        // Everything measured is measured on the left only: both sides run the
        // same drawing, so twice would be the same numbers at twice the cost --
        // and for the running averages it would count each window twice.
        const bool measured = channel == 0;
        const bool probed = measured && ! probes.empty();

        for (int s = 0; s < count; ++s)
        {
            const float dry = samples[s];
            const float wet = circuit.process (dry * state.inputGain) * state.outputGain;

            if (probed)
                sampleScopes (circuit);

            if (measured)
                sampleInspectedCurrent (circuit);

            // The circuit runs even when bypassed and the result is thrown away,
            // so its capacitors do not hold stale charge and re-engaging does
            // not thump.
            samples[s] = state.bypassed ? dry : wet;
        }
    }
}

void PluginProcessor::runMono (float* left, float* right, int count, const BlockState& state)
{
    auto& circuit = *circuits[0];
    applyControls (circuit, state);

    const bool haveTwoInputs = right != nullptr;

    for (int s = 0; s < count; ++s)
    {
        // Both sides are read before either is written, since the mono result
        // goes back over the top of them.
        const float dryLeft = left[s];
        const float dryRight = haveTwoInputs ? right[s] : dryLeft;

        const float source = state.mode == ChannelMode::MonoLeft  ? dryLeft
                           : state.mode == ChannelMode::MonoRight ? dryRight
                                                                  : 0.5f * (dryLeft + dryRight);

        const float wet = circuit.process (source * state.inputGain) * state.outputGain;

        if (! probes.empty())
            sampleScopes (circuit);

        sampleInspectedCurrent (circuit);

        // Bypass stays a true bypass: whatever arrived, untouched, stereo image
        // and all. Only the engaged path collapses to mono.
        if (! state.bypassed)
        {
            left[s] = wet;

            if (right != nullptr)
                right[s] = wet;
        }
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

    // Never wait. A rebuild holds the lock only long enough to move a few
    // pointers, so failing to get it costs one dry block -- better than a stall
    // and better than silence.
    const juce::SpinLock::ScopedTryLockType lock (circuitLock);

    if (! lock.isLocked() || circuits.empty())
        return;

    BlockState state;
    state.mode = static_cast<ChannelMode> (channelModeParameter->getIndex());
    state.bypassed = bypassValue->load() > 0.5f;
    state.inputGain = juce::Decibels::decibelsToGain (inputGainValue->load());
    state.outputGain = juce::Decibels::decibelsToGain (outputGainValue->load());

    // Read once per block: moving a knob re-stamps and re-factorises the matrix,
    // and costs nothing when nothing moved, Circuit only dirtying on a change.
    //
    // Through the cached cells rather than by id: the ids were built here, and
    // building one allocates.
    for (int i = 0; i < maxLiveControls; ++i)
        state.controls[i] = controlValues[(size_t) i]->load();

    state.controlCount = juce::jmin (static_cast<int> (controls.size()), maxLiveControls);

    // Coming back to stereo, the right-hand circuit has been idle and its
    // capacitors still hold the charge they had when it stopped. Clearing costs
    // one bias solve, on a user action, and saves a thump.
    if (state.mode != lastChannelMode)
    {
        if (state.mode == ChannelMode::Stereo)
            for (size_t c = 1; c < circuits.size(); ++c)
                circuits[c]->reset();

        lastChannelMode = state.mode;
    }

    // Everything below was sized in prepareToPlay for the block the host
    // declared -- the oversampler's buffers and the convolver's alike -- so a
    // bigger one is processed in pieces.
    //
    // In pieces rather than at 1x, which is what this used to do: running the
    // circuit at the host's rate when it was discretised for four times that
    // retunes every capacitor in the drawing for the length of the block.
    const auto numSamples = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> whole (buffer);

    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const int count = juce::jmin (preparedBlockSize, numSamples - start);
        auto piece = whole.getSubBlock (static_cast<size_t> (start), static_cast<size_t> (count));

        auto run = [&] (juce::dsp::AudioBlock<float>& block, int samples)
        {
            auto* left = block.getChannelPointer (0);
            auto* right = block.getNumChannels() > 1 ? block.getChannelPointer (1) : nullptr;

            if (state.mode == ChannelMode::Stereo)
                runStereo (left, right, samples, state);
            else
                runMono (left, right, samples, state);
        };

        if (oversampler == nullptr)
        {
            run (piece, count);
        }
        else
        {
            // The dry signal rides through the halfband filters alongside the
            // wet one, so a bypassed signal comes out lined up with the latency
            // the host was told about instead of arriving early.
            auto upsampled = oversampler->processSamplesUp (piece);
            run (upsampled, static_cast<int> (upsampled.getNumSamples()));
            oversampler->processSamplesDown (piece);
        }

        // The cabinet last, at the host's own rate -- see the member for why it
        // sits outside the oversampled section. Skipped when bypassed, because
        // bypass is a true bypass and a cab is as much part of the sound as the
        // circuit is.
        if (! state.bypassed && cabinetActive.load (std::memory_order_relaxed))
        {
            const juce::dsp::ProcessContextReplacing<float> context (piece);
            cabinet.process (context);
        }
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
    // Parameters and the drawing, together: reopening a session with the knobs
    // restored but the circuit gone would be worse than saving nothing.
    juce::ValueTree document (documentType);
    document.appendChild (apvts.copyState(), nullptr);
    document.appendChild (schematic.toValueTree(), nullptr);

    // Always written, unlike the build options below. Those are omitted when
    // they hold their default so that an unchanged sheet stays byte-identical
    // to one saved before the option existed; this one has no default worth
    // preserving and is only useful when it is there.
    document.setProperty (documentVersionProperty, getBuildVersion(), nullptr);

    // Written only when switched off, so an accurate sheet stays byte-identical
    // to one saved before the option existed.
    if (! buildOptions.interelectrodeCapacitance)
        document.setProperty ("interelectrodeCapacitance", false, nullptr);

    if (buildOptions.fastMath)
        document.setProperty ("fastMath", true, nullptr);

    if (buildOptions.predictNewtonSeed)
        document.setProperty ("predictNewtonSeed", true, nullptr);

    // Same rule as the valve capacitance above, and for the same reason: these
    // are accurate by default, so they are written only when switched off and
    // an accurate sheet stays byte-identical to one saved before they existed.
    if (! buildOptions.transistorJunctionCapacitance)
        document.setProperty ("transistorJunctionCapacitance", false, nullptr);

    if (! buildOptions.transistorEarlyEffect)
        document.setProperty ("transistorEarlyEffect", false, nullptr);

    if (oversamplingFactor != 1)
        document.setProperty ("oversampling", oversamplingFactor, nullptr);

    return document;
}

bool PluginProcessor::restoreDocument (const juce::ValueTree& document)
{
    // The one gate every route in shares -- host state, Load, and the preset
    // menu alike -- so a file that isn't ours is refused identically however it
    // was picked, rather than half-loaded into an empty sheet.
    if (! document.hasType (documentType))
        return false;

    const auto drawing = document.getChildWithName ("SCHEMATIC");

    if (! drawing.isValid())
        return false;

    // Absent means accurate: files written before the option existed, and every
    // sheet that never turned it off, land on the full simulation.
    buildOptions.interelectrodeCapacitance = document.getProperty ("interelectrodeCapacitance", true);
    buildOptions.fastMath = document.getProperty ("fastMath", false);
    buildOptions.predictNewtonSeed = document.getProperty ("predictNewtonSeed", false);

    // Absent means accurate, as above: a sheet drawn before these existed opens
    // on the full model rather than being frozen at the older, simpler one.
    buildOptions.transistorJunctionCapacitance = document.getProperty ("transistorJunctionCapacitance", true);
    buildOptions.transistorEarlyEffect = document.getProperty ("transistorEarlyEffect", true);

    // Set before the rebuild below, so the circuits are prepared at the right
    // rate first time rather than being built and immediately rebuilt.
    oversamplingFactor = normaliseOversamplingFactor (static_cast<int> (document.getProperty ("oversampling", 1)));
    prepareOversampler();

    if (const auto parameters = document.getChildWithName (apvts.state.getType()); parameters.isValid())
        apvts.replaceState (parameters);

    schematic.restoreFromValueTree (drawing);

    // Held across the rebuild below and put back afterwards.
    //
    // rebuild() drains these into its BuildResult, and the one it is about to
    // make is discarded -- restoreDocument returns a bool. So anything the load
    // noticed would be consumed here and never reach the console, which is fed
    // by the editor's *own* rebuild a moment later. Taking them out of the way
    // and putting them back is what leaves that rebuild something to report.
    auto pending = schematic.takeLoadWarnings();

    // Only the future direction is worth a word. A sheet with no version
    // predates this field, so it is older, and an older sheet loads correctly by
    // construction. A newer one may name types, models or options this build has
    // never heard of, each degrading quietly on its own -- together, a sheet
    // that opens and plays while not being the circuit that was drawn.
    //
    // Recorded here, raised by the editor as soon as the import lands; see
    // PluginEditor::schematicChangedExternally().
    const auto saved = document.getProperty (documentVersionProperty, "").toString();
    documentFromNewerBuild = saved.isNotEmpty() && compareVersions (saved, getBuildVersion()) > 0;

    rebuild();

    for (const auto& warning : pending)
        schematic.addLoadWarning (warning);

    // The document carried both the drawing and the knob positions, and for a
    // sheet written before parts remembered their own they carried only the
    // knobs. Either way the restored parameters are the truth and the parts are
    // made to agree, which also means the editor's own sync has nothing left to
    // do and cannot undo this.
    adoptControlPositions();

    // An open editor is still showing the old drawing. Announced rather than
    // reached for: the processor has no business knowing what an editor is, and
    // the editor is the only thing that knows how to catch up.
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

        // Every part the control works, not just the one it is named after: a
        // ganged pair is one knob and both halves have to remember the same
        // position, or the next build seeds from whichever was drawn first.
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

        restoreDocument (document);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
