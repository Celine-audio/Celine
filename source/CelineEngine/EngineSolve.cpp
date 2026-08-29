//==============================================================================
/*
    The solving half of Circuit. Netlist construction, component updates and
    lifetime live in Engine.cpp; everything here is about turning that netlist
    into numbers, once per sample.

    Read Engine.h's class comment first -- it explains the DK method these
    functions implement. In short: the linear part of the circuit is eliminated
    ahead of time so that Newton iterates on the handful of nonlinear ports
    rather than on every node.
*/
//==============================================================================

#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
    /** Conductance an inductor is given when solving the DC operating point,
        where it should ideally be a dead short. Small enough to be a short next
        to anything else in an audio circuit, large enough not to wreck the
        matrix conditioning. */
    constexpr double dcInductorConductance = 1.0e3; // 1 milliohm
} // namespace

//==============================================================================
// Stamping
//
// Two systems -- the cached per-sample one and the DC one the operating point
// uses -- are the same circuit seen two ways. Only three things differ, each
// for a physical reason:
//
//     capacitors    a conductance per timestep here, an open circuit at DC
//     inductors     a conductance per timestep here, a short at DC
//     sources       the voltage lands in the per-sample right-hand side, so an
//                   AC winding can vary without touching the cached matrix
//
// Everything else is written once below and used by both, parameterised on the
// one remaining difference: what becomes of a terminal whose voltage is already
// known -- the input node's coupling in one case, a fixed right-hand side
// contribution in the other.
//
// Sharing it is not tidiness. Two copies that must agree exactly is how a bias
// point quietly stops describing the circuit that actually runs, which produces
// a plausible wrong answer rather than an obvious one.
//==============================================================================

template <typename KnownNodeTerm>
void Circuit::stampConductance(double* matrix, NodeIndex a, NodeIndex b, double g, KnownNodeTerm knownNodeTerm) noexcept
{
    const int ra = rowOf(a);
    const int rb = rowOf(b);

    if (ra >= 0)
        matrix[ra * systemSize + ra] += g;
    if (rb >= 0)
        matrix[rb * systemSize + rb] += g;

    if (ra >= 0 && rb >= 0)
    {
        matrix[ra * systemSize + rb] -= g;
        matrix[rb * systemSize + ra] -= g;
    }
    else if (ra >= 0)
    {
        // b is a known voltage, so its contribution is a constant current source.
        knownNodeTerm(ra, b, g);
    }
    else if (rb >= 0)
    {
        knownNodeTerm(rb, a, g);
    }
}

template <typename KnownNodeTerm>
void Circuit::stampTopology(double* matrix, KnownNodeTerm knownNodeTerm) noexcept
{
    for (const auto& r : resistors)
        stampConductance(matrix, r.a, r.b, 1.0 / r.ohms, knownNodeTerm);

    // Controlled sources: the current appears in one pair of rows and the
    // voltage driving it in a different pair of columns. That off-diagonal
    // placement is what makes an amplifier an amplifier, and it is linear.
    for (const auto& g : transconductances)
    {
        const int rFrom = rowOf(g.from);
        const int rTo = rowOf(g.to);

        for (int sign = 0; sign < 2; ++sign)
        {
            const NodeIndex control = sign == 0 ? g.controlPositive : g.controlNegative;
            const double gm = sign == 0 ? g.transconductance : -g.transconductance;
            const int rControl = rowOf(control);

            if (rControl >= 0)
            {
                if (rFrom >= 0)
                    matrix[rFrom * systemSize + rControl] += gm;
                if (rTo >= 0)
                    matrix[rTo * systemSize + rControl] -= gm;
            }
            else
            {
                // The controlling node's voltage is known, so its contribution
                // is a fixed current rather than a matrix entry.
                if (rFrom >= 0)
                    knownNodeTerm(rFrom, control, -gm);
                if (rTo >= 0)
                    knownNodeTerm(rTo, control, gm);
            }
        }
    }

    // Ideal op-amps: the output current is the unknown and the row holds the
    // inputs equal. Asymmetric against a voltage source -- the constraint is on
    // one pair of nodes, the current at a different one.
    for (size_t a = 0; a < idealOpAmps.size(); ++a)
    {
        const auto& amp = idealOpAmps[a];
        const int ampRow = idealOpAmpRowOffset + static_cast<int>(a);
        const int rOut = rowOf(amp.output);

        // The op-amp sources this current into the output node.
        if (rOut >= 0)
            matrix[rOut * systemSize + ampRow] -= 1.0;

        const int rPlus = rowOf(amp.inPlus);
        const int rMinus = rowOf(amp.inMinus);

        if (rPlus >= 0)
            matrix[ampRow * systemSize + rPlus] += 1.0;
        else
            knownNodeTerm(ampRow, amp.inPlus, -1.0);

        if (rMinus >= 0)
            matrix[ampRow * systemSize + rMinus] -= 1.0;
        else
            knownNodeTerm(ampRow, amp.inMinus, 1.0);
    }

    // Transformers. One unknown current per winding, and the same number of
    // equations: every winding shares the volts per turn, and the magnetomotive
    // forces sum to zero.
    int windingRow = transformerRowOffset;

    for (const auto& t : transformers)
    {
        const int firstRow = windingRow;

        for (int w = 0; w < t.windingCount; ++w)
        {
            const int ra = rowOf(t.windings[w].a);
            const int rb = rowOf(t.windings[w].b);

            if (ra >= 0)
                matrix[ra * systemSize + firstRow + w] += 1.0;
            if (rb >= 0)
                matrix[rb * systemSize + firstRow + w] -= 1.0;
        }

        // V(0)/N(0) = V(k)/N(k), written as N(k)*V(0) - N(0)*V(k) = 0 so there's
        // nothing to divide by.
        const auto& first = t.windings[0];

        for (int w = 1; w < t.windingCount; ++w)
        {
            const auto& other = t.windings[w];
            const int equation = firstRow + w - 1;

            auto term = [&](NodeIndex node, double coefficient)
            {
                const int r = rowOf(node);
                if (r >= 0)
                    matrix[equation * systemSize + r] += coefficient;
                else
                    knownNodeTerm(equation, node, -coefficient);
            };

            term(first.a, other.turns);
            term(first.b, -other.turns);
            term(other.a, -first.turns);
            term(other.b, first.turns);
        }

        // Sum of N(k)*I(k) = 0 -- an ideal core takes no magnetising current.
        const int ampereRow = firstRow + t.windingCount - 1;
        for (int w = 0; w < t.windingCount; ++w)
            matrix[ampereRow * systemSize + firstRow + w] += t.windings[w].turns;

        windingRow += t.windingCount;
    }

    // Voltage sources: the +/-1 pattern only. The voltage itself is the caller's
    // business -- see the note at the top of this section.
    for (size_t s = 0; s < voltageSources.size(); ++s)
    {
        const auto& source = voltageSources[s];
        const int sourceRow = numNodeUnknowns + static_cast<int>(s);
        const int rp = rowOf(source.positive);
        const int rn = rowOf(source.negative);

        // The branch current appears in the two node equations...
        if (rp >= 0)
            matrix[rp * systemSize + sourceRow] += 1.0;
        if (rn >= 0)
            matrix[rn * systemSize + sourceRow] -= 1.0;

        // ...and the source's own row states V(positive) - V(negative) = volts.
        if (rp >= 0)
            matrix[sourceRow * systemSize + rp] += 1.0;
        else
            knownNodeTerm(sourceRow, source.positive, -1.0);

        if (rn >= 0)
            matrix[sourceRow * systemSize + rn] -= 1.0;
        else
            knownNodeTerm(sourceRow, source.negative, 1.0);
    }
}

void Circuit::stampCurrentSource(double* rhsVector, NodeIndex a, NodeIndex b, double ieq) noexcept
{
    const int ra = rowOf(a);
    const int rb = rowOf(b);

    if (ra >= 0)
        rhsVector[ra] += ieq;
    if (rb >= 0)
        rhsVector[rb] -= ieq;
}

void Circuit::rebuildLinearSystem()
{
    std::fill(linearMatrix.begin(), linearMatrix.end(), 0.0);
    std::fill(inputCoupling.begin(), inputCoupling.end(), 0.0);

    double* matrix = linearMatrix.data();

    // Ground is 0 V so it contributes nothing at all; the input node changes
    // every sample, so its coefficient is kept and multiplied by vIn in
    // buildRightHandSide().
    auto toInputCoupling = [this](int row, NodeIndex node, double coefficient)
    {
        if (node == inputIndex)
            inputCoupling[static_cast<size_t>(row)] += coefficient;
    };

    for (auto& c : capacitors)
    {
        // A capacitor in series with its ESR: over one trapezoidal step the
        // capacitance looks like dt/2C ohms, so the branch is just those two
        // resistances in series. With no ESR this is the usual 2C/dt.
        c.halfStepResistance = dt / (2.0 * c.farads);
        c.conductance = 1.0 / (c.esrOhms + c.halfStepResistance);
        stampConductance(matrix, c.a, c.b, c.conductance, toInputCoupling);
    }

    for (auto& l : inductors)
    {
        l.conductance = dt / (2.0 * l.henries);
        stampConductance(matrix, l.a, l.b, l.conductance, toInputCoupling);
    }

    stampTopology(matrix, toInputCoupling);

    // gmin across every nonlinear port.
    //
    // DK lifts the nonlinear devices out of this matrix, so a node whose only
    // company is semiconductor terminals has no conductance in the linear system
    // at all and the factorisation is singular. Not an exotic case: a
    // transistor's collector wired to the next one's base is an ordinary
    // direct-coupled pair.
    //
    // The failure was invisible, which is the worse part -- the DC system stamps
    // the devices properly, so the operating point solved and looked right while
    // process() returned silence. This is what SPICE's gmin is for.
    for (const auto& port : ports)
        stampConductance(matrix, port.a, port.b, CircuitComponents::gmin, toInputCoupling);

    // Which rows vIn actually reaches -- typically one or two out of however
    // many the circuit has. Walking those rather than the whole vector is what
    // lets buildRightHandSide() start from a straight fill.
    inputCouplingRows.clear();

    for (int row = 0; row < systemSize; ++row)
        if (inputCoupling[static_cast<size_t>(row)] != 0.0)
            inputCouplingRows.push_back({row, inputCoupling[static_cast<size_t>(row)]});

    // The matrix is now final until the next value change. Factorise it once
    // here and every sample until then is just substitutions -- and, if there
    // are nonlinear devices, so is the DK precomputation that follows.
    factorisationValid = false;

    if (systemSize > 0)
    {
        std::copy(linearMatrix.begin(), linearMatrix.end(), solver.data());
        // Not asserted on. A singular netlist is a drawing somebody made, not a
        // programming error: SchematicBuilder checks hasUsableFactorisation()
        // and names the problem in the console. The assertion that used to be
        // here fired during the test suite's own failure-path cases, which is
        // an assertion that has stopped meaning anything.
        factorisationValid = solver.factorise();

        if (factorisationValid && portCount > 0)
            precomputeDkSystem();
    }

    linearDirty = false;
}

double Circuit::sourceVoltage(const VoltageSource& source) const noexcept
{
    return source.volts + source.acAmplitude * std::sin(source.phase);
}

void Circuit::buildDcSystem(double sourceScale) noexcept
{
    std::fill(dcMatrix.begin(), dcMatrix.end(), 0.0);
    std::fill(dcRhs.begin(), dcRhs.end(), 0.0);

    double* matrix = dcMatrix.data();
    double* rhsVector = dcRhs.data();

    // A known terminal is a fixed voltage, so it becomes a fixed current on the
    // right-hand side. Both known nodes sit at 0 V in practice, so these terms
    // vanish -- written out anyway, since the DC solve has no business assuming
    // it.
    auto toDcRhs = [this, rhsVector](int row, NodeIndex node, double coefficient)
    {
        rhsVector[row] += coefficient * nodeVoltage[static_cast<size_t>(node)];
    };

    // Capacitors are open circuits at DC, so they aren't stamped at all.
    // Inductors are shorts.
    for (const auto& l : inductors)
        stampConductance(matrix, l.a, l.b, dcInductorConductance, toDcRhs);

    stampTopology(matrix, toDcRhs);

    // Each source's row states V(positive) - V(negative) = volts, ramped by
    // sourceScale so the operating point can bring the supplies up gradually.
    //
    // A sinusoidal winding is taken at its peak, not its average: the average is
    // zero, which would leave the reservoir capacitor flat and open playback
    // with a tenth of a second of the rail charging. The peak is where it
    // settles anyway.
    for (size_t s = 0; s < voltageSources.size(); ++s)
    {
        const auto& source = voltageSources[s];
        rhsVector[static_cast<size_t>(numNodeUnknowns) + s] +=
            (source.volts + source.acAmplitude) * sourceScale;
    }

    // Without this, any node reachable only through capacitors would have an
    // all-zero row.
    for (int row = 0; row < numNodeUnknowns; ++row)
        matrix[row * systemSize + row] += CircuitComponents::gmin;
}

//==============================================================================
// Nonlinear ports
//==============================================================================

template <typename Fn>
void Circuit::forEachNonlinearDevice(Fn fn)
{
    // The one place the order of the nonlinear devices is decided, and with it
    // the layout of the port list. Three passes below -- build, limit,
    // linearise -- walk devices in step and line up only because they all come
    // through here. Hand-written, that agreement is an invariant maintained in
    // three places.
    //
    // The running port offset is handed to the callback rather than tracked by
    // it, so the whole thing inlines to what the hand-written loops did.
    int firstPort = 0;

    auto visit = [&fn, &firstPort](auto& device)
    {
        fn(device, firstPort);
        firstPort += CircuitComponents::portCount(device);
    };

    for (auto& d : diodes)
        visit(d);

    for (auto& t : transistors)
        visit(t);

    for (auto& d : vacuumDiodes)
        visit(d);

    for (auto& t : triodes)
        visit(t);

    for (auto& p : pentodes)
        visit(p);

    for (auto& j : jfets)
        visit(j);
}

void Circuit::buildPortList()
{
    ports.clear();
    deviceBlocks.clear();

    CircuitComponents::Port scratch[CircuitComponents::maxPortsPerDevice];

    forEachNonlinearDevice(
        [&](const auto& device, int firstPort)
        {
            juce::ignoreUnused(firstPort); // ports.size() is the same thing here
            const int n = CircuitComponents::portCount(device);
            CircuitComponents::fillPorts(device, scratch);
            deviceBlocks.emplace_back(static_cast<int>(ports.size()), n);
            ports.insert(ports.end(), scratch, scratch + n);
        });

    portCount = static_cast<int>(ports.size());

    const auto m = static_cast<size_t>(portCount);
    portRowA.assign(m, -1);
    portRowB.assign(m, -1);
    portInputCoupling.assign(m, 0.0);
    portVoltage.assign(m, 0.0);
    portVoltagePrevious.assign(m, 0.0);
    portCurrent.assign(m, 0.0);
    portOpenVoltage.assign(m, 0.0);
    portResidual.assign(m, 0.0);
    portJacobian.assign(m * m, 0.0);
    portImpedance.assign(m * m, 0.0);
    portTransfer.assign(static_cast<size_t>(systemSize) * m, 0.0);
    portSolver.resize(portCount);

    for (int p = 0; p < portCount; ++p)
    {
        const auto& port = ports[static_cast<size_t>(p)];
        portRowA[static_cast<size_t>(p)] = rowOf(port.a);
        portRowB[static_cast<size_t>(p)] = rowOf(port.b);

        // A port terminal sitting on the input node picks up vIn directly rather
        // than through the matrix, since that node isn't an unknown.
        double coupling = 0.0;
        if (port.a == inputIndex)
            coupling += 1.0;
        if (port.b == inputIndex)
            coupling -= 1.0;
        portInputCoupling[static_cast<size_t>(p)] = coupling;
    }
}

bool Circuit::limitPortVoltages() noexcept
{
    bool limited = false;

    forEachNonlinearDevice(
        [&limited, this](auto& device, int firstPort)
        {
            limited |= CircuitComponents::limitPortVoltages(device, portVoltage.data() + firstPort);
        });

    return limited;
}

void Circuit::linearisePorts() noexcept
{
    // portJacobian is deliberately not zeroed: every device overwrites its own
    // diagonal block and nothing reads outside one, since stampPorts() and the
    // K*di/dv product both walk deviceBlocks.
    CircuitComponents::DeviceLinearisation linearised{};

    // Each device fills a square block on the diagonal: its ports interact with
    // each other and with nothing else. The block-diagonal structure is what
    // keeps this cheap however many devices there are.
    forEachNonlinearDevice(
        [&linearised, this](const auto& device, int firstPort)
        {
            const int n = CircuitComponents::portCount(device);
            CircuitComponents::linearise(device, portVoltage.data() + firstPort, linearised);

            for (int r = 0; r < n; ++r)
            {
                portCurrent[static_cast<size_t>(firstPort + r)] = linearised.current[r];

                for (int c = 0; c < n; ++c)
                    portJacobian[static_cast<size_t>((firstPort + r) * portCount + firstPort + c)] =
                        linearised.jacobian[r * n + c];
            }
        });
}

void Circuit::readPortVoltagesFromNodes() noexcept
{
    for (int p = 0; p < portCount; ++p)
    {
        const auto& port = ports[static_cast<size_t>(p)];
        portVoltage[static_cast<size_t>(p)] =
            nodeVoltage[static_cast<size_t>(port.a)] - nodeVoltage[static_cast<size_t>(port.b)];
    }
}

void Circuit::stampPorts(double* matrix, double* rhsVector) noexcept
{
    // Port p carries current i_p from its node a to its node b. Linearised,
    //     i_p = sum_q J[p][q] * v_q + ieq_p
    // and each v_q is itself a node-voltage difference, so one Jacobian entry
    // lands in up to four places in the matrix.
    for (const auto& [firstPort, blockSize] : deviceBlocks)
    for (int p = firstPort; p < firstPort + blockSize; ++p)
    {
        const int ra = portRowA[static_cast<size_t>(p)];
        const int rb = portRowB[static_cast<size_t>(p)];

        double equivalentCurrent = portCurrent[static_cast<size_t>(p)];

        // Only this device's own ports can appear in row p; the rest of the row
        // is structurally zero.
        for (int q = firstPort; q < firstPort + blockSize; ++q)
        {
            const double j = portJacobian[static_cast<size_t>(p * portCount + q)];

            const auto& portQ = ports[static_cast<size_t>(q)];
            const int ca = portRowA[static_cast<size_t>(q)];
            const int cb = portRowB[static_cast<size_t>(q)];

            // Move the tangent's constant term to the right-hand side. Written
            // against the limited port voltage, which is where the tangent was
            // actually taken, not against whatever the nodes currently say.
            equivalentCurrent -= j * portVoltage[static_cast<size_t>(q)];

            if (ra >= 0)
            {
                if (ca >= 0)
                    matrix[ra * systemSize + ca] += j;
                else
                    rhsVector[ra] -= j * nodeVoltage[static_cast<size_t>(portQ.a)];

                if (cb >= 0)
                    matrix[ra * systemSize + cb] -= j;
                else
                    rhsVector[ra] += j * nodeVoltage[static_cast<size_t>(portQ.b)];
            }

            if (rb >= 0)
            {
                if (ca >= 0)
                    matrix[rb * systemSize + ca] -= j;
                else
                    rhsVector[rb] += j * nodeVoltage[static_cast<size_t>(portQ.a)];

                if (cb >= 0)
                    matrix[rb * systemSize + cb] += j;
                else
                    rhsVector[rb] -= j * nodeVoltage[static_cast<size_t>(portQ.b)];
            }
        }

        if (ra >= 0)
            rhsVector[ra] -= equivalentCurrent;
        if (rb >= 0)
            rhsVector[rb] += equivalentCurrent;
    }
}

//==============================================================================
// DK precomputation
//==============================================================================

void Circuit::precomputeDkSystem()
{
    // W = A^-1 N'. One triangular solve per port against the already-factorised
    // linear matrix, so O(n^2) each -- the expensive part, done once per value
    // change rather than once per sample.
    for (int p = 0; p < portCount; ++p)
    {
        double* column = portTransfer.data() + static_cast<size_t>(p) * static_cast<size_t>(systemSize);
        std::fill(column, column + systemSize, 0.0);

        const int ra = portRowA[static_cast<size_t>(p)];
        const int rb = portRowB[static_cast<size_t>(p)];

        if (ra >= 0)
            column[ra] += 1.0;
        if (rb >= 0)
            column[rb] -= 1.0;

        solver.solveInPlace(column);
    }

    // K = N W: the impedance the circuit presents between each pair of ports.
    for (int p = 0; p < portCount; ++p)
    {
        const int ra = portRowA[static_cast<size_t>(p)];
        const int rb = portRowB[static_cast<size_t>(p)];

        for (int q = 0; q < portCount; ++q)
        {
            const double* column = portTransfer.data() + static_cast<size_t>(q) * static_cast<size_t>(systemSize);
            double k = 0.0;

            if (ra >= 0)
                k += column[ra];
            if (rb >= 0)
                k -= column[rb];

            portImpedance[static_cast<size_t>(p * portCount + q)] = k;
        }
    }
}

//==============================================================================
// Solving
//==============================================================================

bool Circuit::writeBackSolution(bool checkConvergence) noexcept
{
    bool converged = true;

    for (int row = 0; row < numNodeUnknowns; ++row)
    {
        const auto node = static_cast<size_t>(nodeOfRow[static_cast<size_t>(row)]);
        const double solved = work[static_cast<size_t>(row)];

        // DK decides convergence on the ports, not the nodes, so it asks for this
        // to be skipped -- it's a per-node abs, max and compare for an answer
        // nobody reads.
        if (checkConvergence)
        {
            const double previous = nodeVoltage[node];
            const double tolerance = convergenceAbsTolerance
                                   + convergenceRelTolerance * std::max(std::abs(previous), std::abs(solved));

            if (std::abs(solved - previous) > tolerance)
                converged = false;
        }

        nodeVoltage[node] = solved;
    }

    // The branch currents follow linearly from the node voltages, so they don't
    // get a say in convergence -- once the voltages settle, these have too.
    for (size_t s = 0; s < voltageSources.size(); ++s)
        voltageSources[s].current = work[static_cast<size_t>(numNodeUnknowns) + s];

    for (size_t a = 0; a < idealOpAmps.size(); ++a)
        idealOpAmps[a].current = work[static_cast<size_t>(idealOpAmpRowOffset) + a];

    {
        int windingRow = transformerRowOffset;

        for (auto& t : transformers)
            for (int w = 0; w < t.windingCount; ++w)
                t.windings[w].current = work[static_cast<size_t>(windingRow++)];
    }

    return converged;
}

bool Circuit::solveWithDk(double vIn) noexcept
{
    const auto n = static_cast<size_t>(systemSize);

    // Step one: solve the circuit as if the nonlinear devices weren't there.
    std::copy(rhs.begin(), rhs.end(), linearSolution.begin());
    solver.solveInPlace(linearSolution.data());

    for (int p = 0; p < portCount; ++p)
    {
        const int ra = portRowA[static_cast<size_t>(p)];
        const int rb = portRowB[static_cast<size_t>(p)];

        double v = portInputCoupling[static_cast<size_t>(p)] * vIn;
        if (ra >= 0)
            v += linearSolution[static_cast<size_t>(ra)];
        if (rb >= 0)
            v -= linearSolution[static_cast<size_t>(rb)];

        portOpenVoltage[static_cast<size_t>(p)] = v;
    }

    // Step two: Newton on the ports alone. v carries over from the previous
    // sample, which is why this usually takes two or three passes.
    //
    // With prediction on it carries over *extrapolated*: a port moving in a
    // straight line is more likely to end where the line points. A wild guess
    // costs nothing extra, since limitPortVoltages() damps it on the first pass
    // exactly as it damps any other overshoot.
    if (predictPortVoltages)
    {
        for (int p = 0; p < portCount; ++p)
        {
            const auto i = static_cast<size_t>(p);
            const double lastSample = portVoltage[i];

            portVoltage[i] = 2.0 * lastSample - portVoltagePrevious[i];
            portVoltagePrevious[i] = lastSample;
        }
    }
    else
    {
        // Kept in step even when unused, so switching prediction on mid-stream
        // extrapolates from real history rather than from stale zeros.
        std::copy(portVoltage.begin(), portVoltage.end(), portVoltagePrevious.begin());
    }
    //
    // Convergence is tested at the top of the loop rather than the bottom, so
    // that when it exits the port currents were evaluated at the very port
    // voltages it's exiting with. Testing at the bottom would leave the two one
    // step out of step, and step three below would then push slightly stale
    // currents back into the circuit.
    int iteration = 0;
    bool converged = false;
    bool stepWasSmall = false;

    while (iteration < maxNewtonIterations)
    {
        ++iteration;

        const bool limited = limitPortVoltages();
        linearisePorts();

        if (stepWasSmall && ! limited)
        {
            converged = true;
            break;
        }

        // F(v) = v - vOpen + K i(v),  J = I + K di/dv
        double* jacobian = portSolver.data();

        for (int p = 0; p < portCount; ++p)
        {
            double residual = portVoltage[static_cast<size_t>(p)] - portOpenVoltage[static_cast<size_t>(p)];

            for (int q = 0; q < portCount; ++q)
                residual += portImpedance[static_cast<size_t>(p * portCount + q)]
                          * portCurrent[static_cast<size_t>(q)];

            // J = I + K * di/dv. di/dv is block diagonal, so column q only draws
            // on the rows inside q's own device.
            for (const auto& [firstPort, blockSize] : deviceBlocks)
            {
                for (int q = firstPort; q < firstPort + blockSize; ++q)
                {
                    double kg = 0.0;
                    for (int r = firstPort; r < firstPort + blockSize; ++r)
                        kg += portImpedance[static_cast<size_t>(p * portCount + r)]
                            * portJacobian[static_cast<size_t>(r * portCount + q)];

                    jacobian[p * portCount + q] = (p == q ? 1.0 : 0.0) + kg;
                }
            }

            portResidual[static_cast<size_t>(p)] = -residual;
        }

        if (! portSolver.factorise())
            return false;

        portSolver.solveInPlace(portResidual.data());

        stepWasSmall = true;
        for (int p = 0; p < portCount; ++p)
        {
            const double step = portResidual[static_cast<size_t>(p)];
            const double v = portVoltage[static_cast<size_t>(p)] + step;

            if (std::abs(step) > convergenceAbsTolerance + convergenceRelTolerance * std::abs(v))
                stepWasSmall = false;

            portVoltage[static_cast<size_t>(p)] = v;
        }
    }

    lastIterationCount = iteration;

    // Step three: put the converged device currents back into the circuit.
    // z = zLinear - W i.
    std::copy(linearSolution.begin(), linearSolution.end(), work.begin());

    for (int p = 0; p < portCount; ++p)
    {
        const double current = portCurrent[static_cast<size_t>(p)];
        if (current == 0.0)
            continue;

        const double* column = portTransfer.data() + static_cast<size_t>(p) * n;
        for (size_t row = 0; row < n; ++row)
            work[row] -= column[row] * current;
    }

    writeBackSolution(false);
    return converged;
}

bool Circuit::runFullNewton(const double* baseMatrix, const double* baseRhs) noexcept
{
    if (systemSize == 0)
    {
        lastIterationCount = 0;
        return true;
    }

    const auto matrixElements = static_cast<size_t>(systemSize) * static_cast<size_t>(systemSize);
    const auto vectorElements = static_cast<size_t>(systemSize);

    if (! isNonlinear())
    {
        std::copy_n(baseMatrix, matrixElements, newtonSolver.data());
        std::copy_n(baseRhs, vectorElements, work.data());

        if (! newtonSolver.factorise())
            return false;

        newtonSolver.solveInPlace(work.data());
        writeBackSolution();
        lastIterationCount = 1;
        return true;
    }

    int iteration = 0;
    bool converged = false;

    while (iteration < maxNewtonIterations && ! converged)
    {
        ++iteration;

        std::copy_n(baseMatrix, matrixElements, newtonSolver.data());
        std::copy_n(baseRhs, vectorElements, work.data());

        readPortVoltagesFromNodes();
        const bool limited = limitPortVoltages();
        linearisePorts();
        stampPorts(newtonSolver.data(), work.data());

        if (! newtonSolver.factorise())
            return false;

        newtonSolver.solveInPlace(work.data());

        // A step the limiter had to damp isn't a converged one, however small the
        // resulting voltage change looked.
        converged = writeBackSolution() && ! limited;
    }

    lastIterationCount = iteration;
    return converged;
}

void Circuit::solveOperatingPoint() noexcept
{
    operatingPointConverged = true;

    if (systemSize == 0)
        return;

    // No signal at the input while we find the resting state.
    nodeVoltage[static_cast<size_t>(groundIndex)] = 0.0;
    if (inputIndex >= 0)
        nodeVoltage[static_cast<size_t>(inputIndex)] = 0.0;

    buildDcSystem(1.0);

    if (! runFullNewton(dcMatrix.data(), dcRhs.data()))
    {
        // Source stepping. Newton needs a starting guess in the right
        // neighbourhood, and for a biased transistor stage "everything at 0 V"
        // isn't: the supply is the whole reason any current flows. So bring the
        // supplies up from zero in steps, each solve warm-starting from the last.
        // At zero supply the answer is trivially zero volts everywhere, and each
        // step moves the operating point a little, so Newton always starts close.
        clearState();
        nodeVoltage[static_cast<size_t>(groundIndex)] = 0.0;
        if (inputIndex >= 0)
            nodeVoltage[static_cast<size_t>(inputIndex)] = 0.0;

        for (int step = 1; step <= sourceSteppingSteps; ++step)
        {
            buildDcSystem(static_cast<double>(step) / sourceSteppingSteps);
            operatingPointConverged = runFullNewton(dcMatrix.data(), dcRhs.data());
        }

        jassert(operatingPointConverged); // bias network doesn't resolve -- check the wiring
    }

    // Seed the companion models from the operating point: a capacitor holds its
    // DC voltage and passes no current, an inductor drops no voltage and carries
    // whatever current the short was passing.
    reversedCapacitorCount = 0;

    for (auto& c : capacitors)
    {
        c.vCapPrev = nodeVoltage[static_cast<size_t>(c.a)] - nodeVoltage[static_cast<size_t>(c.b)];
        c.iPrev = 0.0; // no current at DC, so no ESR drop to separate out

        // An electrolytic sitting backwards is a build error, not a sound.
        if (c.polarised && c.vCapPrev < -reverseBiasTolerance)
            ++reversedCapacitorCount;
    }

    jassert(reversedCapacitorCount == 0); // a polarised capacitor is wired backwards

    for (auto& l : inductors)
    {
        const double v = nodeVoltage[static_cast<size_t>(l.a)] - nodeVoltage[static_cast<size_t>(l.b)];
        l.vPrev = 0.0;
        l.iPrev = dcInductorConductance * v;
    }

    // Seed the port voltages too, so the first audio sample's Newton starts from
    // the operating point rather than from nothing.
    readPortVoltagesFromNodes();
}

//==============================================================================
// Per-sample processing
//==============================================================================

void Circuit::buildRightHandSide(double vIn) noexcept
{
    // Nothing in the cached system contributes a constant term: a source's
    // voltage is added below so it can vary, and the only other known node is
    // ground at 0 V. So the vector starts empty rather than from a stored one,
    // and the handful of rows vIn reaches are added individually.
    std::fill(rhs.begin(), rhs.end(), 0.0);

    double* rhsVector = rhs.data();

    for (const auto& [row, coefficient] : inputCouplingRows)
        rhsVector[row] += coefficient * vIn;

    for (size_t s = 0; s < voltageSources.size(); ++s)
        rhsVector[static_cast<size_t>(numNodeUnknowns) + s] += sourceVoltage(voltageSources[s]);

    for (const auto& c : capacitors)
        stampCurrentSource(rhsVector, c.a, c.b,
                           c.conductance * (c.vCapPrev + c.halfStepResistance * c.iPrev));

    for (const auto& l : inductors)
        stampCurrentSource(rhsVector, l.a, l.b, -(l.iPrev + l.conductance * l.vPrev));
}

void Circuit::updateReactiveState() noexcept
{
    for (auto& source : voltageSources)
    {
        if (source.acFrequency > 0.0)
        {
            source.phase += 2.0 * juce::MathConstants<double>::pi * source.acFrequency * dt;

            if (source.phase > 2.0 * juce::MathConstants<double>::pi)
                source.phase -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    for (auto& c : capacitors)
    {
        const double v = nodeVoltage[static_cast<size_t>(c.a)] - nodeVoltage[static_cast<size_t>(c.b)];
        const double vEquivalent = c.vCapPrev + c.halfStepResistance * c.iPrev;

        c.iPrev = c.conductance * (v - vEquivalent);

        // The capacitance itself only sees what's left after the ESR drop.
        c.vCapPrev = v - c.iPrev * c.esrOhms;
    }

    for (auto& l : inductors)
    {
        const double v = nodeVoltage[static_cast<size_t>(l.a)] - nodeVoltage[static_cast<size_t>(l.b)];
        const double ieq = -(l.iPrev + l.conductance * l.vPrev);

        l.iPrev = l.conductance * v - ieq;
        l.vPrev = v;
    }
}

float Circuit::process(float vIn)
{
    jassert(inputIndex >= 0 && outputIndex >= 0);    // call setInputNode()/setOutputNode() first
    jassert(rowOfNode.size() == nodeIndices.size()); // netlist grew since prepare() -- call prepare() again

    // Only on an actual value or topology change. Retrying on a failed
    // factorisation instead -- which is what `|| ! factorisationValid` did --
    // rebuilds an identical matrix and fails identically, once per sample: a
    // full O(n^3) stamp and factorise, 48000 times a second, all of it doomed.
    // The guard below already returns silence for that case, so the retry never
    // bought anything. linearDirty starts true and every setter re-sets it, so
    // no legitimate rebuild is skipped by this.
    if (linearDirty)
        rebuildLinearSystem();

    if (systemSize == 0 || ! factorisationValid)
        return 0.0f;

    nodeVoltage[static_cast<size_t>(inputIndex)] = vIn;
    buildRightHandSide(vIn);

    if (portCount == 0)
    {
        // Purely linear: the factorisation is still good, so this is just the
        // two triangular substitutions. Nothing iterates, so there is no
        // convergence to test -- asking for it would cost a per-node abs, max
        // and compare on every sample for an answer that is discarded.
        std::copy(rhs.begin(), rhs.end(), work.begin());
        solver.solveInPlace(work.data());
        writeBackSolution(false);
        lastIterationCount = 1;
    }
    else if (activeStrategy == SolverStrategy::DiscreteK)
    {
        if (! solveWithDk(vIn))
            ++nonConvergenceCount;
    }
    else if (! runFullNewton(linearMatrix.data(), rhs.data()))
    {
        ++nonConvergenceCount;
    }

    updateReactiveState();

    const auto out = static_cast<float>(nodeVoltage[static_cast<size_t>(outputIndex)] - outputOffset);

    // A non-finite sample means the solve came apart; drop the state rather than
    // let a NaN propagate into the host for the rest of the session.
    if (! std::isfinite(out))
    {
        clearState();
        return 0.0f;
    }

    return out;
}

void Circuit::process(float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
        samples[i] = process(samples[i]);
}
