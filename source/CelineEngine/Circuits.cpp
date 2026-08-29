#include "Circuits.h"

namespace Circuits
{

OnePole makeOnePoleLowpass(double seriesR, double shuntC)
{
    OnePole result;
    result.circuit.addResistor("in", "out", seriesR);
    result.sweptComponent = result.circuit.addCapacitor("out", "gnd", shuntC);
    result.circuit.setInputNode("in");
    result.circuit.setOutputNode("out");
    return result;
}

OnePole makeOnePoleHighpass(double shuntR, double seriesC)
{
    OnePole result;
    result.circuit.addCapacitor("in", "out", seriesC);
    result.sweptComponent = result.circuit.addResistor("out", "gnd", shuntR);
    result.circuit.setInputNode("in");
    result.circuit.setOutputNode("out");
    return result;
}

void ToneStack::setControls(float bass, float mid, float treble)
{
    bassPot.setPosition(circuit, bass);
    midPot.setPosition(circuit, mid);
    treblePot.setPosition(circuit, treble);
}

ToneStack makeClassicToneStack(float bass, float mid, float treble)
{
    ToneStack ts;

    // Rin - Vin---n1 1300ohms
    ts.circuit.addResistor("in", "n1", 1300.0f);

    // R1 - n1---n3 56kohms
    ts.circuit.addResistor("n1", "n3", 56000.0f);

    // C1 - n1---n2 250pF
    ts.circuit.addCapacitor("n1", "n2", 250.0e-12f);

    // C2 - n3---n4 20nF
    ts.circuit.addCapacitor("n3", "n4", 20.0e-9f);

    // C3 - n3---n5 20nF
    ts.circuit.addCapacitor("n3", "n5", 20.0e-9f);

    // RL - out---gnd 1Mohms
    ts.circuit.addResistor("out", "gnd", 1000000.0f);

    // Bass pot (1 MΩ variable resistor / rheostat): n4 to n6
    ts.bassPot = ts.circuit.addVariableResistor("n4", "n6", 1000000.0f, 1.0f, Circuit::Potentiometer::Taper::Logarithmic);

    // Mid pot (25 kΩ): pin 1 = n6, wiper = n5 (output), pin 3 = gnd
    ts.midPot = ts.circuit.addPotentiometer("n6", "n5", "gnd", 25000.0f);

    // Treble pot (250 kΩ): pin 1 = n2, wiper = out (output), pin 3 = n4
    ts.treblePot = ts.circuit.addPotentiometer("n2", "out", "n4", 250000.0f);

    // IO
    ts.circuit.setInputNode("in");
    ts.circuit.setOutputNode("out");

    // Initialize with controls (values between 0 and 1)
    ts.setControls(bass, mid, treble);

    return ts;
}

//==============================================================================
// Diode clipper
//==============================================================================

Circuit makeDiodeClipper()
{
    // Edit these to change the sound.
    //   diode        -- silicon() / germanium() / schottky() / led(), in order of
    //                   increasing forward voltage, so increasing clean headroom
    //                   before the stage starts clipping.
    //   seriesCount  -- diodes stacked per side. Give the two sides different
    //                   counts for asymmetric clipping (and so even harmonics).
    const auto diode = Circuit::DiodeModel::led();
    constexpr int forwardSeriesCount = 1;
    constexpr int reverseSeriesCount = 1;

    Circuit circuit;

    // Series resistor -- everything the diodes clamp away is dropped across this,
    // so it sets how hard the stage is driven for a given input level. Lower it
    // for more clipping at the same input, raise it for less.
    circuit.addResistor("in", "out", 1300.0);

    // Antiparallel pair: the first conducts on the positive half of the wave,
    // the second on the negative.
    circuit.addDiode("out", "gnd", diode, forwardSeriesCount);
    circuit.addDiode("gnd", "out", diode, reverseSeriesCount);

    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    return circuit;
}

//==============================================================================
// Transistor booster
//==============================================================================

Circuit makeTransistorBooster()
{
    // Edit these to change the sound.
    //   transistor -- npnSilicon() / pnpSilicon() / npnGermanium() / pnpGermanium().
    //                 For a PNP you also need to flip the supply: swap "vcc" and
    //                 "gnd" in the addVoltageSource call below (positive ground,
    //                 as in a positive-ground germanium fuzz).
    const auto transistor = Circuit::BjtModel::npnSilicon();
    constexpr double supplyVolts = 9.0;

    constexpr double collectorR = 4700.0;   // Rc -- with Re, sets the gain
    constexpr double emitterR = 1000.0;     // Re
    constexpr double biasUpperR = 100000.0; // Rb1, vcc to base
    constexpr double biasLowerR = 22000.0;  // Rb2, base to gnd

    constexpr double inputCap = 1.0e-6;    // Cin, blocks DC coming in
    constexpr double outputCap = 100.0e-9; // Cout, blocks the collector's DC on the way out
    constexpr double loadR = 1000000.0;    // whatever the next stage looks like

    Circuit circuit;

    circuit.addVoltageSource("vcc", "gnd", supplyVolts);

    // Bias divider. These two set where the stage rests with no signal.
    circuit.addResistor("vcc", "base", biasUpperR);
    circuit.addResistor("base", "gnd", biasLowerR);

    // Signal in through a coupling cap, so the guitar doesn't disturb the bias.
    circuit.addCapacitor("in", "base", inputCap);

    circuit.addResistor("vcc", "collector", collectorR);
    circuit.addResistor("emitter", "gnd", emitterR);

    // Add a large capacitor here in parallel with Re for much more gain:
    //   circuit.addCapacitor ("emitter", "gnd", 47.0e-6);
    circuit.addTransistor("base", "collector", "emitter", transistor);

    // Out through a coupling cap into the next stage's input impedance.
    circuit.addCapacitor("collector", "out", outputCap);
    circuit.addResistor("out", "gnd", loadR);

    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    return circuit;
}

//==============================================================================
// Germanium fuzz
//==============================================================================

void GermaniumFuzz::setControls(float volume, float fuzz)
{
    volumePot.setPosition(circuit, volume);
    fuzzPot.setPosition(circuit, fuzz);
}

GermaniumFuzz makeGermaniumFuzz()
{
    GermaniumFuzz pedal;
    auto& circuit = pedal.circuit;

    // 9V
    circuit.addVoltageSource("vcc", "gnd", 9.0);

    // R1
    circuit.addResistor ("vcc", "n1", 330);
    // R2
    circuit.addResistor ("vcc", "Q1c_Q2b", 33000);
    // R3
    circuit.addResistor ("n2", "n3", 100000);
    // R4
    circuit.addResistor ("n1","Q2c",8200);

    // C1
    circuit.addCapacitor("n2", "in", 2.2e-6);
    // C2
    circuit.addCapacitor("n4", "gnd", 22e-6);
    // C3
    circuit.addCapacitor("n1", "pout", 0.01e-6);

    // RV1
    pedal.fuzzPot = circuit.addPotentiometer ("n3","n4","gnd",1000, Circuit::Potentiometer::Taper::Linear);
    // RV2
    pedal.volumePot = circuit.addPotentiometer("pout","out","gnd",500000, Circuit::Potentiometer::Taper::Logarithmic);

    //Q1
    circuit.addTransistor ("n2","Q1c_Q2b","gnd",Circuit::BjtModel::npn2N2222A());
    //Q2
    circuit.addTransistor ("Q1c_Q2b","Q2c","n3",Circuit::BjtModel::npn2N2222A());


    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    pedal.setControls(0.5f, 0.5f);

    return pedal;
}

//==============================================================================
// Valve stages
//==============================================================================

TriodeStage makeTriodeStage()
{
    // Edit these to change the sound.
    //   valve -- ecc83() / ecc81() / ecc82(), in descending order of gain.
    const auto valve = Circuit::TriodeModel::ecc83();
    constexpr double supplyVolts = 300.0;

    constexpr double plateR = 100000.0;   // Ra
    constexpr double cathodeR = 1500.0;   // Rk -- sets the bias point
    constexpr double cathodeCap = 22.0e-6;// Ck -- bypasses Rk for signal
    constexpr double gridR = 1000000.0;   // grid leak, holds the grid at 0 V DC
    constexpr double inputCap = 22.0e-9;  // Cin
    constexpr double outputCap = 22.0e-9; // Cout
    constexpr double loadR = 1000000.0;   // next stage's grid leak

    TriodeStage stage;
    auto& circuit = stage.circuit;

    circuit.addVoltageSource("b+", "gnd", supplyVolts);
    circuit.addResistor("b+", "plate", plateR);

    circuit.addCapacitor("in", "grid", inputCap);
    circuit.addResistor("grid", "gnd", gridR);

    circuit.addResistor("cathode", "gnd", cathodeR);

    // Ck reaches ground through the switch, so opening it strands the capacitor
    // and leaves Rk unbypassed.
    circuit.addPolarisedCapacitor("cathode", "ckBottom", cathodeCap, 0.5);
    stage.cathodeBypass = circuit.addSwitch("ckBottom", "gnd", true);

    circuit.addTriode("plate", "grid", "cathode", valve);

    circuit.addCapacitor("plate", "out", outputCap);
    circuit.addResistor("out", "gnd", loadR);

    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    return stage;
}

Circuit makePentodeStage()
{
    // Edit these to change the sound.
    //   valve -- el34() / u6l6gc() / el84().
    const auto valve = Circuit::PentodeModel::el34();
    constexpr double supplyVolts = 400.0;

    constexpr double loadR = 4000.0;      // plate load
    constexpr double screenR = 1000.0;    // the screen-stopper every amp has
    constexpr double cathodeR = 680.0;    // Rk, cathode bias
    constexpr double cathodeCap = 100.0e-6;
    constexpr double gridR = 220000.0;
    constexpr double inputCap = 22.0e-9;

    Circuit circuit;

    circuit.addVoltageSource("b+", "gnd", supplyVolts);

    // A plain resistive plate load, which is a real circuit (an EF86 preamp
    // stage is wired this way) but is NOT an output transformer. A transformer
    // primary has almost no DC resistance and presents its reflected impedance
    // only to AC, so a real power valve idles with its plate nearly at B+ and
    // swings around that. Here the load drops a couple of hundred volts at
    // idle, so the valve sits far lower and has much less room to swing.
    // Modelling a real output stage needs the transformer -- an inductor with a
    // reflected load -- which is the next thing to build.
    circuit.addResistor("b+", "plate", loadR);
    circuit.addResistor("b+", "screen", screenR);

    circuit.addCapacitor("in", "grid", inputCap);
    circuit.addResistor("grid", "gnd", gridR);

    circuit.addResistor("cathode", "gnd", cathodeR);
    circuit.addPolarisedCapacitor("cathode", "gnd", cathodeCap, 0.2);

    circuit.addPentode("plate", "screen", "grid", "cathode", valve);

    circuit.setInputNode("in");
    circuit.setOutputNode("plate");

    return circuit;
}

Circuit makeValveRectifierSupply(double loadOhms)
{
    // Edit these to change how much the supply sags.
    //   rectifier -- gz34() stiffest, u5u4gb() softer, u5y3gt() saggiest.
    //                Or swap the addVacuumDiode call for addDiode() to get a
    //                solid-state rectifier and almost no sag at all.
    const auto rectifier = Circuit::VacuumDiodeModel::u5u4gb();

    constexpr double secondaryPeak = 450.0; // transformer secondary, peak volts
    constexpr double mainsFrequency = 50.0;
    constexpr double reservoirCap = 32.0e-6;
    constexpr double smoothingCap = 32.0e-6;
    constexpr double filterR = 1000.0;

    Circuit circuit;

    // A centre-tapped secondary: two windings in antiphase about ground. The
    // second source is wired backwards, which is what puts it 180 degrees out.
    // Each half feeds one of the rectifier's two plates, so the reservoir gets
    // topped up twice per mains cycle instead of once -- full-wave, which is
    // what every amp actually does and why the ripple is at twice mains.
    circuit.addAcVoltageSource("acA", "gnd", secondaryPeak, mainsFrequency);
    circuit.addAcVoltageSource("gnd", "acB", secondaryPeak, mainsFrequency);

    circuit.addVacuumDiode("acA", "b+", rectifier);
    circuit.addVacuumDiode("acB", "b+", rectifier);

    circuit.addPolarisedCapacitor("b+", "gnd", reservoirCap, 1.0);
    circuit.addResistor("b+", "smoothed", filterR);
    circuit.addPolarisedCapacitor("smoothed", "gnd", smoothingCap, 1.0);

    // Whatever the amp draws.
    circuit.addResistor("smoothed", "gnd", loadOhms);

    // Nothing modulates this circuit -- the input node exists only because every
    // Circuit needs one.
    circuit.setInputNode("in");
    circuit.setOutputNode("smoothed");

    return circuit;
}

//==============================================================================
// Three-stage valve preamp
//==============================================================================

void ThreeStagePreamp::setControls(float volume, float bass, float mid, float treble)
{
    volumePot.setPosition(circuit, volume);
    bassPot.setPosition(circuit, bass);
    midPot.setPosition(circuit, mid);
    treblePot.setPosition(circuit, treble);
}

ThreeStagePreamp makeThreeStagePreamp()
{
    ThreeStagePreamp preamp;
    auto& circuit = preamp.circuit;

    // V2 in the netlist: the B+ rail.
    circuit.addVoltageSource("b+", "gnd", 325.0);

    //--------------------------------------------------------------------------
    // Input, and V1 -- the 12AY7 first stage
    //--------------------------------------------------------------------------

    // The netlist's source carries Rser=5K. Our input node is an ideal source,
    // so the guitar's output impedance has to be a resistor of its own.
    circuit.addResistor("in", "inputJack", 5000.0);

    circuit.addResistor("inputJack", "gnd", 1000000.0); // R2, grid leak
    circuit.addResistor("inputJack", "v1grid", 68000.0); // R3, grid stopper

    circuit.addResistor("v1cathode", "gnd", 820.0); // R4
    circuit.addPolarisedCapacitor("v1cathode", "gnd", 250.0e-6, 0.2); // C4, cathode bypass
    circuit.addResistor("b+", "v1plate", 100000.0); // R5

    circuit.addTriode("v1plate", "v1grid", "v1cathode", Circuit::TriodeModel::ecc12ay7());

    //--------------------------------------------------------------------------
    // Volume control, with the bright cap across it
    //--------------------------------------------------------------------------

    circuit.addCapacitor("volTop", "v1plate", 20.0e-9); // C5
    preamp.volumePot = circuit.addPotentiometer("volTop", "volWiper", "gnd", 1000000.0); // U5

    // C6 in the netlist runs from the wiper to N007 and stops there, because the
    // switch that closes the loop back to the top of the pot isn't in the
    // exported netlist. This is that switch.
    circuit.addCapacitor("volWiper", "brightCap", 100.0e-12); // C6
    preamp.brightSwitch = circuit.addSwitch("brightCap", "volTop", true);

    //--------------------------------------------------------------------------
    // V2 -- 12AX7 gain stage, cathode deliberately unbypassed
    //--------------------------------------------------------------------------

    circuit.addResistor("v2grid", "volWiper", 270000.0); // R7, grid stopper
    circuit.addResistor("v2cathode", "gnd", 820.0);      // R8, no bypass cap
    circuit.addResistor("b+", "v2plate", 100000.0);      // R9

    circuit.addTriode("v2plate", "v2grid", "v2cathode", Circuit::TriodeModel::ecc83());

    //--------------------------------------------------------------------------
    // V3 -- 12AX7 cathode follower, direct-coupled to V2's plate
    //--------------------------------------------------------------------------

    // Plate straight to B+, no plate resistor: all the output comes off the
    // cathode. No gain, but it can drive the tone stack without being loaded down.
    circuit.addTriode("b+", "v2plate", "v3cathode", Circuit::TriodeModel::ecc83());
    circuit.addResistor("v3cathode", "gnd", 100000.0); // R10

    //--------------------------------------------------------------------------
    // Tone stack, fed from the cathode follower
    //--------------------------------------------------------------------------

    circuit.addCapacitor("v3cathode", "trebleTop", 250.0e-12); // C1
    circuit.addResistor("v3cathode", "slope", 56000.0);        // R1, the slope resistor
    circuit.addCapacitor("trebleBottom", "slope", 20.0e-9);    // C2
    circuit.addCapacitor("midWiper", "slope", 20.0e-9);        // C3

    // U2: ends trebleTop and trebleBottom, wiper is the output.
    preamp.treblePot = circuit.addPotentiometer("trebleTop", "out", "trebleBottom", 250000.0);

    // U3: listed as N009, N014, N009 -- wiper strapped to one end, so it's a
    // plain variable resistance rather than a divider.
    preamp.bassPot = circuit.addVariableResistor("trebleBottom", "bassMid", 250000.0, 1.0);

    // U4: ends ground and bassMid, wiper feeds C3.
    preamp.midPot = circuit.addPotentiometer("gnd", "midWiper", "bassMid", 25000.0);

    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    preamp.setControls(0.5f, 0.5f, 0.5f, 0.5f);

    return preamp;
}

//==============================================================================
// Mid-hump op-amp overdrive
//
// Generated from the overdrive netlist by tools/netlist_to_circuit.py, left as it
// came out so it stays comparable with the netlist.
//==============================================================================

void MidHumpOverdrive::setControls(float tone, float drive, float level)
{
    tonePot.setPosition(circuit, tone);
    drivePot.setPosition(circuit, drive);
    levelPot.setPosition(circuit, level);
}

void MidHumpOverdrive::setClippingDiodes(const Circuit::DiodeModel& model)
{
    circuit.setDiodeModel(clipperUp, model);
    circuit.setDiodeModel(clipperDown, model);
}

MidHumpOverdrive makeMidHumpOverdrive()
{
    MidHumpOverdrive result;
    auto& circuit = result.circuit;

    // V1 -- the 4.5 V bias rail
    circuit.addVoltageSource("n011", "gnd", 4.5);
    // V2 -- the input

    // C1
    circuit.addCapacitor("n010", "in", 1e-6);
    // R1
    circuit.addResistor("n010", "n011", 10000);
    // C6
    circuit.addCapacitor("n008", "n005", 1e-6);
    // U1 -- the gain stage. Swap ne5532() for jrc4558() to hear the part the
    // pedal actually shipped with; everything else stays put.
    circuit.addOpAmp("U1", "n010", "n003", "n001",
                     Circuit::OpAmpModel::ne5532().withRails(9, 0));
    // Cc
    circuit.addCapacitor("n001", "n003", 51e-12);
    // D1, D2 -- the clipping pair, inside U1's feedback loop.
    // Swap both for redLed() / greenLed() / blueLed() to raise the clipping
    // threshold and soften the knee, which is the commonest mod to this circuit.
    result.clipperUp = circuit.addDiode("n001", "n003", Circuit::DiodeModel::d1n4148());
    result.clipperDown = circuit.addDiode("n003", "n001", Circuit::DiodeModel::d1n4148());
    // R2
    circuit.addResistor("n003", "p001", 4700);
    // Cz
    circuit.addCapacitor("p001", "gnd", 47e-9);
    // R3
    circuit.addResistor("n002", "n003", 51000);
    // R5
    circuit.addResistor("n001", "n009", 1000);
    // U2 -- the tone stage.
    circuit.addOpAmp("U2", "n009", "n004", "n005",
                     Circuit::OpAmpModel::ne5532().withRails(9, 0));
    // R8
    circuit.addResistor("n005", "n004", 1000);
    // C2
    circuit.addCapacitor("n006", "n007", 220e-9);
    // R4
    circuit.addResistor("n007", "gnd", 220);
    // X§TONE (PotLin, 10000 ohm, set=0.5)
    result.tonePot = circuit.addPotentiometer("n004", "n006", "n009", 20000, Circuit::Potentiometer::Taper::Linear);
    // X§DRIVE (PotLin, 500000 ohm, set=0.5)
    result.drivePot = circuit.addVariableResistor("n001", "n002", 500000, 1.0, Circuit::Potentiometer::Taper::Logarithmic);
    // wiper is strapped to one end, so this is a rheostat
    // X§LEVEL (PotLin, 100000 ohm, set=0.5)
    result.levelPot = circuit.addPotentiometer("n008", "out", "gnd", 100000, Circuit::Potentiometer::Taper::Logarithmic);
    // C4
    circuit.addCapacitor("n009", "gnd", 220e-9);
    // R11
    circuit.addResistor("n009", "n011", 10000);

    circuit.setInputNode("in");
    circuit.setOutputNode("out");

    return result;
}

} // namespace Circuits
