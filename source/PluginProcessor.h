#pragma once

#include "Schematic/SchematicBuilder.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

//==============================================================================
/**
    A circuit sandbox: the schematic on screen is the signal path.

    Two speeds of change, and the split between them is the whole design:

      Topology      Adding a part, moving a wire, retyping a resistor. Changes
                    which nodes exist, so the matrix is rebuilt and a fresh bias
                    point solved. That allocates, so it happens on the message
                    thread when the user asks -- `rebuild()`.

      Controls      Pots and switches only move a resistance the engine has
                    already stamped, which it re-stamps per block without
                    allocating. So they stay live.

    That is why there is a Rebuild button rather than an attempt to edit the
    running circuit: it keeps the fast path honest instead of making every knob
    pay for the possibility of a topology change.
*/
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

    /** Hands the host a real bypass control rather than making it fade around
        the plugin. */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==========================================================================
    // The schematic, and turning it into something that runs
    //==========================================================================

    /** The drawing. Owned here rather than by the editor so it survives the
        window being closed, and so the host can save it. Message thread only. */
    SchematicModel::Schematic& getSchematic() noexcept { return schematic; }
    const SchematicModel::Schematic& getSchematic() const noexcept { return schematic; }

    /** Builds the schematic and swaps it in. Stamping, factorising and the bias
        solve happen before the swap, so the audio thread is locked out only for
        as long as it takes to move a few pointers; a failed build leaves the
        previous circuit running and reports why. Message thread; it allocates. */
    SchematicModel::BuildResult rebuild();

    /** Deliberate inaccuracies traded for CPU. Read at rebuild() time, so a
        plain value is enough. These travel *with* the document, unlike the
        window size, because they change what the circuit sounds like. */
    SchematicModel::BuildOptions buildOptions;

    /** See wasDocumentFromNewerBuild(). */
    bool documentFromNewerBuild = false;

    /** How many times the host's rate the circuit runs at: 1, 2 or 4.

        Every nonlinear device generates harmonics without limit, and at 48 kHz
        what they make above 24 kHz folds back as inharmonic grit. 2x is roughly
        twice the CPU, the per-sample solve dominating.

        Changing it re-prepares the circuits -- the DK discretisation depends on
        the timestep -- so it goes through rebuild() rather than per block. */
    int oversamplingFactor = 1;

    /** Sets the factor and rebuilds. Message thread; returns the build result
        so the caller can report failures the same way Rebuild does. */
    SchematicModel::BuildResult setOversamplingFactor (int factor);

    /** Loads the cabinet impulse response the Output terminal names, and
        returns what stopped it, or an empty string.

        Message thread; it reads a file. Not folded into `rebuild()`, because a
        cabinet touches nothing the matrix contains -- though rebuild() does call
        it, so loading a preset brings its cabinet along.

        A missing file is a *warning*, not an error: the sheet is fine, the
        setting is kept, and audio carries on without the cab. */
    juce::String refreshCabinet();

    /** How long an impulse response may be, in samples **at 48 kHz**; anything
        longer is truncated. Against a reference rate because what is limited is
        a filter *duration* -- 42.7 ms has to stay 42.7 ms at 96 kHz rather than
        becoming half a cab, so the sample count follows the session rate. */
    static constexpr int cabinetImpulseSamples = 2048;
    static constexpr double cabinetReferenceRate = 48000.0;

    //==========================================================================
    /** One scope's picture, written by the audio thread and read by the UI.

        A scrolling min/max envelope rather than a list of samples: the circuit
        runs at up to 192 kHz into a hundred-odd pixels, so something has to
        decimate and every Nth sample would alias.

        Lock-free by construction -- one writer, one reader, fixed storage, no
        blocking operation. */
    struct ScopeTrace
    {
        /** How many columns the picture is. Fixed, because the storage has to be
            allocated before the audio thread ever touches it. */
        static constexpr int columns = 128;

        std::atomic<float> minimum[columns] {};
        std::atomic<float> maximum[columns] {};

        /** The column being filled. Everything to its left is complete. */
        std::atomic<int> writeColumn { 0 };

        /** The settled DC level and the full swing over the visible window --
            the two numbers you actually bias against, alongside the picture. */
        std::atomic<float> dcAverage { 0.0f };
        std::atomic<float> peakToPeak { 0.0f };

        /** True once this trace has a circuit behind it. A scope whose pins
            touch nothing still draws, and draws a flat line, which is honest but
            indistinguishable from a node genuinely at zero -- so it says which. */
        std::atomic<bool> live { false };
    };

    /** As many scopes as the pool has room for. Fixed for the same reason the
        knob parameters are: the storage cannot be grown from the audio thread,
        and a sheet can draw as many probes as it likes. */
    static constexpr int maxScopes = 8;

    /** The trace for the scope element with this id, or null. Message thread. */
    const ScopeTrace* getScopeTrace (int elementId) const noexcept;

    /** Re-reads the scopes' time bases off the drawing and clears their
        pictures. Its own entry point rather than part of rebuild(): retiming a
        probe changes nothing about the circuit. Message thread. */
    void refreshScopeTiming();

    /** Ask for a part's current to be measured, or 0 to stop measuring.

        Nothing is measured until something asks, so an editor that is closed --
        or open on a part with no readable current -- costs the audio thread
        nothing at all. */
    void setInspectedElement (int elementId) noexcept;

    /** The mean current and power through the inspected part, and the largest
        current seen over the window. False when that part has no reading: it is
        not in the built circuit, or it is a type whose current cannot be
        derived from its terminal voltages. See CurrentProbe. */
    bool readInspectedCurrent (float& current, float& power, float& peak) const noexcept;

    /** How wide a slice of time the picture covers. */
    static constexpr double scopeWindowSeconds = 0.04;

    /** How long the loaded impulse response actually came out, in samples, or 0
        if there isn't one.

        Exists because loading happens on a background thread and returns
        nothing, so this is the only way to see that a file was truncated to the
        cap rather than merely asked to be. */
    int getCabinetLength() const { return cabinet.getCurrentIRSize(); }

    /** Writes the knob parameters back onto the parts that own them.

        The parameters are the live truth -- what the audio thread reads, the
        host automates and the session saves. A part's stored position is the
        drawing's memory, so a knob keeps its meaning when the slots shift under
        it. Wherever the two could disagree the parameters win, and this is that
        direction. Message thread only. */
    void adoptControlPositions();

    /** The live controls of the circuit currently running, in the order their
        parts appear on the sheet. Message thread: for building the control
        panel, not for audio. */
    std::vector<SchematicModel::LiveControl> getLiveControls() const;

    //==========================================================================
    // The whole circuit as one document
    //==========================================================================

    /** The drawing and the knob positions together.

        This is what the host saves *and* what a preset file contains -- one
        format, one code path. A circuit without its knob settings is half a
        preset, and a session that reopened with the knobs restored but the
        circuit gone would be worse than one that saved nothing. */
    juce::ValueTree createDocument();

    /** The editor's last size. On the processor because the editor is rebuilt
        every time the window closes, and in getStateInformation but deliberately
        not in createDocument, so loading a preset never resizes your window.
        Atomic because a host may save state from a thread of its choosing. */
    std::atomic<int> editorWidth { 1060 };
    std::atomic<int> editorHeight { 700 };

    /** Loads one back, rebuilds, and tells any open editor to catch up.
        Returns false if the tree isn't one of ours. */
    bool restoreDocument(const juce::ValueTree& document);

    /** Extension for saved circuits -- Céline schematics. */
    static constexpr const char* circuitFileExtension = ".celsch";

    /** The ValueTree type a saved circuit carries, and what both the host's
        state blob and a `.celsch` file are wrapped in. */
    static constexpr const char* documentType = "CELINESCHEMATIC";

    /** The property naming the build that wrote a document.

        Absent means "older than this field", which is the harmless direction --
        such a sheet is by definition not from the future. Only a version this
        build does not recognise as its own or older is worth saying anything
        about. */
    static constexpr const char* documentVersionProperty = "celineVersion";

    /** Whether the document most recently restored came from a build newer
        than this one.

        Answered here rather than reported through the build, because the answer
        belongs to the *import*: it is true the moment the file is read, and a
        person wants to know before they start looking at what they opened, not
        as one line among the build's notes afterwards. Set on every restore, so
        it never describes a document older than the current one. */
    bool wasDocumentFromNewerBuild() const noexcept { return documentFromNewerBuild; }

    /** This build's version, as written into every document it saves. */
    static juce::String getBuildVersion();

    /** Orders two dotted version strings: negative if `a` is older, 0 if they
        match, positive if `a` is newer.

        Compared component by component as *numbers*, not as text -- "0.10.0" is
        newer than "0.9.79" and a string compare says the opposite. Missing
        components read as zero, so "1.0" and "1.0.0" are the same build. */
    static int compareVersions (const juce::String& a, const juce::String& b);

    /** How many circuits actually run. Stereo keeps one per channel with its
        own capacitor state; the mono settings run one and send its output to
        both, differing only in what they feed it -- the easiest 50% of the
        simulation anyone will ever save. */
    enum class ChannelMode
    {
        Stereo = 0,
        MonoLeft,
        MonoRight,
        MonoSum,
    };

    /** Fired when restoreDocument() replaced the drawing, so an open editor can
        reload the canvas. A callback rather than a cast to the editor type: the
        processor has no business knowing what an editor is, and some hosts
        restore state from a thread the UI can't be touched from, which the
        editor's own handler can deal with. */
    std::function<void()> onSchematicReplaced;

    /** How many generic knob parameters exist.

        The parameter list is fixed at construction, so a sheet's pots and
        switches map onto a pool of this size and anything past it gets no fader
        -- rebuild() says so rather than dropping it quietly.

        Raising this is safe: new ids land after the existing ones, so saved
        sessions and host automation are untouched. Lowering it is not. */
    static constexpr int maxLiveControls = 16;

    /** Parameter id for live control `index`. */
    static juce::String getControlParameterId (int index) { return "knob" + juce::String (index + 1); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Rebuilt whenever the factor or the block size changes. Null at 1x, where
        the signal goes straight to the circuit. */
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int preparedBlockSize = 512;

    void prepareOversampler();

    SchematicModel::Schematic schematic;

    /** Guards `circuits` and `controls`. Held for microseconds by the message
        thread to swap them in, and only ever *tried* by the audio thread, which
        would rather pass the block through dry than block. */
    juce::SpinLock circuitLock;

    /** One per channel, so each keeps its own capacitor and device state. */
    std::vector<std::unique_ptr<Circuit>> circuits;
    std::vector<SchematicModel::LiveControl> controls;

    //==========================================================================
    /** The speaker cabinet, at the very end of everything.

        **After the output gain**, which convolution being linear allows.

        **After the oversampler**, at the host's rate: an impulse response is a
        fixed-rate FIR that generates no harmonics and so cannot alias. It is
        the one part of the chain that gains nothing from running faster.

        **Zero latency**, so it stays out of the latency the oversampler
        reports. A partitioned convolver would be cheaper but would put the
        whole thing out of step with the dry path bypass depends on. */
    juce::dsp::Convolution cabinet { juce::dsp::Convolution::Latency { 0 } };

    /** Whether the audio thread should actually run it: true only when the
        switch is on *and* a file is loaded, so a missing impulse response is
        one branch rather than a silent block. Atomic because the message thread
        sets it while audio is running. */
    std::atomic<bool> cabinetActive { false };

    /** What is already in the convolver, so an unchanged path does not reload on
        every rebuild -- loading re-reads the file and hands it to a background
        thread. Message thread only. */
    juce::File loadedCabinetFile;

    //==========================================================================
    // Scopes. The probes are what the last build found; the traces are the
    // pictures the audio thread fills in.

    /** Which nodes each scope watches. Swapped under `circuitLock` alongside the
        circuits, because the node names are only meaningful for the circuit they
        were extracted from -- a probe left pointing at a node the new circuit
        has not got would read a silent zero and look like a dead stage. */
    std::vector<SchematicModel::ScopeProbe> probes;

    /** One picture per scope. A plain array rather than a vector: it must not be
        reallocated while the audio thread is writing into it, and the audio
        thread is writing into it whenever a scope exists. */
    std::array<ScopeTrace, maxScopes> scopeTraces;

    /** Which element owns each trace slot, mirrored out of `probes` under
        `circuitLock`, so getScopeTrace() -- called from the paint path several
        times a frame -- finds a trace without locking. Locking in paint would
        hand the audio thread a reason to drop a block every frame. 0 means the
        slot is unused. */
    std::array<std::atomic<int>, maxScopes> scopeElementIds {};

    /** Where the audio thread has got to in the column it is filling. Not in
        ScopeTrace because nothing outside the audio thread has any business
        reading them, and putting them there would invite it. */
    struct ScopeAccumulator
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
        int count = 0;
    };

    std::array<ScopeAccumulator, maxScopes> scopeAccumulators;

    /** How many samples go into one column, at the rate the circuit actually
        runs at -- which is the oversampled one, since that is where the samples
        come from. Recomputed whenever that rate changes.

        One per scope, because the time base is a property of the probe: a sheet
        can watch a 20 kHz ripple on one and an envelope settling on another, and
        those are not the same number of samples per column. */
    std::array<int, maxScopes> samplesPerScopeColumn { };

    /** The window the *inspected part's* current is averaged over. Not per
        scope -- it is not a scope -- and kept as its own figure so that
        retiming a probe does not silently retime the current readout with it. */
    int samplesPerCurrentWindow = 2048;

    /** Everything the per-sample loop reads, gathered once a block so the two
        render paths take one argument rather than eight. */
    struct BlockState
    {
        ChannelMode mode = ChannelMode::Stereo;
        bool bypassed = false;
        float inputGain = 1.0f, outputGain = 1.0f;
        float controls[maxLiveControls] {};
        int controlCount = 0;
    };

    /** One circuit per side, each with its own state. */
    void runStereo (float* left, float* right, int count, const BlockState& state);

    /** One circuit, its output copied to both sides. */
    void runMono (float* left, float* right, int count, const BlockState& state);

    void applyControls (Circuit& circuit, const BlockState& state);

    void prepareScopes();

    /** Takes one reading from every probe. Called from inside the per-sample
        loop, so it is the one piece of scope code that has to be cheap: two
        node lookups per probe, measured at 3.5 ns a sample against a 310 ns
        solve. */
    void sampleScopes (Circuit& circuit) noexcept;

    //==========================================================================
    // The inspected part's current.
    //
    // One part at a time, because the inspector only ever shows one: a whole
    // sheet's worth of readings would cost a node lookup per part per sample to
    // produce numbers nobody is looking at. The editor says which, the audio
    // thread answers, and when no inspector is open nothing is measured at all.

    /** Which part the inspector is showing, or 0. Written by the message thread,
        read by the audio thread, and safe to be stale by a block -- the worst a
        race can do is measure the previously selected part for one more buffer. */
    std::atomic<int> inspectedElement { 0 };

    /** Where that part's pins landed. Swapped with the circuits under
        `circuitLock`, for the same reason the scope probes are. */
    std::vector<SchematicModel::CurrentProbe> currentProbes;

    /** Running totals for the inspected part, averaged over the window the
        scope draws. Mean rather than instantaneous: a number reshuffling itself
        sixty times a second is one you cannot read, and what cooks a resistor is
        average power anyway. */
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

    /** One reading from the inspected part, if there is one. Called from the
        per-sample loop next to sampleScopes and costing the same two node
        lookups -- but only ever for a single part. */
    void sampleInspectedCurrent (Circuit& circuit) noexcept;

    double currentSampleRate = 44100.0;

    juce::AudioParameterBool* bypassParameter = nullptr;
    juce::AudioParameterChoice* channelModeParameter = nullptr;

    /** The raw value cells processBlock reads, looked up once in the
        constructor.

        Not an optimisation for its own sake: getControlParameterId() builds
        "knob" + String(n), which is two heap allocations, and the loop that
        reads them runs the full pool every block whatever the sheet holds --
        about thirty allocations per block on the audio thread. The lookup
        itself is free (APVTS keys its map by StringRef), so the strings were
        the whole cost.

        The pointers are stable: an adapter lives as long as the APVTS does, and
        the parameter list is fixed at construction. */
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* inputGainValue = nullptr;
    std::atomic<float>* outputGainValue = nullptr;
    std::array<std::atomic<float>*, maxLiveControls> controlValues {};

    /** So a switch back to stereo can clear the circuit that has been sitting
        idle. Its capacitors would otherwise still hold whatever charge they had
        when it was last used, and re-engaging would thump. */
    ChannelMode lastChannelMode = ChannelMode::Stereo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
