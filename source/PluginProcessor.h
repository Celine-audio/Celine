#pragma once

#include "dsp/BypassFade.h"
#include "dsp/InputFilePlayer.h"
#include "dsp/PartitionedConvolver.h"
#include "Schematic/SchematicBuilder.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>

//==============================================================================
/** The plugin: the schematic on screen is the signal path.

    Topology changes (parts, wires, values) need rebuild(): the matrix is rebuilt and a
    new bias point solved, on the message thread. Pots and switches stay live: the
    engine re-stamps their resistance per block without allocating. */
class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** The plugin's own bypass, so the host uses it instead of adding another. */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==========================================================================
    // The schematic, and turning it into something that runs
    //==========================================================================

    /** The drawing. Message thread only. */
    SchematicModel::Schematic& getSchematic() noexcept { return schematic; }
    const SchematicModel::Schematic& getSchematic() const noexcept { return schematic; }

    /** Builds the schematic and swaps it in. All the work happens before the swap, so the
        audio thread is locked out only to move pointers; a failed build keeps the previous
        circuit running. Message thread; allocates.

        The swap dips through silence: the old circuit fades out over rebuildFadeSeconds,
        the new one fades in. Only one circuit runs at a time, and a change of
        oversampling factor swaps its filters at the silent point. */
    SchematicModel::BuildResult rebuild();

    /** Frees the circuit and filters left by a finished fade-out (the audio thread can't).
        Cheap when there is nothing to do; called from the editor's timer. Message thread. */
    void releaseFadedCircuits();

    /** Length of each half of the rebuild dip. */
    static constexpr double rebuildFadeSeconds = BypassFade::fadeSeconds;

    /** Build settings, read at rebuild() time. Saved in the document. */
    SchematicModel::BuildOptions buildOptions;

    /** See wasDocumentFromNewerBuild(). */
    bool documentFromNewerBuild = false;

    /** Oversampling factor: one of `oversamplingFactors`. Changing it rebuilds the
        circuits, since the discretisation depends on the timestep. */
    int oversamplingFactor = 1;

    /** The factors offered: one halfband stage per doubling. */
    static constexpr std::array<int, 5> oversamplingFactors { 1, 2, 4, 8, 16 };
    static constexpr int maximumOversamplingFactor = oversamplingFactors.back();

    /** Sets the factor and rebuilds. Message thread; returns the build result. */
    SchematicModel::BuildResult setOversamplingFactor (int factor);

    /** Loads the impulse response the Output terminal names; returns an error message or
        empty. Message thread. Also called by rebuild(). A missing file is a warning and
        the audio runs without the cabinet. */
    juce::String refreshCabinet();

    /** Longest impulse response, in samples at 48 kHz; scaled with the session rate so the
        duration stays the same. Longer ones are truncated. */
    static constexpr int cabinetImpulseSamples = 2048;
    static constexpr double cabinetReferenceRate = 48000.0;

    /** The most taps ever loaded, at the highest expected rate. The convolver is sized for
        this once, in prepareToPlay. */
    static constexpr int maximumCabinetSamples =
        (int) (cabinetImpulseSamples * 192000.0 / cabinetReferenceRate);

    /** The file player that can replace the host input. Not part of the document. Played
        before everything, including bypass's dry copy; the input gain still applies. */
    InputFilePlayer& getInputFilePlayer() noexcept { return inputFile; }

    //==========================================================================
    /** One scope's picture: a scrolling min/max envelope, written by the audio thread and
        read by the UI. Lock-free, fixed storage. */
    struct ScopeTrace
    {
        /** Number of columns. */
        static constexpr int columns = 128;

        std::atomic<float> minimum[columns] {};
        std::atomic<float> maximum[columns] {};

        /** The column being filled; everything to its left is complete. */
        std::atomic<int> writeColumn { 0 };

        /** DC level and peak-to-peak swing over the window. */
        std::atomic<float> dcAverage { 0.0f };
        std::atomic<float> peakToPeak { 0.0f };

        /** True once the trace has a circuit behind it. */
        std::atomic<bool> live { false };
    };

    /** Number of scope slots. */
    static constexpr int maxScopes = 8;

    /** The trace of the scope with this id, or null. Message thread. */
    const ScopeTrace* getScopeTrace (int elementId) const noexcept;

    /** Re-reads the scopes' time bases and clears their pictures. Message thread. */
    void refreshScopeTiming();

    /** Sets the part whose current is measured, or 0 to stop. */
    void setInspectedElement (int elementId) noexcept;

    /** Mean current, mean power and peak current of the inspected part. False when it
        isn't in the built circuit or its current can't be derived (see CurrentProbe). */
    bool readInspectedCurrent (float& current, float& power, float& peak) const noexcept;

    /** Samples the live circuits have held since this processor was made: samples Newton
        could not settle, each repeating the one before (see Circuit::process). Only ever
        grows, across rebuilds too, so the editor can tell new ones from old by comparing
        it with what it last saw. Written by the audio thread. */
    juce::int64 getHeldSampleTotal() const noexcept { return heldSampleTotal.load (std::memory_order_relaxed); }

    /** Time span of a scope picture. */
    static constexpr double scopeWindowSeconds = 0.04;

    /** Length of the loaded impulse response in samples, or 0. */
    int getCabinetLength() const { return cabinetLength.load(); }

    /** Writes the knob parameters back onto their parts. The parameters win wherever the
        two disagree. Message thread. */
    void adoptControlPositions();

    /** The running circuit's live controls, in strip order. Message thread. */
    std::vector<SchematicModel::LiveControl> getLiveControls() const;

    //==========================================================================
    // The whole circuit as one document
    //==========================================================================

    /** The drawing and knob positions as one tree: what the host saves and what a preset
        file contains. */
    juce::ValueTree createDocument();

    /** The editor's last size. Saved in the plugin state, not in documents. Atomic: hosts
        may save from any thread. */
    std::atomic<int> editorWidth { 1060 };
    std::atomic<int> editorHeight { 700 };

    /** Whether the palette is folded. Kept across editor rebuilds, not saved. Message
        thread. */
    bool paletteCollapsed = false;

    /** The preset the sheet came from, as the toolbar shows it; empty for none.
        `presetIsFactory` marks a shipped example, which Save can't overwrite. Saved in the
        plugin state, not in documents. */
    juce::String presetName;
    bool presetIsFactory = false;
    bool presetModified = false;
    juce::File presetFile;

    /** Loads a document, rebuilds and notifies an open editor. False if the tree isn't
        ours. */
    bool restoreDocument(const juce::ValueTree& document);

    /** Extension for saved circuits. */
    static constexpr const char* circuitFileExtension = ".celsch";

    /** The ValueTree type of a saved circuit, for both the host state and .celsch files. */
    static constexpr const char* documentType = "CELINESCHEMATIC";

    /** Names the build that wrote a document. Absent in older documents. */
    static constexpr const char* documentVersionProperty = "celineVersion";

    /** Whether the last restored document came from a newer build. */
    bool wasDocumentFromNewerBuild() const noexcept { return documentFromNewerBuild; }

    /** This build's version, written into saved documents. */
    static juce::String getBuildVersion();

    /** Compares dotted versions numerically: negative if `a` is older, 0 if equal,
        positive if newer. Missing components count as zero. */
    static int compareVersions (const juce::String& a, const juce::String& b);

    /** Stereo runs one circuit per channel; the mono modes run one circuit and copy its
        output to both. */
    enum class ChannelMode
    {
        Stereo = 0,
        MonoLeft,
        MonoRight,
        MonoSum,
    };

    /** Fired when restoreDocument() replaced the drawing. May come from a non-UI thread. */
    std::function<void()> onSchematicReplaced;

    /** Number of generic knob parameters. A sheet's pots and switches map onto these in
        order; rebuild() reports any that don't fit. Raising it is safe, lowering it breaks
        saved sessions. */
    static constexpr int maxLiveControls = 16;

    /** Upper bound on the oversampler's latency at any factor; the bypass dry delay is
        sized for it once. */
    static constexpr int maximumOversamplingLatency = 64;

    /** Parameter id for live control `index`. */
    static juce::String getControlParameterId (int index) { return "knob" + juce::String (index + 1); }

    /** Level of a full-scale sine at 0 dB gain, in dBu: 1 V peak per unit of full scale,
        so -10 log10(1.2), about -0.79. The same at input and output. */
    static double fullScaleSineDbu() noexcept { return -10.0 * std::log10 (1.2); }

    /** The INPUT knob as a calibration level: the dBu a full-scale sine reaches at the
        circuit (NAM's `input_level_dbu`). */
    static double inputGainToDbu (double gainDb) noexcept { return fullScaleSineDbu() + gainDb; }

    /** The OUTPUT knob as a calibration level: the circuit output in dBu that reaches full
        scale (NAM's `output_level_dbu`). */
    static double outputGainToDbu (double gainDb) noexcept { return fullScaleSineDbu() - gainDb; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Recreated when the factor or block size changes. Null at 1x. */
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int preparedBlockSize = 512;

    /** A filter chain for `factor`, ready to install, and its latency. */
    struct PreparedOversampler
    {
        std::unique_ptr<juce::dsp::Oversampling<float>> filters;
        int latency = 0;
    };

    PreparedOversampler makeOversampler (int factor) const;

    /** Installs `fresh` immediately, without a fade. For prepareToPlay, or a factor whose
        sheet won't build. */
    void installOversamplerNow (PreparedOversampler& fresh, int factor);

    /** Builds at `factor` and swaps in through the rebuild dip, with `fresh` filters when
        the factor changed. */
    SchematicModel::BuildResult rebuildAt (int factor, PreparedOversampler* fresh);

    /** Ends any rebuild dip at once. For prepareToPlay. */
    void cancelRebuildFade();

    /** Bypass crossfade against the latency-matched dry input, applied after the cabinet. */
    BypassFade bypassFade;

    SchematicModel::Schematic schematic;

    /** Guards `circuits` and `controls`. The message thread holds it briefly to swap; the
        audio thread only tries it, and passes audio through dry if it fails. */
    juce::SpinLock circuitLock;

    /** One per channel. */
    std::vector<std::unique_ptr<Circuit>> circuits;
    std::vector<SchematicModel::LiveControl> controls;

    /** The circuit a rebuild replaced, while it fades out. Guarded by `circuitLock`, freed
        by releaseFadedCircuits() or the next rebuild. */
    std::vector<std::unique_ptr<Circuit>> fadingCircuits;

    /** The filters `fadingCircuits` run on, when a factor change gave the new circuit its
        own. Null with fadingOwnOversampler set means the old circuit ran at the host rate. */
    std::unique_ptr<juce::dsp::Oversampling<float>> fadingOversampler;
    bool fadingOwnOversampler = false;

    /** Samples left in each half of the dip, at the host rate. Set under `circuitLock`,
        counted down by the audio thread. Never both non-zero. */
    int fadeOutRemaining = 0;
    int fadeInRemaining = 0;
    int rebuildFadeLength = 1;

    /** Set by the audio thread when a fade-out reaches silence. */
    std::atomic<bool> rebuildFadeFinished { false };

    //==========================================================================
    /** The speaker cabinet, last in the chain: after the output gain, at the host rate,
        with no latency of its own. PartitionedConvolver rather than juce::dsp::Convolution
        so a new filter can be set from the message thread while audio runs. */
    PartitionedConvolver cabinet;

    /** See getInputFilePlayer(). */
    InputFilePlayer inputFile;

    /** Length of the loaded impulse response. Written by the message thread. */
    std::atomic<int> cabinetLength { 0 };

    /** Whether to run the cabinet: switched on and a file loaded. */
    std::atomic<bool> cabinetActive { false };

    /** The file already loaded, to skip reloading an unchanged path. Message thread. */
    juce::File loadedCabinetFile;

    // ==========================================================================
    // Scopes: probes from the last build, and the traces the audio thread fills.

    /** Which nodes each scope watches. Swapped with the circuits under `circuitLock`. */
    std::vector<SchematicModel::ScopeProbe> probes;

    /** One trace per scope slot. Fixed array: never reallocated while audio runs. */
    std::array<ScopeTrace, maxScopes> scopeTraces;

    /** The element owning each trace slot (0 = unused), so getScopeTrace() can find a
        trace without locking. */
    std::array<std::atomic<int>, maxScopes> scopeElementIds {};

    /** The audio thread's progress through the current column. */
    struct ScopeAccumulator
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
        int count = 0;
    };

    std::array<ScopeAccumulator, maxScopes> scopeAccumulators;

    /** Samples per column for each scope, at the oversampled rate. */
    std::array<int, maxScopes> samplesPerScopeColumn { };

    /** Samples the inspected part's current is averaged over. */
    int samplesPerCurrentWindow = 2048;

    /** Everything the per-sample loop reads, gathered once per block. */
    struct BlockState
    {
        ChannelMode mode = ChannelMode::Stereo;
        bool bypassed = false;
        float inputGain = 1.0f, outputGain = 1.0f;
        float controls[maxLiveControls] {};
        int controlCount = 0;
    };

    /** One circuit per channel. `set` is the live circuits or the fading ones; only live
        ones apply the controls and feed the scopes and current readout. */
    void runStereo (std::vector<std::unique_ptr<Circuit>>& set, bool live,
                    float* left, float* right, int count, const BlockState& state);

    /** One circuit, output copied to both channels. */
    void runMono (std::vector<std::unique_ptr<Circuit>>& set, bool live,
                  float* left, float* right, int count, const BlockState& state);

    /** Applies the rebuild dip at the host rate: down while the old circuit plays, up while
        the new one does. */
    void applyRebuildFade (juce::AudioBuffer<float>& piece, bool outgoing) noexcept;

    void applyControls (Circuit& circuit, const BlockState& state);

    void prepareScopes();

    /** Takes one reading from every probe. Called per sample, so it must be cheap. */
    void sampleScopes (Circuit& circuit) noexcept;

    // ==========================================================================
    // The inspected part's current (one part at a time; nothing measured when no one
    // asks).

    /** The part being measured, or 0. Written by the message thread, read by the audio
        thread. */
    std::atomic<int> inspectedElement { 0 };

    /** Where its pins landed. Swapped with the circuits under `circuitLock`. */
    std::vector<SchematicModel::CurrentProbe> currentProbes;

    /** Running totals for the inspected part, averaged over the scope window. */
    struct CurrentAccumulator
    {
        double sumCurrent = 0.0;
        double sumPower = 0.0;
        double peakCurrent = 0.0;
        int count = 0;
    };

    CurrentAccumulator currentAccumulator;

    std::atomic<float> inspectedCurrent { 0.0f };
    std::atomic<float> inspectedPower { 0.0f };
    std::atomic<float> inspectedPeakCurrent { 0.0f };
    std::atomic<bool> inspectedLive { false };

    /** See getHeldSampleTotal(). */
    std::atomic<juce::int64> heldSampleTotal { 0 };

    /** Adds what `circuit` held since `before`, its count at the start of the block. */
    void addHeldSamples (const Circuit& circuit, int before) noexcept
    {
        if (const int held = circuit.getNonConvergenceCount() - before; held > 0)
            heldSampleTotal.fetch_add (held, std::memory_order_relaxed);
    }

    /** One reading from the inspected part. Called per sample. */
    void sampleInspectedCurrent (Circuit& circuit) noexcept;

    double currentSampleRate = 44100.0;

    juce::AudioParameterBool* bypassParameter = nullptr;
    juce::AudioParameterChoice* channelModeParameter = nullptr;

    /** Raw parameter values processBlock reads, looked up once so the audio thread never
        builds parameter id strings. */
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* inputGainValue = nullptr;
    std::atomic<float>* outputGainValue = nullptr;
    std::array<std::atomic<float>*, maxLiveControls> controlValues {};

    /** Last channel mode, so switching back to stereo can reset the idle circuit. */
    ChannelMode lastChannelMode = ChannelMode::Stereo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
