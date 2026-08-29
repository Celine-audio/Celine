#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Components/Components.h"
#include "Solver.h"
#include <unordered_map>
#include <vector>

//==============================================================================
/**
    A modified-nodal-analysis (MNA) circuit solver, built from a netlist of
    named nodes -- the same nodes you would label on a schematic.

    Component descriptors live in the Components folder, the linear algebra in
    Solver.h. Ready-made topologies in Circuits.h are built entirely on this
    class's public interface.

        Circuit circuit;
        circuit.addResistor ("in", "out", 1000.0);
        auto capId = circuit.addCapacitor ("out", "gnd", 100e-9);
        circuit.setInputNode ("in");
        circuit.setOutputNode ("out");
        circuit.prepare (sampleRate);

        circuit.setCapacitance (capId, newValue);   // a knob sweeping cutoff
        float vOut = circuit.process (vIn);

    Nodes and unknowns
    ------------------
    "gnd" is always node 0, implicitly at 0 V. The input node's voltage is
    whatever process() is given -- an ideal source rather than an unknown, which
    keeps the solve small. Every other named node is solved for each sample.

    A voltage source fixes a voltage and lets the circuit decide the current, so
    its branch current becomes an unknown of its own and the system grows by a
    row and a column. That is the "modified" in MNA; see VoltageSource.h. The
    solution vector is the unknown node voltages followed by the source
    currents.

    Capacitors and inductors are discretised with the trapezoidal rule, which
    turns each into a fixed conductance in parallel with a current source
    derived from its own previous voltage and current. Nonlinear devices are
    re-linearised on every Newton iteration.

    What costs what: the DK method
    ------------------------------
    Stamping each device's linearisation into the full n x n matrix and
    re-factorising every Newton iteration works -- it is what the operating
    point solve still does -- but a four-triode preamp is forty-odd nodes, and
    cubing that three times a sample is not a realtime budget.

    The circuit is only nonlinear in a few places. A device touches the rest of
    it through its ports (Ports.h), and a forty-node preamp might have eight.
    Everything between the ports is linear and constant between value changes,
    so eliminate it once. Writing the linear system as

        A z = b - N' i        v = N z + (input coupling)

    with N the port-to-node incidence matrix, z the node voltages and i the port
    currents, substitution gives

        v = vOpen - K i(v),   K = N A^-1 N'

    K is the Thevenin impedance the circuit presents to its own nonlinear ports
    -- m x m, constant until a component value changes. vOpen is what the port
    voltages would be with the devices removed: one triangular solve a sample.
    Newton then runs on m unknowns instead of n:

        F(v) = v - vOpen + K i(v)      J = I + K di/dv

    So the O(n^3) factorisation happens once per knob move rather than three
    times a sample, and each iteration costs m^3 rather than n^3. Per sample:
    one O(n^2) substitution, a few m x m iterations, one O(n*m) update to
    recover the node voltages. Nothing allocates -- prepare() sizes it all.

    A purely linear circuit skips all of it: the factorisation is reused until a
    value changes, so a sample is two triangular substitutions.
*/
class Circuit
{
   public:
    using NodeIndex = CircuitComponents::NodeIndex;
    using ComponentId = CircuitComponents::ComponentId;
    using Potentiometer = CircuitComponents::Potentiometer;
    using Switch = CircuitComponents::Switch;
    using Changeover = CircuitComponents::Changeover;
    using DiodeModel = CircuitComponents::DiodeModel;
    using BjtModel = CircuitComponents::BjtModel;
    using VacuumDiodeModel = CircuitComponents::VacuumDiodeModel;
    using TriodeModel = CircuitComponents::TriodeModel;
    using PentodeModel = CircuitComponents::PentodeModel;
    using JfetModel = CircuitComponents::JfetModel;
    using OpAmpModel = CircuitComponents::OpAmpModel;
    using OpAmp = CircuitComponents::OpAmp;
    using Winding = CircuitComponents::Winding;

    Circuit();

    //==========================================================================
    // Netlist construction -- passive
    //==========================================================================

    /** Adds a resistor between two named nodes (created on first use). */
    ComponentId addResistor(const juce::String& nodeA, const juce::String& nodeB, double ohms);

    /** Adds a capacitor between two named nodes (created on first use). */
    ComponentId addCapacitor(const juce::String& nodeA, const juce::String& nodeB, double farads);

    /** Adds a polarised capacitor -- an electrolytic -- with `positive` as its
        marked positive terminal.

        Two things separate this from addCapacitor(). Its ESR is non-zero, which
        genuinely changes the response when the part is bypassing a comparable
        resistance. And prepare() checks it isn't reverse-biased at the DC
        operating point, reporting through getReversedCapacitorCount() -- see
        Capacitor.h for why the reversal is reported rather than simulated.

        The default ESR is a rough figure for a small electrolytic. Set it from
        a datasheet if the part is doing anything but coupling. */
    ComponentId addPolarisedCapacitor(const juce::String& positive,
                                      const juce::String& negative,
                                      double farads,
                                      double esrOhms = 1.0);

    /** Adds an inductor between two named nodes (created on first use). */
    ComponentId addInductor(const juce::String& nodeA, const juce::String& nodeB, double henries);

    /** Adds a 3-terminal potentiometer between pin 1, wiper, and pin 3.
        Creates two internal resistors representing the upper and lower legs of the pot. */
    Potentiometer addPotentiometer(const juce::String& pin1,
                                   const juce::String& wiper,
                                   const juce::String& pin3,
                                   double maxOhms,
                                   Potentiometer::Taper taper = Potentiometer::Taper::Linear);

    /** Adds a 2-terminal variable resistor (rheostat) between two named nodes. */
    Potentiometer addVariableResistor(const juce::String& nodeA,
                                      const juce::String& nodeB,
                                      double maxOhms,
                                      double minOhms = 1.0,
                                      Potentiometer::Taper taper = Potentiometer::Taper::Linear);

    /** Adds a voltage-controlled current source: a current between `from` and
        `to` proportional to the voltage across `controlPositive` and
        `controlNegative`. Linear, so it costs nothing per sample. See Vccs.h. */
    ComponentId addVccs(const juce::String& from,
                        const juce::String& to,
                        const juce::String& controlPositive,
                        const juce::String& controlNegative,
                        double transconductance);

    /** Adds a switch between two named nodes -- in series to make or break a
        connection, in parallel to short something out. See Switch.h. */
    Switch addSwitch(const juce::String& nodeA, const juce::String& nodeB, bool initiallyClosed = true);

    /** Adds a changeover (SPDT) switch: `common` connects to one throw or the
        other, never both. Starts on throw A. */
    Changeover addChangeoverSwitch(const juce::String& common,
                                   const juce::String& throwA,
                                   const juce::String& throwB);

    //==========================================================================
    // Netlist construction -- sources
    //==========================================================================

    /** Adds an ideal voltage source holding V(positive) - V(negative) at `volts`.

        A 9 V pedal supply is addVoltageSource ("vcc", "gnd", 9.0). For a
        positive-ground circuit like a germanium fuzz, wire it the other way
        round: addVoltageSource ("gnd", "vee", 9.0).

        Each source adds a row and a column to the system -- see the class notes
        above and VoltageSource.h. */
    ComponentId addVoltageSource(const juce::String& positive, const juce::String& negative, double volts);

    /** Adds a sinusoidal source -- a mains transformer secondary, the thing a
        rectifier turns into a supply. `amplitude` is peak volts, not RMS, so a
        325 V peak winding is a 230 V RMS one.

        The DC operating point is solved with the sinusoid at its peak rather
        than at zero. That isn't the true DC state, but it puts the reservoir
        capacitor straight onto roughly the voltage it charges to, so playback
        starts with the rail up instead of spending the first tenth of a second
        getting there. */
    ComponentId addAcVoltageSource(const juce::String& positive,
                                   const juce::String& negative,
                                   double amplitude,
                                   double frequency,
                                   double dcOffset = 0.0);

    //==========================================================================
    // Netlist construction -- semiconductors
    //==========================================================================

    /** Adds a diode conducting from `anode` to `cathode`.

        `seriesCount` stacks that many identical diodes in series, which raises
        the clipping threshold proportionally without adding nodes to the solve. */
    ComponentId addDiode(const juce::String& anode,
                         const juce::String& cathode,
                         const DiodeModel& model = DiodeModel::silicon(),
                         int seriesCount = 1);

    /** Adds a bipolar transistor. The model carries its own polarity, so an NPN
        and a PNP are wired up identically -- base to base, collector to
        collector -- and the model decides which way the currents run.

        Any junction capacitances the model carries (capBaseEmitter /
        capBaseCollector) are wired in here, as a valve's interelectrode
        capacitance is: a zero field means none is added. */
    ComponentId addTransistor(const juce::String& base,
                              const juce::String& collector,
                              const juce::String& emitter,
                              const BjtModel& model = BjtModel::npnSilicon());

    /** Adds a JFET. The model carries its channel type, so an N-channel and a
        P-channel part are wired identically. */
    ComponentId addJfet(const juce::String& drain,
                        const juce::String& gate,
                        const juce::String& source,
                        const JfetModel& model = JfetModel::j201());

    /** Adds an ideal op-amp: forces its inputs equal and sources whatever output
        current that takes. One matrix row and nothing else -- no internal nodes,
        no clipping, no bandwidth. See IdealOpAmp.h for when to prefer it over
        the macro model. */
    ComponentId addIdealOpAmp(const juce::String& inPlus,
                              const juce::String& inMinus,
                              const juce::String& output);

    /** One winding of a transformer, as given to addTransformer(). */
    struct WindingSpec
    {
        juce::String a, b;
        double turns;
    };

    /** Adds an ideal transformer. Windings are given as {nodeA, nodeB, turns};
        only the ratios between the turns counts matter, so 1:1 and 100:100 are
        the same transformer.

        An ideal transformer passes DC, which a real one cannot -- put an
        inductor across the primary for the magnetising inductance and the DC
        goes away along with it. See Transformer.h. */
    ComponentId addTransformer(const std::vector<WindingSpec>& windings);

    /** A transformer with a centre-tapped secondary, as a push-pull output stage
        or a full-wave valve rectifier needs. The two halves share one core, so
        this is a single three-winding transformer rather than two of them. */
    ComponentId addCenterTapTransformer(const juce::String& primaryA,
                                        const juce::String& primaryB,
                                        const juce::String& secondaryA,
                                        const juce::String& centerTap,
                                        const juce::String& secondaryB,
                                        double primaryTurns,
                                        double secondaryTurns);

    /** Adds an op-amp, built from primitives rather than as a device of its own
        -- see OpAmp.h. Returns the handles to its parts, mostly so the clamp
        diodes can be reached.

        Each op-amp creates one internal node named after `name`, which must
        therefore be unique within the circuit. Its supply rails are *not*
        per-op-amp: op-amps whose model puts a rail at the same voltage share
        one node and one source for it, as they share a supply on a real board.
        Two op-amps of the same type on the same battery therefore cost far less
        than twice one. */
    OpAmp addOpAmp(const juce::String& name,
                   const juce::String& inPlus,
                   const juce::String& inMinus,
                   const juce::String& output,
                   const OpAmpModel& model = OpAmpModel::tl072());

    //==========================================================================
    // Netlist construction -- valves
    //==========================================================================

    /** Adds a rectifier valve conducting from plate to cathode. Use an ordinary
        addDiode() instead to model a solid-state rectifier; the difference in
        how much the supply sags is the point of choosing between them. */
    ComponentId addVacuumDiode(const juce::String& plate,
                               const juce::String& cathode,
                               const VacuumDiodeModel& model = VacuumDiodeModel::gz34());

    /** Wires a valve's interelectrode capacitance between terminals it already
        has. Called by addTriode() and addPentode(); zero values are skipped. */
    void addInterelectrodeCapacitance(const juce::String& grid,
                                      const juce::String& plate,
                                      const juce::String& cathode,
                                      double gridToCathode,
                                      double gridToPlate,
                                      double plateToCathode);

    /** Adds a triode. Its interelectrode capacitance comes with it -- see
        TriodeModel -- so the Miller effect is present without the netlist
        having to add anything. */
    ComponentId addTriode(const juce::String& plate,
                          const juce::String& grid,
                          const juce::String& cathode,
                          const TriodeModel& model = TriodeModel::ecc83());

    /** Adds a pentode. Strap `screen` to the same node as `plate` for the
        triode-mode wiring some amps offer as a switch. */
    ComponentId addPentode(const juce::String& plate,
                           const juce::String& screen,
                           const juce::String& grid,
                           const juce::String& cathode,
                           const PentodeModel& model = PentodeModel::el34());

    //==========================================================================
    // Runtime component-value updates
    //==========================================================================

    void setResistance(ComponentId id, double ohms);
    void setCapacitance(ComponentId id, double farads);
    void setInductance(ComponentId id, double henries);

    /** Changes a supply voltage -- a sagging battery, a bias trim. */
    void setVoltage(ComponentId id, double volts);

    /** Swaps a diode's model at runtime -- e.g. a silicon/germanium/LED selector. */
    void setDiodeModel(ComponentId id, const DiodeModel& model, int seriesCount = 1);

    /** Swaps a transistor's model at runtime -- e.g. a gain-selection knob that
        picks a different forwardBeta.

        DC parameters only, effectively: junction capacitances are topology,
        wired by addTransistor() from the model it was given, and swapping the
        model here does not re-wire them. The Early voltage and everything else
        take effect immediately. */
    void setTransistorModel(ComponentId id, const BjtModel& model);

    //==========================================================================
    // I/O node designation
    //==========================================================================

    /** The node driven directly by process()'s input -- treated as a known
        voltage source, not an unknown, so it must be named here (and will
        be created if it hasn't been referenced by a component yet). */
    void setInputNode(const juce::String& node);

    /** Which node's solved voltage process() returns. */
    void setOutputNode(const juce::String& node);


    /** Subtracts a fixed offset from the returned output.

        A biased stage sits at some DC operating point, so its output node rests
        volts above ground rather than at zero. Call this after prepare() with
        no argument and it uses the operating point it just solved, which is the
        DC-blocking capacitor every real pedal has on its output, without the
        extra node. */
    void setOutputOffsetToOperatingPoint();

    //==========================================================================
    // Lifetime
    //==========================================================================

    /** Call once the full netlist is defined and whenever the sample rate changes.
        Allocates every buffer process() needs, so it must not be called from the
        audio thread. */
    void prepare(double sampleRate);

    /** Clears all reactive state and re-solves the DC operating point. */
    void reset();

    //==========================================================================
    // Per-sample processing
    //==========================================================================

    float process(float vIn);

    /** Processes a block in place -- just a convenience wrapper around process(). */
    void process(float* samples, int numSamples);

    //==========================================================================
    // Diagnostics (not for the audio thread)
    //==========================================================================

    /** True if the netlist contains anything needing Newton iteration. */
    bool isNonlinear() const noexcept { return portCount > 0; }

    /** Total unknowns: node voltages plus voltage-source branch currents. */
    int getSystemSize() const noexcept { return systemSize; }

    /** Nonlinear ports -- the size of the system Newton actually iterates on
        under DK. See the DK notes above. */
    int getPortCount() const noexcept { return portCount; }

    //==========================================================================
    // Solver strategy
    //==========================================================================

    /**
        How the per-sample nonlinear solve is done.

        DK is a win when the ports are few compared to the nodes, which is the
        case that matters: a preamp is mostly linear plumbing between a handful
        of valves. It is a loss when they aren't. A shunt diode clipper has one
        unknown node and two ports, so DK's reduced system is *larger* than the
        one it replaces, and it pays the elimination cost for nothing.

        Auto picks per circuit, at prepare() time, and is almost always what you
        want. The explicit settings exist to benchmark one against the other and
        to check they agree.
    */
    enum class SolverStrategy
    {
        Auto,
        DiscreteK,  // eliminate the linear part, iterate on the ports
        FullNewton, // stamp devices into the whole matrix, re-factorise each iteration
    };

    /** Must be called before prepare(); the choice is acted on there. */
    void setSolverStrategy(SolverStrategy strategy) noexcept { requestedStrategy = strategy; }

    /** Whether Newton starts each sample from a straight-line extrapolation of
        the last two, rather than from the last one alone.

        A port that has been moving steadily is more likely to land where the
        line points than where it currently sits, and the iteration it saves is
        saved on the most expensive loop in the plugin. It cannot make an answer
        wrong -- Newton converges to the same root from either start -- so the
        only risk is a bad guess costing an extra pass, and the per-device
        voltage limiter damps a wild one on the first iteration anyway.

        Default off: measured a win on pedals and low-gain preamps, but a loss
        on a high-gain one -- the port trajectories there are clipped rather
        than smooth, and the bad guesses cost limiter-damped iterations. It
        ships as the BuildOptions.predictNewtonSeed option for that reason. */
    void setPortVoltagePrediction(bool shouldPredict) noexcept { predictPortVoltages = shouldPredict; }

    /**
        How precisely Newton has to settle before it stops, and so how many
        iterations a sample costs.

        Newton is stopped once no port voltage moves by more than
        `absolute + relative * |voltage|`. Tightening that buys accuracy nobody
        can hear; loosening it buys CPU. The defaults are set at the resolution
        of the float the result is rounded into -- about 7 significant digits --
        so they're as tight as can possibly matter.

        On a valve circuit the relative term is what bites, because plate
        voltages are hundreds of volts and the tolerance scales with them. A
        preamp whose output swings 30 V and is then scaled to full scale turns
        a relative tolerance of 1e-6 into roughly -100 dBFS of error, and 1e-4
        into roughly -60 dBFS. Somewhere between is a good trade; measure before
        choosing, because it depends on how much gain follows.

        Safe to change at any time -- it only affects when the loop stops. */
    void setConvergenceTolerance(double absolute, double relative) noexcept
    {
        convergenceAbsTolerance = absolute;
        convergenceRelTolerance = relative;
    }

    /** Which strategy prepare() settled on. */
    SolverStrategy getSolverStrategy() const noexcept { return activeStrategy; }

    /** Just the unknown node voltages, excluding source branch currents. */
    int getNumUnknowns() const noexcept { return numNodeUnknowns; }

    /** Newton iterations the last processed sample took (1 for a linear circuit). */
    int getLastIterationCount() const noexcept { return lastIterationCount; }

    /** Counts samples since prepare() where Newton hit the iteration limit without
        converging. Should stay at zero; a non-zero value means the circuit is
        being pushed somewhere the solver struggles with. */
    int getNonConvergenceCount() const noexcept { return nonConvergenceCount; }

    /** True if the per-sample matrix factorised.

        Separate from foundOperatingPoint(), and it has to be: the DC system and
        the cached per-sample system are different matrices, so a circuit can
        settle on a perfectly sensible bias point and still leave the per-sample
        one singular. When that happens process() returns 0 for every sample --
        so this is the difference between a circuit that is quiet and a circuit
        that is broken. */
    bool hasUsableFactorisation() const noexcept { return factorisationValid; }

    /** True if prepare() managed to find a DC operating point. A false here means
        the bias network doesn't resolve -- usually a wiring mistake. */
    bool foundOperatingPoint() const noexcept { return operatingPointConverged; }

    /** How many polarised capacitors sit reverse-biased at the DC operating
        point, which on real hardware is the part that leaks, heats and
        eventually vents. Anything above zero is a netlist wired backwards --
        swap that capacitor's terminals. Recomputed by every reset(). */
    int getReversedCapacitorCount() const noexcept { return reversedCapacitorCount; }

    /** The solved voltage at any named node, for testing and metering.

        This hashes the string and probes a map, so it is fine per rebuild and
        wrong per sample. Anything reading a node every sample -- a scope, a
        current readout -- should resolve the name once with getNodeIndex() and
        use the overload below. */
    double getNodeVoltage(const juce::String& node) const;

    /** The index a node name resolves to, or -1 if the netlist has no such node.

        Stable for the lifetime of the netlist: nodes are only ever appended, by
        getOrCreateNode(), and never renumbered. So a caller that resolves its
        names when the circuit is built can hold the indices until the next
        build without re-checking them. */
    NodeIndex getNodeIndex(const juce::String& node) const;

    /** The solved voltage at an already-resolved node -- two array loads and no
        hashing, which is what makes it safe to call per sample.

        A negative index reads 0 V, so an unresolved probe reads as ground
        rather than needing a branch at every call site. */
    double getNodeVoltage(NodeIndex node) const noexcept
    {
        return node >= 0 && static_cast<size_t>(node) < nodeVoltage.size()
                 ? nodeVoltage[static_cast<size_t>(node)]
                 : 0.0;
    }

    /** The solved branch current through a voltage source, in amps, flowing into
        its positive terminal -- so a supply powering a circuit reads negative. */
    double getSourceCurrent(ComponentId id) const;

    /** The solved current through one winding of a transformer, amps, flowing
        into that winding's first terminal. */
    double getWindingCurrent(ComponentId transformer, int winding) const;

   private:
    //==========================================================================
    // Internal types
    //==========================================================================
    using Resistor = CircuitComponents::Resistor;
    using Capacitor = CircuitComponents::Capacitor;
    using Inductor = CircuitComponents::Inductor;
    using Diode = CircuitComponents::Diode;
    using Bjt = CircuitComponents::Bjt;
    using VacuumDiode = CircuitComponents::VacuumDiode;
    using Triode = CircuitComponents::Triode;
    using Pentode = CircuitComponents::Pentode;
    using Jfet = CircuitComponents::Jfet;
    using Vccs = CircuitComponents::Vccs;
    using IdealOpAmp = CircuitComponents::IdealOpAmp;
    using Transformer = CircuitComponents::Transformer;
    using VoltageSource = CircuitComponents::VoltageSource;

    //==========================================================================
    // Newton-Raphson tuning
    //==========================================================================

    // Defaults chosen by measurement, not by caution. Newton converges
    // quadratically, so the size of its last step badly overstates how much
    // error is left -- a relative tolerance of 1e-4 leaves the three-stage preamp
    // 140 dB below peak while costing a fifth fewer iterations than 1e-6 did.
    // See setConvergenceTolerance().
    double convergenceAbsTolerance = 1.0e-5;
    double convergenceRelTolerance = 1.0e-4;

    /** Hard cap on iterations. Around three is typical thanks to warm starting,
        so hitting this means something has gone badly non-smooth. Set generously:
        an occasional expensive sample is far cheaper than giving up on one and
        letting a visibly wrong value through. */
    static constexpr int maxNewtonIterations = 64;

    /** How many steps the operating-point solve ramps the supplies over if it
        can't find the bias point in one go. See solveOperatingPoint(). */
    static constexpr int sourceSteppingSteps = 16;

    /** How far a polarised capacitor may sit the wrong way round at the DC
        operating point before it's called a wiring error. Real electrolytics
        shrug off a few tenths of a volt; this is loose enough not to fire on
        one that legitimately rests at zero. */
    static constexpr double reverseBiasTolerance = 0.1;

    //==========================================================================
    // Node management
    //==========================================================================
    static constexpr NodeIndex groundIndex = 0;

    NodeIndex getOrCreateNode(const juce::String& name);
    int rowOf(NodeIndex n) const noexcept { return rowOfNode[static_cast<size_t>(n)]; }

    //==========================================================================
    // Stamping
    //==========================================================================

    /** Rebuilds the cached linear matrix and its known-node coupling terms, and
        re-factorises if the circuit is linear. Cheap enough to run per block. */
    void rebuildLinearSystem();

    /** Everything that stamps identically into the per-sample system and the DC
        one: resistors, controlled sources, ideal op-amps, transformers, and a
        voltage source's +/-1 pattern.

        `knownNodeTerm(row, node, coefficient)` receives every contribution from
        a terminal whose voltage isn't an unknown, meaning "add
        coefficient * V(node) to this row's right-hand side". That callback is
        the only thing the two systems disagree about, so parameterising on it
        is what lets them share one description of the circuit instead of two
        that have to be kept identical by hand. See EngineSolve.cpp. */
    template <typename KnownNodeTerm>
    void stampTopology(double* matrix, KnownNodeTerm knownNodeTerm) noexcept;

    /** Stamps a conductance between two nodes, routing any known-voltage
        terminal through `knownNodeTerm` as above. */
    template <typename KnownNodeTerm>
    void stampConductance(double* matrix, NodeIndex a, NodeIndex b, double g, KnownNodeTerm knownNodeTerm) noexcept;

    /** Stamps a companion current source whose element current obeys
        i = g*v - ieq, flowing from a to b. */
    void stampCurrentSource(double* rhsVector, NodeIndex a, NodeIndex b, double ieq) noexcept;

    /** Fills `rhs` with everything that doesn't depend on the unknowns: the
        known-node coupling, the source voltages and the reactive companion
        sources. */
    void buildRightHandSide(double vIn) noexcept;

    /** A source's voltage right now, DC plus whatever its sinusoid is doing. */
    double sourceVoltage(const VoltageSource& source) const noexcept;

    /** Copies a solved vector back into nodeVoltage and the source currents.
        With `checkConvergence` it also returns whether every node stayed inside
        the tolerance; the DK path passes false, since it judges convergence on
        the ports instead and the per-node test would be wasted work. */
    bool writeBackSolution(bool checkConvergence = true) noexcept;

    //==========================================================================
    // Nonlinear ports
    //==========================================================================

    /** Calls `fn` on every nonlinear device in the circuit, in the one order
        that defines how ports are laid out. Everything that walks the port list
        in step -- building it, limiting voltages, linearising -- goes through
        here, so the order can't drift between them. */
    template <typename Fn>
    void forEachNonlinearDevice(Fn fn);

    /** Collects every device's ports into one flat list and caches the row each
        port terminal maps to. Called from prepare(). */
    void buildPortList();

    /** Runs every device's voltage limiter over `portVoltage`, returning true if
        any of them had to damp the step. */
    bool limitPortVoltages() noexcept;

    /** Linearises every device about `portVoltage`, filling portCurrent and the
        block-diagonal portJacobian. */
    void linearisePorts() noexcept;

    /** Stamps the linearised devices into a full n x n system. Only the
        operating-point solve needs this; the per-sample path uses DK instead. */
    void stampPorts(double* matrix, double* rhsVector) noexcept;

    /** Reads port voltages straight off the node voltages, for seeding Newton. */
    void readPortVoltagesFromNodes() noexcept;

    //==========================================================================
    // Solving
    //==========================================================================

    /** Precomputes the DK transfer matrix W = A^-1 N' and impedance K = N W.
        O(n^2) per port, done once whenever the linear system changes. */
    void precomputeDkSystem();

    /** The per-sample path: one linear solve for vOpen, Newton on the m ports,
        then recover the node voltages. Returns false if Newton gave up. */
    bool solveWithDk(double vIn) noexcept;

    /** The full-matrix Newton, stamping devices into the whole n x n system and
        re-factorising each iteration. Slower than DK and only used for the
        operating point, where the matrix is the DC one and it runs once. */
    bool runFullNewton(const double* baseMatrix, const double* baseRhs) noexcept;

    /** Builds the DC system: capacitors open, inductors shorted, supplies scaled
        by `sourceScale`, plus a gmin path from every node to ground so nodes
        reachable only through capacitors don't leave the matrix singular. */
    void buildDcSystem(double sourceScale) noexcept;

    /** Solves the circuit's DC operating point with no input signal and seeds the
        reactive state from it, so playback starts from the steady state the
        circuit would settle into rather than from an arbitrary zero. */
    void solveOperatingPoint() noexcept;

    /** Rolls capacitor/inductor companion state forward using the voltages just solved. */
    void updateReactiveState() noexcept;

    /** Zeroes reactive and Newton state without solving an operating point. */
    void clearState() noexcept;

    //==========================================================================
    // Netlist
    //==========================================================================
    std::unordered_map<juce::String, NodeIndex> nodeIndices;
    std::vector<Resistor> resistors;
    std::vector<Capacitor> capacitors;
    std::vector<Inductor> inductors;
    std::vector<Diode> diodes;
    std::vector<Bjt> transistors;
    std::vector<VacuumDiode> vacuumDiodes;
    std::vector<Triode> triodes;
    std::vector<Pentode> pentodes;
    std::vector<Jfet> jfets;
    std::vector<Vccs> transconductances;
    std::vector<IdealOpAmp> idealOpAmps;
    std::vector<Transformer> transformers;
    std::vector<VoltageSource> voltageSources;

    NodeIndex inputIndex = -1;
    NodeIndex outputIndex = -1;
    double outputOffset = 0.0;
    double dt = 1.0 / 44100.0;

    //==========================================================================
    // Solver state, all sized in prepare()
    //==========================================================================
    CelineEngine::DenseSolver solver;

    std::vector<int> rowOfNode;       // node index -> row, or -1 if its voltage is known
    std::vector<NodeIndex> nodeOfRow; // the inverse mapping, for writing solutions back
    int numNodeUnknowns = 0; // unknown node voltages

    int systemSize = 0; // numNodeUnknowns plus every constraint row below

    // Where each kind of constraint row starts. Voltage sources take one row
    // each, ideal op-amps one, and transformers one per winding.
    int idealOpAmpRowOffset = 0;
    int transformerRowOffset = 0;

    std::vector<double> linearMatrix;  // cached stamps of every linear component
    std::vector<double> inputCoupling; // per row: multiply by vIn for its RHS contribution
    std::vector<double> rhs;           // per-sample right-hand side
    std::vector<double> work;          // scratch the solver overwrites with the solution
    std::vector<double> nodeVoltage;   // per node, known nodes included
    std::vector<double> dcMatrix;      // DC operating-point system, built by buildDcSystem()
    std::vector<double> dcRhs;

    /** The non-zero entries of inputCoupling, as (row, coefficient). The input
        reaches one or two rows of however many the circuit has, so the
        per-sample right-hand side walks these rather than the whole vector. */
    struct InputCouplingTerm
    {
        int row;
        double coefficient;
    };

    std::vector<InputCouplingTerm> inputCouplingRows;

    //==========================================================================
    // Nonlinear port system, sized in prepare()
    //==========================================================================
    std::vector<CircuitComponents::Port> ports;

    /** Where each device's ports start and how many it has. A device's ports
        interact only with each other, so its Jacobian is one square block on the
        diagonal and everything off it is structurally zero. Both the stamping
        and the DK matrix product walk these blocks rather than the full m x m,
        which is the difference between O(m^2) and O(m * blocksize) work. */
    std::vector<std::pair<int, int>> deviceBlocks;

    std::vector<int> portRowA, portRowB;   // row of each terminal, or -1 if its voltage is known
    std::vector<double> portInputCoupling; // +1/-1/0: how much of vIn lands on this port directly
    int portCount = 0;

    std::vector<double> portVoltage;     // v -- what Newton iterates on
    std::vector<double> portVoltagePrevious;  // v one sample further back, for the seed
    std::vector<double> portCurrent;     // i(v)
    std::vector<double> portOpenVoltage; // vOpen -- port voltages with the devices removed
    std::vector<double> portJacobian;    // di/dv, block diagonal, stored m x m
    std::vector<double> portResidual;    // F(v)

    //==========================================================================
    // DK precomputation, valid until the linear system changes
    //==========================================================================
    std::vector<double> portTransfer;      // W = A^-1 N', n x m, column major
    std::vector<double> portImpedance;     // K = N W, m x m
    std::vector<double> linearSolution;    // z with the nonlinear devices removed
    CelineEngine::DenseSolver portSolver; // the m x m Newton system

    /** Scratch for the full-matrix Newton. It gets its own solver so that
        stamping devices into a copy of the system never disturbs `solver`, which
        holds the factorised linear matrix that DK and the linear fast path both
        depend on staying valid between samples. */
    CelineEngine::DenseSolver newtonSolver;

    bool predictPortVoltages = false;
    SolverStrategy requestedStrategy = SolverStrategy::Auto;
    SolverStrategy activeStrategy = SolverStrategy::DiscreteK;

    bool linearDirty = true;
    bool factorisationValid = false;
    bool operatingPointConverged = true;
    int lastIterationCount = 0;
    int nonConvergenceCount = 0;
    int reversedCapacitorCount = 0;
};
