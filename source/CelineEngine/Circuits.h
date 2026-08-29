#pragma once

#include "Engine.h"

//==============================================================================
/**
    Known circuits with known answers: the engine's test bench.

    **The plugin does not build circuits this way** -- it builds them from the
    drawing, through SchematicBuilder, and nothing in `source/` outside this file
    calls anything here. What keeps it is tests/CelineEngine.cpp and benchmarks/,
    which need circuits whose behaviour is known independently of this codebase:
    a one-pole whose corner is arithmetic, a tone stack with a published curve,
    an op-amp overdrive transcribed from a SPICE netlist. A drawn schematic
    cannot play that role -- it would only confirm that the builder and the
    engine agree with each other.

    Fixtures, then, not the way to define a circuit. Each is built purely on
    Circuit's public interface, which also makes them a fair test of it, and each
    factory returns a struct bundling the Circuit with whatever ids a test needs
    to turn a knob. Anything here with no caller is dead and should go.
*/
namespace Circuits
{

struct OnePole
{
    Circuit circuit;
    Circuit::ComponentId sweptComponent;
};

// Vin --[R]-- out --[C]-- gnd
OnePole makeOnePoleLowpass(double seriesR, double shuntC);

// Vin --[C]-- out --[R]-- gnd
OnePole makeOnePoleHighpass(double shuntR, double seriesC);

struct ToneStack
{
    Circuit circuit;

    Circuit::Potentiometer bassPot;
    Circuit::Potentiometer treblePot;
    Circuit::Potentiometer midPot;

    void setControls(float bass, float mid, float treble);
};

// The classic three-knob passive tone stack: a treble cap to the wiper, a bass
// cap to a series bass/mid pair. Copied into most valve guitar amps ever built.
ToneStack makeClassicToneStack(float bass = 0.5f, float mid = 0.5f, float treble = 0.5f);

//==============================================================================
/**
    A shunt diode clipper -- the distortion stage of most dirt pedals, and the
    simplest circuit that actually exercises
    the nonlinear solver:

        in --[Rseries]--+-- out
                        |
                       |>|
                       |<|
                        |
                       gnd

    Below the diodes' forward voltage they are open and the stage passes
    signal; above it they clamp and the series resistor takes up the difference.
    The shape of that transition -- the whole character of the distortion --
    comes out of the Shockley equation rather than a hand-tuned waveshaper.

    Values, diode type and how many are stacked per side are set in the function
    body in Circuits.cpp.
*/
Circuit makeDiodeClipper();

//==============================================================================
/**
    A common-emitter transistor gain stage running off a 9 V supply -- the
    building block almost every pedal is made of, and the smallest circuit that
    exercises both a transistor and a voltage source.

                       vcc (+9 V)
                        |     |
                       [Rb1] [Rc]
                        |     |
        in --][Cin]--+--+-----+---][Cout]-- out
                     |        |
                    [Rb2]     C
                     |     B--|
                     |        E
                    gnd       |
                             [Re]
                              |
                             gnd

    The base resistors hold the collector roughly midway between supply and
    ground, so the signal has room to swing both ways. prepare() solves that bias
    point before any audio arrives, so the stage starts up already biased.

    Gain is roughly Rc/Re, about 4.7x here. Bypassing Re raises it towards
    Rc/re', which is how the same topology becomes a fuzz.
*/
Circuit makeTransistorBooster();

//==============================================================================
/**
    The classic two-transistor germanium fuzz: PNP pair, positive ground, with
    the second stage's emitter degeneration set by the fuzz control.

    Positive ground is not a quirk to work around: it is what germanium PNP
    parts wanted, and why pedals of this shape refuse to share a supply.

    Returns a struct rather than a bare Circuit because it has knobs --
    addPotentiometer() hands back a handle, and without keeping it there is no
    way to move the pot afterwards.
*/
struct GermaniumFuzz
{
    Circuit circuit;

    Circuit::Potentiometer volumePot; // VR2, 500k at the output
    Circuit::Potentiometer fuzzPot;   // VR1, 1k in Q2's emitter leg

    /** Both controls run 0 to 1. */
    void setControls(float volume, float fuzz);
};

GermaniumFuzz makeGermaniumFuzz();

//==============================================================================
/**
    A 12AX7 preamp stage with a cathode-bias resistor -- the first gain stage of
    essentially every valve guitar amp, and the smallest circuit that exercises
    a triode.

                        B+ (300 V)
                         |
                        [Ra 100k]
                         |
        in --][Cin]--+---+-- plate --][Cout]-- out
                     |   |
                   [Rg]  V (triode)
                     |   |
                    gnd  cathode --+--[Rk 1k5]-- gnd
                                   |
                                  [Ck 22u]
                                   |
                                  gnd

    The cathode resistor is the whole trick: plate current flows through it and
    lifts the cathode a volt or two above ground, which is the same thing as
    holding the grid a volt or two *below* the cathode. The valve biases itself,
    with no negative supply needed. Ck bypasses that resistor for signal so the
    bias stays put while the gain doesn't get degenerated away -- take it out
    and the stage loses more than half its gain.

    Gain here is around 60. Drive the grid past the point where it goes positive
    and it starts drawing current through Cin, dragging the bias with it: that's
    the blocking distortion a real amp does when you hit it hard.

    The "fat" switch puts Ck in and out of circuit -- closed for full gain, open
    to leave Rk unbypassed, which degenerates the stage and costs most of it.
    Amps label this one bright, fat or lo/hi gain depending on who built them.
*/
struct TriodeStage
{
    Circuit circuit;
    Circuit::Switch cathodeBypass;
};

TriodeStage makeTriodeStage();

//==============================================================================
/**
    An EL34 power stage: pentode, cathode bias, and a resistive load standing in
    for the output transformer's reflected impedance.

    Not a full power amp -- there's no phase inverter and no push-pull pair, so
    this is one half of one, single-ended. It's here to show the pentode
    working and to be something to build a real output stage out of.

    Worth watching `getNodeVoltage("screen")` against the plate when you drive
    it: the screen keeps drawing current after the plate has given up, which is
    both what makes pentode clipping sound the way it does and what destroys
    output valves in amps run with a failing screen resistor.
*/
Circuit makePentodeStage();

//==============================================================================
/**
    A valve rectifier feeding a capacitor-input filter -- an amp's power supply,
    and the source of sag.

        ac ~ --|>|-- B+ --+--[Rfilter]--+
                          |             |
                        [Cres]        [Csmooth]        [Rload]
                          |             |                |
                         gnd           gnd              gnd

    Nothing here is in the signal path. What it produces is a B+ rail that
    droops when the load pulls hard and recovers when it lets go, on the time
    constant of the reservoir capacitor. Feed a power stage from it instead of
    from a fixed supply and you get the compression and bloom that a stiff
    solid-state supply doesn't give you.

    `loadOhms` stands in for the amp hanging off it -- make it smaller to see
    more sag. Swap makeVacuumRectifier for a silicon Diode in the netlist and
    the sag largely disappears, which is the modelling point.
*/
Circuit makeValveRectifierSupply(double loadOhms = 20000.0);

//==============================================================================
/**
    A three-valve preamp with a passive tone stack, transcribed from a SPICE
    netlist. This is the topology behind a large fraction of valve guitar amps:

        in -[5k]- jack -[68k]- V1 grid        12AY7, cathode bypassed
                                V1 plate -][C5]- volume pot
                                                   |
                        bright cap + switch across it
                                                   |
                                   wiper -[270k]- V2 grid   12AX7, unbypassed
                                                  V2 plate --+
                                                             | direct coupled
                                                  V3 grid ---+  12AX7 cathode
                                                  V3 cathode ---  follower
                                                      |
                                              [tone stack] --- out

    V3 is the part worth understanding. Its plate goes straight to B+ with no
    plate resistor, and the signal comes off its cathode -- a cathode follower.
    It has no voltage gain at all; it's there purely because the tone stack that
    follows is a heavy, low-impedance load that would flatten a normal gain
    stage. It's also direct-coupled to V2's plate, with no capacitor between
    them, so V3's grid sits at whatever V2's plate does, a couple of hundred
    volts up, and its cathode follows a little below that.

    Node names are readable rather than the netlist's N00x. The mapping:

        b+ = N001          v1plate = N005      trebleTop    = N003
        inputJack = N010   v1cathode = N013    slope        = N008
        v1grid = N011      volTop = N006       trebleBottom = N009
        v2grid = P001      volWiper = N012     bassMid      = N014
        v2plate = N004     brightCap = N007    midWiper     = N015
        v2cathode = P002   v3cathode = N002    out          = out

    Two notes on the transcription. The netlist's pot subcircuits take their
    pins as (end, end, wiper), which is what makes U3 -- listed as N009, N014,
    N009 -- a rheostat with its wiper strapped to one end, the way the 5F6-A
    wires its bass control. And `Rtap`/`tap` are ignored: no pot here brings a
    tap out to a node, so it has no effect.
*/
struct ThreeStagePreamp
{
    Circuit circuit;

    Circuit::Potentiometer volumePot;
    Circuit::Potentiometer treblePot;
    Circuit::Potentiometer bassPot; // wired as a rheostat, as in the real amp
    Circuit::Potentiometer midPot;

    /** The bright cap across the volume pot. Closed lifts the top end, and it
        does most of its work with the volume down -- wound up, there's little
        of the pot left for it to bypass. */
    Circuit::Switch brightSwitch;

    /** All four controls run 0 to 1. */
    void setControls(float volume, float bass, float mid, float treble);
};

ThreeStagePreamp makeThreeStagePreamp();

//==============================================================================
/**
    A mid-humped op-amp overdrive with its clipping diodes inside the feedback
    loop -- the topology behind a great many green-ish overdrive pedals.
    Transcribed from a SPICE netlist.

    Node names and component values are exactly as the netlist gave them, so
    this can be diffed against the schematic it came from. The parts that matter:

        U1          the gain stage, with the clipping in its feedback loop
        D1, D2      antiparallel silicon, across that loop -- this is the whole
                    character of the circuit. Clipping inside the feedback path
                    is soft and never fully flattens the wave, which is why it
                    compresses rather than fuzzes.
        DRIVE       500k rheostat in series with R3 (51k), setting the loop gain
        R2 + Cz     4k7 and 47n from the inverting input to ground -- the mid
                    hump this circuit is named for, and the reason it pushes
                    mids rather than sitting flat
        U2, TONE    the tone stage
        LEVEL       100k output pot

    Both op-amps run from 9 V with the signal biased to 4.5 V by V1, so the
    output has equal room in both directions.

    Both op-amps are NE5532. Swapping either for OpAmpModel::jrc4558() gives the
    part the pedal actually shipped with -- slower, noisier and a tenth the
    bandwidth, which is most of what people mean by the 4558 sound. Both models
    are built from their datasheets; see OpAmp.h for exactly which figures are
    specified and which are not.
*/
struct MidHumpOverdrive
{
    Circuit circuit;

    Circuit::Potentiometer tonePot;  // 20k
    Circuit::Potentiometer drivePot; // 500k, wired as a rheostat
    Circuit::Potentiometer levelPot; // 100k

    /** D1 and D2 -- the antiparallel pair inside U1's feedback loop.

        These are handles rather than indices for a reason. addOpAmp() builds its
        rail clamps out of ordinary diodes, so it consumes diode ids of its own,
        and in this circuit U1 is added before D1 -- which makes the clipping
        pair ids 2 and 3, not 0 and 1. Counting them by hand gets you the op-amp's
        clamps instead, and the circuit carries on working while ignoring
        whatever you thought you changed. */
    Circuit::ComponentId clipperUp = -1;
    Circuit::ComponentId clipperDown = -1;

    /** All three run 0 to 1. */
    void setControls(float tone, float drive, float level);

    /** Swaps both clipping diodes. Silicon is stock; the LEDs clip later and
        much more gradually. Call before prepare(), or call prepare() again. */
    void setClippingDiodes(const Circuit::DiodeModel& model);
};

MidHumpOverdrive makeMidHumpOverdrive();

} // namespace Circuits
