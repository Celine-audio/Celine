//==============================================================================
/*
    Circuit's netlist half: building the netlist, changing component values,
    naming nodes, and the prepare()/reset() lifetime.

    The solving half -- stamping, the DK precomputation and the per-sample
    Newton -- lives in EngineSolve.cpp.
*/
//==============================================================================

#include "Engine.h"

#include <algorithm>
#include <cmath>

Circuit::Circuit()
{
    nodeIndices["gnd"] = groundIndex;
}

//==========================================================================
// Netlist construction -- passive
//==========================================================================

Circuit::ComponentId Circuit::addResistor(const juce::String& nodeA, const juce::String& nodeB, double ohms)
{
    resistors.push_back({getOrCreateNode(nodeA), getOrCreateNode(nodeB), ohms});
    linearDirty = true;
    return static_cast<ComponentId>(resistors.size() - 1);
}

Circuit::ComponentId Circuit::addCapacitor(const juce::String& nodeA, const juce::String& nodeB, double farads)
{
    Capacitor c{};
    c.a = getOrCreateNode(nodeA);
    c.b = getOrCreateNode(nodeB);
    c.farads = farads;
    capacitors.push_back(c);
    linearDirty = true;
    return static_cast<ComponentId>(capacitors.size() - 1);
}

Circuit::ComponentId Circuit::addPolarisedCapacitor(const juce::String& positive,
                                                    const juce::String& negative,
                                                    double farads,
                                                    double esrOhms)
{
    const auto id = addCapacitor(positive, negative, farads);
    auto& c = capacitors[static_cast<size_t>(id)];
    c.esrOhms = std::max(0.0, esrOhms);
    c.polarised = true;
    return id;
}

Circuit::ComponentId Circuit::addInductor(const juce::String& nodeA, const juce::String& nodeB, double henries)
{
    Inductor l{};
    l.a = getOrCreateNode(nodeA);
    l.b = getOrCreateNode(nodeB);
    l.henries = henries;
    inductors.push_back(l);
    linearDirty = true;
    return static_cast<ComponentId>(inductors.size() - 1);
}

Circuit::Potentiometer Circuit::addPotentiometer(const juce::String& pin1,
                                                 const juce::String& wiper,
                                                 const juce::String& pin3,
                                                 double maxOhms,
                                                 Potentiometer::Taper taper)
{
    Potentiometer pot;
    pot.upperResistor = addResistor(pin1, wiper, maxOhms * 0.5);
    pot.lowerResistor = addResistor(wiper, pin3, maxOhms * 0.5);
    pot.maxResistance = maxOhms;
    pot.taper = taper;
    return pot;
}

Circuit::Potentiometer Circuit::addVariableResistor(const juce::String& nodeA,
                                                    const juce::String& nodeB,
                                                    double maxOhms,
                                                    double minOhms,
                                                    Potentiometer::Taper taper)
{
    Potentiometer pot;
    pot.upperResistor = -1;
    pot.lowerResistor = addResistor(nodeA, nodeB, maxOhms * 0.5);
    pot.maxResistance = maxOhms;
    pot.minResistance = minOhms;
    pot.taper = taper;
    return pot;
}

Circuit::ComponentId Circuit::addIdealOpAmp(const juce::String& inPlus,
                                            const juce::String& inMinus,
                                            const juce::String& output)
{
    IdealOpAmp amp{};
    amp.inPlus = getOrCreateNode(inPlus);
    amp.inMinus = getOrCreateNode(inMinus);
    amp.output = getOrCreateNode(output);
    idealOpAmps.push_back(amp);
    linearDirty = true;
    return static_cast<ComponentId>(idealOpAmps.size() - 1);
}

Circuit::ComponentId Circuit::addTransformer(const std::vector<WindingSpec>& windings)
{
    jassert(windings.size() >= 2);
    jassert(windings.size() <= static_cast<size_t>(CircuitComponents::maxWindings));

    Transformer t{};
    t.windingCount = std::min(static_cast<int>(windings.size()), CircuitComponents::maxWindings);

    for (int w = 0; w < t.windingCount; ++w)
    {
        const auto& spec = windings[static_cast<size_t>(w)];
        t.windings[w] = {getOrCreateNode(spec.a), getOrCreateNode(spec.b), spec.turns, 0.0};
    }

    transformers.push_back(t);
    linearDirty = true;
    return static_cast<ComponentId>(transformers.size() - 1);
}

Circuit::ComponentId Circuit::addCenterTapTransformer(const juce::String& primaryA,
                                                      const juce::String& primaryB,
                                                      const juce::String& secondaryA,
                                                      const juce::String& centerTap,
                                                      const juce::String& secondaryB,
                                                      double primaryTurns,
                                                      double secondaryTurns)
{
    // Both halves wound the same way round, meeting at the tap, so the tap sits
    // midway and the two ends move in opposition to it.
    const double half = 0.5 * secondaryTurns;

    return addTransformer({
        {primaryA, primaryB, primaryTurns},
        {secondaryA, centerTap, half},
        {centerTap, secondaryB, half},
    });
}

Circuit::ComponentId Circuit::addVccs(const juce::String& from,
                                      const juce::String& to,
                                      const juce::String& controlPositive,
                                      const juce::String& controlNegative,
                                      double transconductance)
{
    Vccs g{};
    g.from = getOrCreateNode(from);
    g.to = getOrCreateNode(to);
    g.controlPositive = getOrCreateNode(controlPositive);
    g.controlNegative = getOrCreateNode(controlNegative);
    g.transconductance = transconductance;
    transconductances.push_back(g);
    linearDirty = true;
    return static_cast<ComponentId>(transconductances.size() - 1);
}

Circuit::Switch Circuit::addSwitch(const juce::String& nodeA, const juce::String& nodeB, bool initiallyClosed)
{
    Switch toggle;
    toggle.resistor = addResistor(nodeA, nodeB, toggle.openOhms);
    toggle.setClosed(*this, initiallyClosed);
    return toggle;
}

Circuit::Changeover Circuit::addChangeoverSwitch(const juce::String& common,
                                                 const juce::String& throwA,
                                                 const juce::String& throwB)
{
    Changeover changeover;
    changeover.throwA = addSwitch(common, throwA, true);
    changeover.throwB = addSwitch(common, throwB, false);
    return changeover;
}

//==========================================================================
// Netlist construction -- sources
//==========================================================================

Circuit::ComponentId Circuit::addVoltageSource(const juce::String& positive,
                                               const juce::String& negative,
                                               double volts)
{
    VoltageSource source{};
    source.positive = getOrCreateNode(positive);
    source.negative = getOrCreateNode(negative);
    source.volts = volts;
    voltageSources.push_back(source);
    linearDirty = true;
    return static_cast<ComponentId>(voltageSources.size() - 1);
}

Circuit::ComponentId Circuit::addAcVoltageSource(const juce::String& positive,
                                                 const juce::String& negative,
                                                 double amplitude,
                                                 double frequency,
                                                 double dcOffset)
{
    const auto id = addVoltageSource(positive, negative, dcOffset);
    auto& source = voltageSources[static_cast<size_t>(id)];
    source.acAmplitude = amplitude;
    source.acFrequency = frequency;
    source.phase = 0.0;
    return id;
}

//==========================================================================
// Netlist construction -- semiconductors
//==========================================================================

Circuit::ComponentId Circuit::addDiode(const juce::String& anode,
                                       const juce::String& cathode,
                                       const DiodeModel& model,
                                       int seriesCount)
{
    Diode d{};
    d.anode = getOrCreateNode(anode);
    d.cathode = getOrCreateNode(cathode);
    d.model = model;
    d.seriesCount = std::max(1, seriesCount);
    d.vCrit = CircuitComponents::criticalVoltage(d.model.saturationCurrent, d.scaleVoltage());
    // From the stack, like vCrit above and for the same reason: this is what the
    // step limiter damps against, and a knee computed for one junction while the
    // exponential runs on n leaves the limiter acting on every pass.
    d.vCritBreakdown = d.model.breakdownVoltage > 0.0
                         ? CircuitComponents::criticalVoltage(d.model.breakdownCurrent,
                                                              d.breakdownScaleVoltage())
                         : 0.0;
    diodes.push_back(d);
    return static_cast<ComponentId>(diodes.size() - 1);
}

Circuit::ComponentId Circuit::addTransistor(const juce::String& base,
                                            const juce::String& collector,
                                            const juce::String& emitter,
                                            const BjtModel& model)
{
    Bjt t{};
    t.base = getOrCreateNode(base);
    t.collector = getOrCreateNode(collector);
    t.emitter = getOrCreateNode(emitter);
    transistors.push_back(t);

    const auto id = static_cast<ComponentId>(transistors.size() - 1);
    setTransistorModel(id, model);

    // The junction capacitances wire themselves, exactly as a valve's
    // interelectrode capacitance does: ordinary capacitors between terminals
    // the transistor already has, so the Miller multiplication of the
    // base-collector one comes out of the nodal solve rather than any formula.
    // Zero means "don't model it", which is how BuildOptions turns them off.
    //
    // Same bookkeeping caveat as addTriode(): these add capacitors, so ids
    // handed out after this call shift. Hold what addCapacitor() returns
    // rather than counting.
    if (model.capBaseEmitter > 0.0)
        addCapacitor(base, emitter, model.capBaseEmitter);

    if (model.capBaseCollector > 0.0)
        addCapacitor(base, collector, model.capBaseCollector);

    return id;
}

Circuit::ComponentId Circuit::addJfet(const juce::String& drain,
                                      const juce::String& gate,
                                      const juce::String& source,
                                      const JfetModel& model)
{
    Jfet j{};
    j.drain = getOrCreateNode(drain);
    j.gate = getOrCreateNode(gate);
    j.source = getOrCreateNode(source);
    j.model = model;
    j.vCritGate = CircuitComponents::criticalVoltage(model.gateSaturationCurrent, model.gateScaleVoltage());
    jfets.push_back(j);
    return static_cast<ComponentId>(jfets.size() - 1);
}

Circuit::OpAmp Circuit::addOpAmp(const juce::String& name,
                                 const juce::String& inPlus,
                                 const juce::String& inMinus,
                                 const juce::String& output,
                                 const OpAmpModel& model)
{
    // The gain node carries signal, so it belongs to this op-amp alone and is
    // named after it.
    const juce::String gainNode = name + "_gain";

    // The rails do not. They are fixed voltages against ground, and on a real
    // board every op-amp sharing a supply shares the same wire -- so share the
    // node here too, keyed on the voltage itself, and create the source only for
    // whichever op-amp asks first.
    //
    // This is not just tidiness. Each rail costs an unknown node *and* a
    // constraint row, so a second op-amp on the same supply was adding six
    // dimensions to the system to describe voltages the first one had already
    // pinned. Two ideal sources at the same potential also make the sharing
    // exact rather than approximate: there is no impedance between them to lose.
    auto sharedRail = [this](double volts)
    {
        const juce::String node = "opamp_rail_" + juce::String(volts, 6);
        const auto nodesBefore = nodeIndices.size();

        getOrCreateNode(node);

        if (nodeIndices.size() != nodesBefore)
            addVoltageSource(node, "gnd", volts);

        return node;
    };

    OpAmp amp;

    // Differential input resistance -- the only thing loading the input pins.
    amp.inputResistor = addResistor(inPlus, inMinus, model.inputResistance);

    // Gain stage: current into the gain node proportional to the differential
    // input. Driving from ground into the node means it sources rather than
    // sinks, so the node follows the non-inverting input.
    amp.gainStage = addVccs("gnd", gainNode, inPlus, inMinus, model.inputTransconductance());

    // The gain node rests at the midpoint of the rails, not at ground. On a
    // single supply those are different places, and hanging it off ground puts
    // it permanently outside the clamp diodes -- one of them then conducts all
    // the time and pins the open-loop gain to a few thousand at every frequency.
    const juce::String midNode = sharedRail(model.midpoint());

    // The gain node's own impedance, and the capacitor that rolls the open-loop
    // gain off at 6 dB/octave the way a real part does.
    amp.gainNodeResistor = addResistor(gainNode, midNode, model.gainNodeResistance);
    amp.poleCapacitor = addCapacitor(gainNode, midNode, model.poleCapacitance());

    // Rails, with a diode each way clamping the gain node between them. This is
    // the whole of the saturation behaviour, and it is two ordinary diodes.
    //
    // The clamp nodes sit a diode drop further in than the headroom figure asks
    // for, because the diode only starts conducting once it is that far past
    // them -- without the offset, every op-amp would swing a volt wider than its
    // railHeadroom said. The clamp stays soft either way, which is what a real
    // output stage does as it runs out of room.
    constexpr double clampDiodeDrop = 0.7;

    const juce::String highClamp = sharedRail(model.positiveRail - model.railHeadroom - clampDiodeDrop);
    const juce::String lowClamp = sharedRail(model.negativeRail + model.railHeadroom + clampDiodeDrop);

    amp.positiveClamp = addDiode(gainNode, highClamp, DiodeModel::silicon());
    amp.negativeClamp = addDiode(lowClamp, gainNode, DiodeModel::silicon());

    // Output buffer: current into the output proportional to how far it lags
    // the gain node. Drawing from ground rather than from the gain node is what
    // makes it a buffer -- the load can't pull the gain stage about.
    amp.outputStage = addVccs("gnd", output, gainNode, output, 1.0 / model.outputResistance);

    return amp;
}

//==========================================================================
// Netlist construction -- valves
//==========================================================================

Circuit::ComponentId Circuit::addVacuumDiode(const juce::String& plate,
                                             const juce::String& cathode,
                                             const VacuumDiodeModel& model)
{
    VacuumDiode d{};
    d.plate = getOrCreateNode(plate);
    d.cathode = getOrCreateNode(cathode);
    d.model = model;
    vacuumDiodes.push_back(d);
    return static_cast<ComponentId>(vacuumDiodes.size() - 1);
}

void Circuit::addInterelectrodeCapacitance(const juce::String& grid,
                                           const juce::String& plate,
                                           const juce::String& cathode,
                                           double gridToCathode,
                                           double gridToPlate,
                                           double plateToCathode)
{
    // Zero means "don't model it", which is how a caller turns one off -- the
    // editor's Settings menu does exactly that, via BuildOptions.
    //
    // No new *nodes* either way, since these connect terminals the valve already
    // has. That is not the same as free: in DK every capacitor is a state
    // variable, and three per valve measures +23% on a six-triode preamp.
    if (gridToCathode > 0.0)
        addCapacitor(grid, cathode, gridToCathode);

    if (gridToPlate > 0.0)
        addCapacitor(grid, plate, gridToPlate);

    if (plateToCathode > 0.0)
        addCapacitor(plate, cathode, plateToCathode);
}

Circuit::ComponentId Circuit::addTriode(const juce::String& plate,
                                        const juce::String& grid,
                                        const juce::String& cathode,
                                        const TriodeModel& model)
{
    Triode t{};
    t.plate = getOrCreateNode(plate);
    t.grid = getOrCreateNode(grid);
    t.cathode = getOrCreateNode(cathode);
    t.model = model;
    triodes.push_back(t);
    const auto id = static_cast<ComponentId>(triodes.size() - 1);

    // The valve wires its own interelectrode capacitance. Grid-to-plate is the
    // Miller path, multiplied by the stage's own gain by the nodal solve rather
    // than by any filter written here. How much it matters depends on what
    // drives the grid -- see TriodeModel for the measured figures.
    //
    // These add capacitors, so any capacitor ids handed out after this call are
    // shifted by however many the model asked for. Hold the ids addCapacitor()
    // returns rather than counting.
    addInterelectrodeCapacitance(grid, plate, cathode, model.capGridCathode, model.capGridPlate,
                                 model.capPlateCathode);
    return id;
}

Circuit::ComponentId Circuit::addPentode(const juce::String& plate,
                                         const juce::String& screen,
                                         const juce::String& grid,
                                         const juce::String& cathode,
                                         const PentodeModel& model)
{
    Pentode p{};
    p.plate = getOrCreateNode(plate);
    p.screen = getOrCreateNode(screen);
    p.grid = getOrCreateNode(grid);
    p.cathode = getOrCreateNode(cathode);
    p.model = model;
    pentodes.push_back(p);
    const auto id = static_cast<ComponentId>(pentodes.size() - 1);

    // As for the triode, though a pentode's grid-to-plate capacitance is small
    // by construction -- that is what the screen is for.
    addInterelectrodeCapacitance(grid, plate, cathode, model.capGridCathode, model.capGridPlate,
                                 model.capPlateCathode);
    return id;
}

//==========================================================================
// Runtime component-value updates
//==========================================================================

void Circuit::setResistance(ComponentId id, double ohms)
{
    auto& r = resistors[static_cast<size_t>(id)];

    // Exact comparison on purpose: this only decides whether to re-stamp, and an
    // unchanged knob writes back a bit-identical value.
    if (! juce::exactlyEqual(r.ohms, ohms))
    {
        r.ohms = ohms;
        linearDirty = true;
    }
}

void Circuit::setCapacitance(ComponentId id, double farads)
{
    auto& c = capacitors[static_cast<size_t>(id)];
    if (! juce::exactlyEqual(c.farads, farads))
    {
        c.farads = farads;
        linearDirty = true;
    }
}

void Circuit::setInductance(ComponentId id, double henries)
{
    auto& l = inductors[static_cast<size_t>(id)];
    if (! juce::exactlyEqual(l.henries, henries))
    {
        l.henries = henries;
        linearDirty = true;
    }
}

void Circuit::setVoltage(ComponentId id, double volts)
{
    // No re-stamp needed: a source's voltage lives entirely in the right-hand
    // side, which is rebuilt every sample anyway. Only the matrix is cached.
    voltageSources[static_cast<size_t>(id)].volts = volts;
}

void Circuit::setDiodeModel(ComponentId id, const DiodeModel& model, int seriesCount)
{
    auto& d = diodes[static_cast<size_t>(id)];
    d.model = model;
    d.seriesCount = std::max(1, seriesCount);
    d.vCrit = CircuitComponents::criticalVoltage(d.model.saturationCurrent, d.scaleVoltage());
    // From the stack, like vCrit above and for the same reason: this is what the
    // step limiter damps against, and a knee computed for one junction while the
    // exponential runs on n leaves the limiter acting on every pass.
    d.vCritBreakdown = d.model.breakdownVoltage > 0.0
                         ? CircuitComponents::criticalVoltage(d.model.breakdownCurrent,
                                                              d.breakdownScaleVoltage())
                         : 0.0;
}

void Circuit::setTransistorModel(ComponentId id, const BjtModel& model)
{
    auto& t = transistors[static_cast<size_t>(id)];
    t.model = model;
    t.vCritBe = CircuitComponents::criticalVoltage(model.saturationCurrent, model.forwardScaleVoltage());
    t.vCritBc = CircuitComponents::criticalVoltage(model.saturationCurrent, model.reverseScaleVoltage());
}

//==========================================================================
// I/O node designation
//==========================================================================

void Circuit::setInputNode(const juce::String& node)
{
    inputIndex = getOrCreateNode(node);
}

void Circuit::setOutputNode(const juce::String& node)
{
    outputIndex = getOrCreateNode(node);
}

void Circuit::setOutputOffsetToOperatingPoint()
{
    jassert(outputIndex >= 0);
    outputOffset = nodeVoltage[static_cast<size_t>(outputIndex)];
}

//==========================================================================
// Lifetime
//==========================================================================

void Circuit::prepare(double sampleRate)
{
    jassert(inputIndex >= 0 && outputIndex >= 0); // call setInputNode()/setOutputNode() first

    dt = 1.0 / sampleRate;

    const auto numNodes = nodeIndices.size();

    // Ground and the input node are known voltages; every other node is an
    // unknown. Voltage sources then add one branch-current unknown each, which
    // sit after all the node rows.
    rowOfNode.assign(numNodes, -1);
    nodeOfRow.clear();
    nodeOfRow.reserve(numNodes);

    int row = 0;
    for (size_t node = 0; node < numNodes; ++node)
    {
        if (static_cast<NodeIndex>(node) == groundIndex || static_cast<NodeIndex>(node) == inputIndex)
            continue;

        rowOfNode[node] = row++;
        nodeOfRow.push_back(static_cast<NodeIndex>(node));
    }

    numNodeUnknowns = row;

    // Constraint rows, laid out after the node rows: one per voltage source,
    // one per ideal op-amp, then one per transformer winding.
    idealOpAmpRowOffset = numNodeUnknowns + static_cast<int>(voltageSources.size());
    transformerRowOffset = idealOpAmpRowOffset + static_cast<int>(idealOpAmps.size());

    int transformerRows = 0;
    for (const auto& t : transformers)
        transformerRows += t.windingCount;

    systemSize = transformerRowOffset + transformerRows;

    // Everything process() touches is sized here and never resized again.
    const auto n = static_cast<size_t>(systemSize);
    solver.resize(systemSize);
    linearMatrix.assign(n * n, 0.0);
    dcMatrix.assign(n * n, 0.0);
    inputCoupling.assign(n, 0.0);
    inputCouplingRows.clear();
    inputCouplingRows.reserve(n);
    rhs.assign(n, 0.0);
    dcRhs.assign(n, 0.0);
    work.assign(n, 0.0);
    linearSolution.assign(n, 0.0);
    nodeVoltage.assign(numNodes, 0.0);

    newtonSolver.resize(systemSize);

    // Collect the nonlinear devices' ports. This sizes everything the DK path
    // needs and is what decides how big the per-sample Newton actually is.
    buildPortList();

    // Pick a strategy. DK replaces an n x n Newton with an m x m one, having
    // paid up front to eliminate everything linear -- so it wins exactly when
    // the ports are the small part of the circuit. When they aren't, the
    // reduced system is no smaller than what it replaced and the elimination is
    // wasted effort. A shunt diode clipper is the clearest example: one unknown
    // node, two ports.
    activeStrategy = requestedStrategy;

    if (activeStrategy == SolverStrategy::Auto)
    {
        activeStrategy = portCount < systemSize ? SolverStrategy::DiscreteK : SolverStrategy::FullNewton;
    }

    linearDirty = true;
    factorisationValid = false;
    nonConvergenceCount = 0;

    reset();

    // Built here rather than lazily on the first sample. Two reasons: the
    // stamping, the factorisation and the DK precomputation are all allocations
    // and O(n^3) work that have no business happening in the first audio
    // callback, and whoever called prepare() can now ask whether the circuit is
    // actually solvable -- hasUsableFactorisation() is meaningless until this
    // has run.
    rebuildLinearSystem();
}

void Circuit::clearState() noexcept
{
    for (auto& c : capacitors)
    {
        c.vCapPrev = 0.0;
        c.iPrev = 0.0;
    }

    for (auto& l : inductors)
    {
        l.vPrev = 0.0;
        l.iPrev = 0.0;
    }

    for (auto& d : diodes)
        d.vLast = 0.0;

    for (auto& t : transistors)
    {
        t.vBeLast = 0.0;
        t.vBcLast = 0.0;
    }

    for (auto& d : vacuumDiodes)
        d.vLast = 0.0;

    for (auto& t : triodes)
    {
        t.vGridLast = 0.0;
        t.vPlateLast = 0.0;
    }

    for (auto& p : pentodes)
    {
        p.vGridLast = 0.0;
        p.vScreenLast = 0.0;
        p.vPlateLast = 0.0;
    }

    for (auto& j : jfets)
    {
        j.vGateLast = 0.0;
        j.vDrainLast = 0.0;
    }

    for (auto& s : voltageSources)
    {
        s.current = 0.0;
        s.phase = 0.0;
    }

    for (auto& a : idealOpAmps)
        a.current = 0.0;

    for (auto& t : transformers)
        for (int w = 0; w < t.windingCount; ++w)
            t.windings[w].current = 0.0;

    std::fill(nodeVoltage.begin(), nodeVoltage.end(), 0.0);
    std::fill(portVoltage.begin(), portVoltage.end(), 0.0);
    std::fill(portVoltagePrevious.begin(), portVoltagePrevious.end(), 0.0);
    lastIterationCount = 0;
}

void Circuit::reset()
{
    clearState();
    solveOperatingPoint();
}

//==========================================================================
// Node management
//==========================================================================

Circuit::NodeIndex Circuit::getOrCreateNode(const juce::String& name)
{
    auto it = nodeIndices.find(name);
    if (it != nodeIndices.end())
        return it->second;

    const auto index = static_cast<NodeIndex>(nodeIndices.size());
    nodeIndices[name] = index;
    return index;
}

Circuit::NodeIndex Circuit::getNodeIndex(const juce::String& node) const
{
    const auto it = nodeIndices.find(node);
    return it == nodeIndices.end() ? -1 : it->second;
}

double Circuit::getNodeVoltage(const juce::String& node) const
{
    return getNodeVoltage(getNodeIndex(node));
}

double Circuit::getWindingCurrent(ComponentId transformer, int winding) const
{
    if (transformer < 0 || static_cast<size_t>(transformer) >= transformers.size())
        return 0.0;

    const auto& t = transformers[static_cast<size_t>(transformer)];
    if (winding < 0 || winding >= t.windingCount)
        return 0.0;

    return t.windings[winding].current;
}

double Circuit::getSourceCurrent(ComponentId id) const
{
    if (id < 0 || static_cast<size_t>(id) >= voltageSources.size())
        return 0.0;

    return voltageSources[static_cast<size_t>(id)].current;
}
