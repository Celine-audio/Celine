# Known limitations of the circuit engine

What this simulator does not do, ordered by how much it changes what you hear.
Everything here is a deliberate boundary rather than a defect — but a boundary
you should know about before trusting a result or blaming a circuit.

The engine solves a netlist by modified nodal analysis, re-linearising the
nonlinear devices with Newton–Raphson each sample and eliminating the linear
part ahead of time (the DK method). It is component-level and physically
structured: what it gets wrong, it gets wrong for reasons listed below, not
because a curve was drawn by ear.

---

## 1. Aliasing — the big one

**Oversampling is off by default.** `process()` runs one circuit solve per input
sample at whatever rate the host provides, unless the Settings menu raises it to
2x, 4x, 8x or 16x.

Measured on the default sheet (the diode clipper) driven 24 dB hot with a
1.88 kHz tone, the ratio of fold-down to genuine harmonic content, and the
whole plugin's time for one stereo 512-sample block at 48 kHz in Release:

    factor   alias / harmonic   diode clipper        12AX7 gain stage
      1x         -23.7 dB       242 us   2.3%        509 us    4.8%
      2x         -43.7 dB       444 us   4.2%       1011 us    9.5%
      4x         -59.8 dB       832 us   7.8%       1934 us   18.1%
      8x         -93.1 dB      1556 us  14.6%       3537 us   33.2%
     16x         -99.1 dB      2871 us  26.9%       6927 us   64.9%

The percentage is of one core. Each doubling costs a little under twice the
CPU -- Newton needs fewer iterations when consecutive samples are closer -- and
the first buys the most per unit of it: 20 dB for less than double. 8x is where
the aliasing stops being the thing you hear; 16x buys 6 dB more for twice the
CPU again, sits near the floor the measurement can see, and on anything bigger
than one valve stage is a setting for rendering, not for playing. A 96 kHz
session doubles every figure in the table.

(The alias figures are from `Oversampling trades CPU for aliasing` in
`tests/PluginBasics.cpp`, the timings from `Oversampling cost` in
`benchmarks/`. An earlier version of that test windowed an off-bin tone with a
Hann window, whose own leakage read as -57 dB of aliasing: it put 4x at -53.5
and could not see past 8x.)

Every nonlinear device in here generates harmonics, and clipping generates them
without limit. At 48 kHz, everything a diode produces above 24 kHz folds back
down into the audio band as inharmonic content. It does not sound like
distortion; it sounds like grit that moves the wrong way when you play up the
neck.

Left at 1×, this is the single largest gap between this engine and a commercial
amp sim, larger than any modelling detail below it. A hard-driven overdrive
at 48 kHz aliases audibly.

Oversampling is the only real fix — nothing downstream can remove energy that
has already folded down. Everything else is mitigation, and two of them are
free: **bandlimit the input** (a guitar has almost nothing above 8 kHz, and the
aliasing you never generate costs nothing to remove), and **leave the valve
interelectrode capacitance on**, since those capacitors are exactly what
bandlimits each stage before the next one multiplies its harmonics up again.
Turning that off to save 23% makes this worse, which is a tension worth knowing
about before reaching for it.

Note also that the circuit measurements in `benchmarks/` are at 1×, so budget
with the table above: the op-amp overdrive at ~6% of a core per channel is
roughly 11% at 2×, 21% at 4×, 40% at 8× and 75% at 16×.

## 2. Missing physics that changes the sound

### Valves: what the models can and can't do

Every valve here bar one is fitted to a JJ datasheet and reproduces Ia, gm and ra
at the quoted operating point (see `Triode.h`, `Pentode.h`, `VacuumDiode.h`). The
exception is **12AX7 GE**, fitted instead to General Electric's ET-T509B sheet
and to 18 of the resistance-coupled stages tabulated on it; it makes the opposite
trade deliberately, and the two paragraphs below are about the trade it declines
to make. That
is a stronger claim than it sounds — Koren's own published parameter sets, which
this project used before, were 20–30% out at that point on the triodes and had
plate resistance two to six times too high on the pentodes. But fitting a point
is not the same as modelling a valve, and these are the gaps that remain.

**Koren's triode can't fit the operating point and the curve family at once.**
Measured on the ECC83S: fitting the datasheet's Ia, gm and ra exactly leaves
0.51 mA rms against the published transfer curve; fitting the curve instead gets
that to 0.02 mA but pulls gm 23% low. Both can't be had. The stated figures win
here because they're the manufacturer's measurements, but it means the model is
most trustworthy *near* the operating point and drifts away from it. The
Dempwolf–Zölzer DAFx-11 model addresses exactly this by parametrising the
exponent — it is implemented, and the three measured 12AX7s in the Triode
dropdown use it.

GE's sheet lets that trade be priced rather than argued about, because it
tabulates 18 real resistance-coupled stages with their gains. Against those, in
the DK engine, the JJ-fitted `ecc83()` is **32.5% rms out**, and the error tracks
the supply — roughly −4% at 300 V, −23% at 180 V, −50% at 90 V. `ecc83ge()` is
**2.5%** over the same stages with no supply bias, and misses the published
250 V point by about 6% instead of nailing it. Neither is the "right" answer;
they are the two ends of the same trade. `[ge]` in `tests/CelineEngine.cpp`
prints the table for both.

**Fitting Ri forces KVB somewhere unphysical.** The triode fits land KVB at
16 000–24 000 where Koren's sets use 300. It reproduces the datasheet and tracks
the plate curves better, but KVB is supposed to shape the low-plate-voltage knee,
and a value that large means it is doing something else. Concretely: KVB sets
where `sqrt(KVB + Vp²)` hands over from constant to linear, so at 23 706 that
happens at 154 V and every stage biased below it is on the wrong side of the
model's knee. For the JJ-fitted valves, treat behaviour below about 50 V of plate
voltage as unsupported by any measurement. **12AX7 GE is the exception**: its
KVB of 4793 puts the transition at 69 V, and the 90 V rows it was fitted against
run their plates at roughly 30–60 V, so that region is measured rather than
extrapolated for that model alone. 4793 is still well above the 300–600 usually
quoted for a 12AX7 — forcing it into that band costs the stage table 2.5% → 7.4%,
so the data does want it high, but nobody should call the number physical.

**No Koren parameter set fits everything at once.** Fitting the 18 stages, the
Eb = 100 V gm/ra characteristic, the 250 V point and the absolute Ia at a stated
Vg together is not possible in this formulation: adding the last as a target
drove MU past 110 and EX down to 1.05 and still missed it by 13%. Both 12AX7
Koren models are consequently 22.5% out on absolute Ia(Vg) — the GE fit does not
regress it, but it does not fix it either. A cathode-biased stage is insensitive
to this because the bias point self-adjusts, which is why the stage table can be
right while this is not; a fixed-bias stage is not.

**Grid conduction is still a guess.** `gridPerveance` is 1e-3 for every triode
and pentode, chosen as plausible rather than fitted — datasheets simply don't
publish grid-current curves. It matters: grid current is what charges the
coupling capacitor and shifts the bias when a stage is driven hard, which is
blocking distortion, the splutter of an overdriven amp. Having it non-zero gets
the behaviour qualitatively; the amount is not calibrated, and it draws about
60% too much at Vg = +1 V.

The three **measured 12AX7** models are the exception and the way out: they use
the Dempwolf–Zölzer formulation, whose grid current was measured on real bottles
rather than assumed (Gg, ξ, Cg, Ig0 from Table 1 of the paper). Pick one of those
if grid conduction is what you are listening for. They cost about 2% more per
sample than Koren.

What *is* now right in both formulations is where that current comes from. Koren
fitted his curve to plate current at a negative grid, where the plate collects
everything the cathode emits — so continued past a positive grid it is a cathode
current, and the grid's share has to come out of it. The triode used to add grid
current alongside it, which had the cathode emitting Ik + Ig. It is subtracted
now, as the Dempwolf–Zölzer path always did, so `Ia + Ig = Ik` holds in both. The
size of Ig is still the guess above; only its bookkeeping is fixed. Nothing moves
below a positive grid, which is every stage in normal operation.

The pentode is deliberately not changed to match. Its split is three ways
(Ia + Ig2 + Ig1 = Ik) and how the control grid's interception divides between
plate and screen is not something the datasheets settle, so guessing it would
trade a known-wrong bookkeeping for an invented one.

**Screen current doesn't depend on plate voltage.** Koren's pentode makes Ig2 a
function of the grids alone. In a real valve, when the plate voltage collapses
into clipping the plate stops taking current and the screen takes it instead —
which is what destroys output valves and part of what a hard-driven power stage
sounds like. Here the screen carries on regardless, and the cathode current goes
down with the plate: on the EL34 fit it falls 66% between Ua 250 V and Ua 20 V,
which is not something a hot cathode does.

*Gurskii's fix was tried and does not work.* The standard community repair
(audioXpress 2/2011) makes the screen take the plate's complement,
`Ig2 = 2*E1^EX*(pi/2 - atan(Vpk/KVB))/KG2`, with KG2 rescaled by `atan(KVB/Ua)`
so the fitted operating point is preserved exactly. It does fix the cathode
current — 9% droop instead of 66% — but the Mullard EL34 transfer
characteristics (sheet 2033, Va = Vg2 = 250/375/425 V) refute it. Reading where
each Ig2 curve crosses 25 mA:

    curve         sheet    Koren    Gurskii
    1  250/250    -6 V     -6.6 V   -6.6 V     (control: identical by construction)
    2  375/375   -16 V    -18.5 V  -12.6 V
    3  425/425   -22 V    -23.3 V  -15.1 V

In absolute terms at those points Koren reads 26/30/28 mA against the sheet's 25,
and Gurskii 26/20/16. Koren is the better of the two on both curves that move.

No knee width rescues it, because one KVB sets both the high-Va falloff and the
gain at collapse, and `pi/2 - atan(Va/KVB)` decays as 1/Va forever where a real
screen current flattens above the knee:

    KVB2      Ig2(425)/Ig2(250)      screen gain at plate collapse
      59            0.595                       6.8x
     400            0.746                       1.6x
    5000            0.977                       1.0x    (i.e. back to Koren)

The data wants the first column near 1.0; getting there costs all of the second.
A form that is flat above the knee and rises below it would satisfy both, but
fitting one needs screen-current data in the knee region, and no sheet here has
any — all three published curves sit at Va >= 250 V, well above it.

What is also left is the other axis. Both currents carry the same E1^EX, so at a
fixed plate voltage their *ratio* is fixed however the grid moves. The 6V6S note
in `Pentode.h` has the numbers: the sheet's two columns give Ia/Ig2 of 9 and 14,
and the model cannot be both.

**No secondary emission.** Beam tetrodes and pentodes show a kink in the plate
curves at low plate voltage where secondary electrons flow backwards. Not
modelled, and it sits exactly where a power stage goes when clipping hard.

**The plate-slope term fixed Koren's knee, not everything about the plate
curves.** Koren's single arctangent sets both the knee and the plate resistance,
so fitting Ri pinned KVB near 50 V on every pentode and left the knee far too
soft. Every pentode with a pentode plate family on its sheet now carries a
factor (1 + Ua/VA) that takes the slope and frees KVB for the knee, refitted with
the operating point held exactly. Against the families digitised from
`docs/specs/tubes` (rms, every 10 V from Ua 30 V):

    valve   sheet's Ug2   Koren's form   now    knee KVB
    EL34       250 V        17.7 mA     7.8 mA    13 V
    EL84       250 V        16.2         6.8       6 V
    KT88       300 V        18.8        10.6      14 V
    6L6GC      400 V        14.0         9.2      23 V   (MU, EX refitted too)
    KT77       250 V         8.9         8.2      34 V
    KT66       400 V        27.3        23.5      10 V

What is left, valve by valve:

- **KT66** — the rest of its 23.5 mA is how current scales with *screen*
  voltage: its family is at 400 V, its operating point at 250, and its low
  curves at 400 V sit 15–25 mA under the model (67 mA on Ug1 −35 V where the
  model draws 93). Koren's grid law cannot hold both; freeing MU and EX
  reaches 17.8 mA only at MU 50, which a −50 V curve still conducting at 400 V
  rules out, and the sheet's triode Ri of 1.3k is unreachable alongside the
  family (the model gives 1.07k).
- **KT88** — one slope for the whole family cannot give the coldest curves
  the steeper one they have above 300 V (Ug1 −30 V: 74 mA at 250, 87 at 400 on
  the sheet; 64 and 69 here).
- **EL34** — the knee is now right on the upper curves and overshoots on the
  lower ones: 54 mA at Ua 50 on Ug1 −15 V against the sheet's 40.
- **6V6S** — still in Koren's form, because its sheet has no pentode plate
  family; its triode-connected curves cannot separate knee from slope. Held
  out, they land within 4.1 mA rms of the model.

A sharper knee is a sharper corner for Newton. A pentode starved down to a
fraction of a volt above its cathode — the screen taking the cathode current
through a big plate load — biases right beside the floor where plate current
stops, and the refitted KT66 overshot past it and settled at −149 V until the
pentode's step limiter learned not to cross the cathode in one step from a
conducting plate (`limitPortVoltages` in `Pentode.h`).

**Rectifiers are pure 3/2-power, which is too steep at low current.** Real
rectifiers conduct more than Child-Langmuir predicts near the origin, where
initial electron velocity matters and space charge doesn't yet dominate. Fitted
to the JJ 5Y3S forward curve the model reads 25 mA at 20 V where the sheet shows
45. It converges by mid-range, which is where a supply actually sits.

**No warm-up.** A valve rectifier's slow turn-on — the reason an amp with one
comes up gently instead of slamming the rail to full voltage — is a heater
thermal effect, and nothing here has a heater. The supply is at full voltage
from the first sample. Same for cathode warm-up on the amplifying valves.

**Everything is at one temperature and one age.** No heater dynamics, no cathode
depletion, no drift. A valve that has been running for an hour behaves
differently from a cold one, and a worn one differently again; none of that
exists here.

**The suppressor grid is tied to the cathode** — see `Pentode.h`. Koren's model
has no term for g3, so it isn't offered as a terminal.

### Interelectrode capacitance is modelled — with caveats

`Triode` and `Pentode` carry their datasheet capacitances and `addTriode()` /
`addPentode()` wire them, so the Miller effect is present without a netlist
having to add anything.

**It is the most expensive single thing here.** No new *nodes* appear — the
capacitors connect terminals the valve already has — but in the DK method every
capacitor is a state variable, so three per valve is a much bigger state vector.
Measured: **+7%** on a three-stage 12AX7 preamp, **+23%** on a six-triode
channel. An earlier version of this file said 3%, from a circuit too
small for the state vector to matter; treat that as withdrawn.

That cost is why it is the first thing `BuildOptions` lets you switch off (the
Settings menu in the editor). Off is a real inaccuracy, not a cheaper way to get
the same answer — the top end stops rolling off — so it is opt-in and travels
with the saved sheet.

How much it matters depends almost entirely on **what drives the grid**, since
the capacitance only acts against a source impedance. Measured here on a 12AX7
stage, loss at 10 kHz relative to 1 kHz: **−0.7 dB** through a 68k grid stopper,
**−7.2 dB** from a 470k source. That is Aiken's point — the driving impedance
decides whether this is a detail or the dominant tone control — and it is a good
reason to model the source impedance properly rather than drive from the ideal
voltage source this engine defaults to (see below).

**Two corrections worth recording, because this file got it wrong twice.** It
first said the absence of Miller capacitance was "part of why a real preamp gets
darker as it gets louder"; the mechanism runs the other way, since Miller
capacitance scales with gain and compression lowers gain. It then said an
overdriven stage gets audibly brighter — measured with a peak-to-peak gain
figure on a *clipping* stage, which is not a frequency response at all: clipped
output is pinned near the rails at any frequency, so the rolloff collapses to
nothing as an artefact. Re-measured with a small probe tone riding on the drive,
the real drive-dependence is **−7.2 dB to −5.7 dB at 470k, and nothing at all at
68k**. The effect is real, much smaller than the formula implies, and only
visible where the rolloff was large to begin with — the loading is time-varying
across the cycle rather than following the large-signal gain.

The remaining caveat: these are lumped constants from datasheets, measured cold
and unbiased. The gain-dependence above is emergent and real; a further
dependence of the capacitance itself on operating point is not modelled.

### The source impedance is zero and the load impedance is infinite

`setInputNode()` makes that node an ideal voltage source, and `setOutputNode()`
reads its node like a perfect voltmeter.

A guitar pickup is nothing like an ideal source — it is a few henries of
inductance and several kilohms of resistance, and what a pedal's input
impedance does to it is a real and well-known part of the sound (this is what
"loading" means, and why a buffer changes a guitar's tone before it changes
anything else). Likewise the following stage's input impedance loads this
one's output.

Both are modellable today by building the source network into the netlist
explicitly, but nothing does it automatically, and the convenience API
encourages forgetting.

### No component tolerances

Every part is exactly its nominal value. Real circuits are built from 5% and
10% parts, and the unit-to-unit variation that produces is a large part of why
individual pedals and amps of the same model sound different.

### No noise

No Johnson noise, no shot noise, no 1/f. The simulation is perfectly silent
between notes, which no analogue circuit is.

### Op-amp: slew rate, phase margin, swing and bias current are modelled; the input range and current limit are not

`OpAmp` models finite gain, rails it clips against, two poles, input bias
current, and — for any
model with a `slewRate` — a long-tailed input pair whose tail current caps how
fast the compensation capacitor charges (`DifferentialPair.h`). That is the
Boyle macromodel's input stage, and the slew rate, the gain-bandwidth and the
input voltage at which the stage runs out of current all follow from the
datasheet's two figures. A pair with emitter resistors, as the 741's has, takes
a `degeneration` that keeps it linear further out and runs it out of current
more abruptly.

The second pole is placed from each part's phase margin, and the dominant pole
is raised so the gain still crosses unity at the datasheet's unity-gain
bandwidth. That distinction was the larger error: a sheet's "unity-gain
bandwidth" is where the gain *crosses*, which with a second pole sits below the
gain-bandwidth the audio band sees (3.0 MHz against 3.3 on a TL072). The 741 was
worse — its bandwidth had been read off the sheet's rise time as 1.2 MHz, which
is a follower's closed-loop bandwidth; the open loop crosses at 0.885.

Checked against TI's own TL072 and µA741 Boyle cards in ngspice-47, on a gain
of 101 at ±15 V with 0.1 V in (10 V out, which the 741 must slew to above about
8 kHz). Error in the whole waveform, rms:

               1 kHz   5 kHz   10 kHz   20 kHz
    TL072      0.1%    0.4%    1.0%     3.2%     (was 0.4 / 2.1 / 4.2 / 8.9)
    LM741      0.1%    0.4%    1.2%     4.0%     (was 1.7 / 6.6 / 5.9 / 1.7)

The 741's old 1.7% at 20 kHz was two errors cancelling — at small signal, where
nothing slews, the same model was 8–11% too loud at 10–20 kHz. Part of what is
left at 20 kHz is the trapezoidal rule at 192 kHz, under which a 20 kHz tone
answers as 20.75 kHz would in the analogue circuit — about 1.2 points of the
TL072's 1.6% amplitude error there. On a Rat — an LM308 at gain 1000 — slewing
changes a hard-driven output by a fifth.

Where each margin comes from: the TL072's and the 741's from TI's cards,
measured open-loop in ngspice (64 and 63 degrees), and each consistent with its
sheet's quoted follower overshoot (62 and 65); the JRC4558's and NE5532's from
their sheets' overshoot alone, with the test load counted (60 and 73, the
NE5532's with the 50 Ω output resistance of TI's NE5534 card); the LM308's read off a scanned
phase plot, about 65 and coarse. Only the 741's degeneration (0.35) has a source
— the card's emitter resistors — so the other parts keep the bare pair, which
matters only to a part that slews.

The pair costs two Newton ports, so it is only fitted where it can act: a
sampled output moves at most its whole swing in a sample, and a part whose slew
rate covers that never slews (`OpAmpModel::slewsAt`). On 9 V at 48 kHz that
leaves only the LM308; the LM741 and the JRC4558 join at 4× oversampling; the
TL072 and NE5532 never do. Where it is fitted it is not cheap: the Rat's
gain stage, hard driven, goes from 0.24 ms per 512 samples to 0.40 ms — 1.7×,
or about 4% of a core at 48 kHz for that one stage. (An earlier run, before the
second pole was added, read 0.44–0.66 ms with a wide spread; the cause of that
spread was not isolated.) The second pole is a node, not a port, and costs
nothing measurable: the two-op-amp pedal is 0.66 ms per block with or without
it. Newton is not what
grows (4.3–4.7 iterations a sample against 3.8–4.1, the worst case unchanged);
each iteration solves four ports instead of two.

**On 9 V.** Every sheet here characterises at ±15 V, and pedals run on a 9 V
battery. Three things decide what a part does there, and each is now the
sheet's:

- **Where it clips.** The clamp used to be a 1N4148 on the gain node, whose
  current is the input stage's — gm times the overdrive, hundreds of amps per
  volt in a node whose impedance is not physical. Across the five decades from
  just clipping to hard driven its drop moved 0.45 V, so the clip level drifted
  with the drive: a TL072 on 9 V clipped 0.4 V wider at 1.5 V in than at 0.3,
  where TI's card moves 70 mV. The clamp's saturation current is now scaled by
  gm, so it conducts by the overdrive in volts, and its knee is a quarter of a
  junction's: across a millivolt to a volt of overdrive it moves 45 mV. Each
  part now clips where its sheet says within ±30 mV at any drive (a test holds
  all five).
- **The swing itself.** The sheets quote it into a load — 10k for most, 2k for
  the NE5532 — so the clamp sits further out by the output resistance's share
  of that load. Against supply, from the sheets that plot it:
  - **TL072:** TI's Figure 10 is a line, 1.11 V of headroom on ±4.5 V against
    1.40 on ±15, so on a pedal it swings 0.4 V wider each side than a constant
    1.5 V let it. Diodes Inc.'s second-source sheet agrees at ±5 (3.9 V).
  - **JRC4558:** NJR's lines have slope one, so the headroom does not move.
  - **NE5532:** Philips' 1997 sheet gives the same 2 V into 600 Ω at ±15 and
    ±18; TI's revision H says the same. Its ±13.5 V into 2k is the figure used.
  - **LM741:** Motorola's MC4741C (a quad MC1741, built to match it) plots each
    side separately at ±6, ±9, ±12 and ±15 V, and the two are not alike. The
    positive side stops 0.69–0.99 V short and the negative 1.44–1.85, both
    growing a little as the supply falls. On average that is the tables' ±14 V.
    What the tables hide is that a 741 driven into both rails clips lopsided,
    0.9 V further in on the negative side on 9 V. The model now does.
  - **LM308:** no supply data; Analog Devices' LM308A sheet may have it but
    could not be fetched from here. It keeps National's ±15 V figure.
- **The shape.** On a gain of 11 at 9 V, driven 0.6 V into both rails and with
  each model given its card's clip level (the cards' levels are their own clamp
  settings — the µA741 card stops 0.39 V short where the sheet says 1 V), the
  whole cycle against TI's cards in ngspice-47: TL072 1.6% rms of the swing,
  LM741 0.3%, NE5532 (TI's NE5534 card with its 22 pF) 1.3%. That covers the
  knee, the flat and the recovery off the rail; what is left on the TL072 is its
  card's slightly rounder knee.

**Gain against supply.** Only the 741's sheets plot it. TI's µA741 Figure 5-6
and Motorola's MC4741C Figure 8 agree that the open-loop gain on ±4.5 V is
about a quarter of its ±15 V value (53 V/mV against 200), and the model follows
TI's. The gain-bandwidth is held, so only the dominant pole moves: from 5 Hz to
about 19 Hz, and what changes is the loop gain below that.

**What no sheet gives** is gain-bandwidth or slew rate against supply: searched
across TI, National, Fairchild, Motorola/onsemi, Philips, NJR and Diodes Inc.
sheets for all five parts. The one indirect reading is TI's TL07x power
bandwidth at ±5, ±10 and ±15 V (Figures 5–6 of SLOS080J). From the knees, the
slew rate is 13–14 V/µs at all three supplies, flat within what the plot
resolves, and the TL072 never slews at audio rates anyway. So the bandwidth and
slew rate keep their ±15 V figures on 9 V. Bipolar parts' bandwidth does fall
somewhat at low supply as their collector-base capacitances grow. No sheet here
puts a number on it, so it is not modelled.

**Input bias current** is two constant currents at the inputs, signed by the
input pair: in for NPN (741 80 nA, NE5532 200 nA, LM308 1.5 nA) and for the
TL072's P-channel JFET gate leakage (65 pA), out for the 4558's PNP pair
(25 nA). It is what shifts a bipolar pedal's bias point: 80 mV through a 741's
1M bias resistor. Typical figures; real parts spread several times either way,
and the offset *current* — the two inputs disagreeing — is not modelled. Each
pair of sources reads the existing clamp rails rather than a rail of its own,
which cost 7% of the two-op-amp pedal's CPU; as built it costs nothing
measurable.

**The JRC4558 now follows its maker.** It used to follow ArtChip's clone sheet
(2.8 MHz, 2.2 V/µs, 2 MΩ); NJR's own NJM4558 sheet, now in `docs/specs`, says
3 MHz, 1 V/µs, 5 MΩ and 100 dB, and shows a PNP input pair. The slew rate is
the audible change: half what the clone claimed. NJR's SPICE model is encrypted
and behind a registration, so the 4558 is the one part here with no vendor card
to check against. Its phase margin (60°) still rests on ArtChip's quoted
overshoot, which ArtChip's own phase plot (nearer 80°) contradicts.

**The NE5532 has no card of its own.** TI's NE5534 card, compensated with the
22 pF its sheet gives for unity gain, is the nearest thing. Against it, on the
gain of 101: within 0.7% in amplitude and 1.9° in phase up to 20 kHz. But it
crosses unity at 8.5 MHz with 59° of margin, and gives 17% follower overshoot
where revision H says 10, so it is not adopted as the reference for the open
loop. **The LM308 has no card at all** — TI publishes none for it — so it
remains checked against its sheet only.

What is still absent, and why:

- **The input common-mode range.** The sheets give the range: ±13 V of ±15 on
  a 741 or 4558, ±14 on an LM308, and −12 to +15 on a TL072. TI's legacy TL07x
  Figure 5-57 locates it exactly: the offset is flat from 3.4 V above the
  negative rail to 0.7 V above the positive one, then leaves the plot. The
  NE5532's (onsemi and Philips, Figure 7) tracks the supply to within about a
  volt. None of them says what the part does *past* the edge, which is where
  the sound would be. A JFET-input TL07x is known to phase-reverse below its
  negative limit and a bipolar part does something else, but no sheet
  quantifies either, so any model of it would be invented. On 9 V a TL072
  biased at 4.5 V has 1.4 V of input swing before it gets there; an LM741
  about 1.5 V.
- **The output current limit.** 25 mA on a 741, 38 on an NE5532, about 6 on an
  LM308 (TI's output-swing curve). In the classic pedals it never engages — the
  Rat's 308 drives its clipping diodes through 1k, a few milliamps at most — so
  it would cost a Newton port per op-amp for nothing audible.
- **Poles beyond the second**, and the zeros a real compensation network has.
  They are what is left between the models and the cards above 20 kHz.
- Input offset voltage and current, CMRR, PSRR, noise.

The NE5532 model follows TI's revision K tables (12 MHz, 5 V/µs); the sheet in
`docs/specs` is the older revision H (10 MHz, 9 V/µs). At audio rates it slews
at neither.

The **LM386** (`PowerAmp.h`) is built the same way and shares the gaps, plus
its own: no crossover distortion from its class-AB output stage, no current
limit (it swings 8 Ω the way the datasheet says, but a 4 Ω load swings wider
than the datasheet's 3.5 V), and no bypass pin, since the supply here is ideal
and has no ripple to reject. Its frequency response is fitted to the datasheet's
curves at 6 V and follows the supply from there by the physics of its bias
network; it is −3 dB at 260 kHz at gain 20 where the datasheet says 300, which
is all ultrasonic.

### Transformer: no core saturation, and it passes DC

The engine's `Transformer` is ideal. Magnetising inductance, leakage inductance
and winding resistance all compose from ordinary components around it — see the
header — but **saturation does not**, because it needs a nonlinear inductor and
the engine has no such device.

A drawn transformer can be **Ideal** or **Real**. Real wires those three strays
itself, sized from the turns ratio against an assumed 8 Ω secondary: about
−1 dB at 30 Hz and 9 kHz for a typical output transformer, and no DC through it.
That assumption is the thing to know — the numbers land inside the range real
guitar output transformers measure (15–30 H, 20–100 mH, 60–120 Ω), but they are
a *shape*, not a particular transformer, and nothing here is fitted to a
datasheet the way the valve models are. A transformer doing anything other than
feeding a speaker — an interstage or a mains transformer — is being sized by an
assumption that does not apply to it, and wants Ideal plus components you choose.

Ideal still passes DC, which a real one cannot. Put an inductor across the
primary and the DC goes away with it. The builder warns when an Ideal
transformer has nothing across its primary, since that is easy to leave out and
confusing when you do — the bias point comes straight through and nothing looks
obviously wrong. Real is exempt from the warning, having its own.

Saturation is missing from **both**, and that matters exactly where a guitar amp
lives: at high level and low frequency, a real output transformer's core gives up
and the bass goes soft and mushy. Real gets you the bandwidth and the DC block;
it stays perfectly linear however hard you hit it.

## 3. Missing physics that matters less here

- **BJT (Ebers-Moll):** the Early effect (VAF) and junction capacitances
  (CJE/CJC) are modelled for the parts whose cards publish them — the silicon
  small-signal pair exactly, datasheet Cobo for the 2N2222A/2N5133/BC109C,
  the NTE102/103 and the 2SC2240 —
  and both are **on by default**, like everything else here — turn them off
  under Settings → Performance if you need the CPU back. VAF
  reads as an output resistance ro = VAF/Ic, a few percent of a stage's gain;
  the capacitances bite through Miller, and measured here on a gain-of-110
  stage driven through 100k they put the pole at 7.5 kHz where the estimate
  said 8.7. The capacitances are fixed values — a real junction's falls off
  with reverse bias (VJE/MJE/VJC/MJC stay out) — and with no transit time
  (TF/TR) fT still doesn't degrade with current. The forward high-injection
  knee (IKF) is modelled for the cards that carry one — the BC107-109 family,
  the 2N5088/2N5089, the MPSA18 and MPSA13, the NTE102/103 and the
  2SC2240 — but not for the older models
  whose cards were entered without it (the 2N2222A's knee is 19.5 mA, so its
  gain keeps climbing past it). One knee cannot draw every roll-off: the
  2SC2240's gain is flat to 30 mA and a quarter of that by 200 mA, and the
  fit, made for the audio end, is 15% high at its 100 mA rating. The reverse
  knee (IKR), terminal resistances and the reverse Early effect are out
  everywhere, and so is base-width modulation of the base current, which is
  what a datasheet's hre measures — 8e-4 on the NTE102/103, 4e-5 here.
- **Darlingtons** (the MPSA13) are two transistors on an internal node, so the
  pair's Vbe and gain come out right, but a real Darlington's gain below a few
  milliamps is extrapolated: onsemi's curves start at 5 mA.
- **JFET (Shichman-Hodges):** simple square law, no capacitances.
- **MOSFET** (`Mosfet.h`): a square law smoothed through threshold into an
  exponential below it, so the moderate inversion a pedal biases one in is
  continuous, with a source resistance solved inside the device that bends it
  straight as the current rises — 45% more CPU on a clipping one-MOSFET stage,
  about 2.5% of a core per channel at 48 kHz, after warm-starting the solve
  (cold, it was 3.1 times). Both cards are fitted to Vishay's sheets, and the
  2N7000's covers the BS170, which Vishay make from the same die. The
  subthreshold slope is fitted only for the 2N7000 (n = 2.0, from Vishay's two
  threshold points); the BS250's sheet resolves nothing below about 30 mA, so
  its pedal region is that n extrapolated, landing 1 mA at 3.2 V inside the
  sheet's 1–3.5 V window. One resistance cannot bend the BS250's family
  everywhere: the middle runs 7–20% under the curves. No drain resistance and
  no mobility degradation as such, and the gate capacitances are constants
  read at a pedal's bias rather than the steeply voltage-dependent things they
  are — the gate-drain one doubles between 5 V and 1 V. And the part number is
  not the die: onsemi's 2N7000 carries 1.6 times Vishay's current above 3 V,
  onsemi's BS170 is a different, weaker die, and General Semiconductor's BS250
  has three times Vishay's input capacitance. The threshold spread on a real
  part (0.8 to 3 V; 1 to 3.5 V on the BS250) dwarfs every one of these; the
  models are Vishay's typical part.
- **Diodes:** the 1N4001, the BAT41 and the BAT46 carry junction capacitance,
  and the 1N4001 its stored charge (transit time, 5.7 µs), integrated
  backward-Euler inside the device; the two BATs their series resistance and
  guard ring. The guard ring has no resistance of its own, which is what leaves
  the BAT46 16–24 mV off ST's curve above 50 mA — past anything a clipper
  draws. The older models carry none of these. The capacitance is a constant — a real one rises
  into forward bias — and the stored charge relaxes within one sample at any
  audio rate, so what it models is the charge a clipping edge has to move, not
  the shape of the recovery, which is over in microseconds.
- **Capacitors:** ESR is modelled. Dielectric absorption, ESL, and the large
  voltage-dependence of Class 2 ceramics are not — a ceramic can lose half its
  capacitance under DC bias, which is a real effect in a real pedal.
- **Everything is at a fixed 300 K.** `thermalVoltage = 0.025852` throughout,
  no self-heating and no drift. Germanium parts in particular are noticeably
  temperature-dependent in reality: the NTE102/103 sheet puts ICBO at 0.8 µA
  at 25 °C and 20 µA at 80 °C, and that leakage is what a germanium fuzz's
  bias rests on.
- **Valves:** no warm-up, no heater dynamics, no cathode depletion. Grid
  conduction uses a plausible perveance rather than a fitted one (published
  grid-current data barely exists), and the pentode's screen current is
  approximate.
- **Potentiometers:** two ideal resistors. No wiper resistance, no taper
  tolerance, no scratch.
- **Switches:** a resistor that changes value, not a true open circuit. The
  node stays in the matrix either way.
- **A reverse-biased electrolytic is reported, not simulated** — see
  `Capacitor.h`. `getReversedCapacitorCount()` flags it as a netlist error.

## 4. Numerical

### Trapezoidal integration, fixed step

Reactive components use the trapezoidal rule at the sample period. Two
consequences worth knowing:

- It is A-stable but not L-stable, so it does not damp components near Nyquist.
  A sharp step can ring. SPICE offers Gear/BDF2 for exactly this reason; this
  engine has one integrator and no choice.
- Pole frequencies are warped near Nyquist. Immaterial in the audio band, real
  above it.

### A non-converged sample is used anyway

Newton stops at `maxNewtonIterations = 64`. If it hits that, the loop exits,
`nonConvergenceCount` increments, and *the last iterate is used as the answer*.
`process()` guards against non-finite output by dropping state, but a value
that is merely wrong and finite passes through silently.

Check `getNonConvergenceCount()` after a session if a circuit sounds off. It
should be zero.

### Convergence tolerance is a sound/CPU trade, not a correctness threshold

Defaults are `1e-5` absolute and `1e-4` relative, chosen by measurement. Newton
converges quadratically, so the last step badly overstates remaining error —
`1e-4` leaves the three-stage preamp about 140 dB below peak. On a circuit with a
lot of gain *after* the nonlinearity, that margin shrinks; see
`setConvergenceTolerance()`.

### Dense LU

The solver is dense, O(n³) to factorise and O(n²) per solve. That is the right
choice at the sizes here (n = 5 to 28) and the wrong one later: a complete amp
— preamp, tone stack, phase inverter, power stage, transformer — lands around
n = 60–80, where a sparse solver would start to win clearly.

## 5. Structural

### Only ground and the input node can have a known voltage

Every other node is an unknown, and every voltage source adds a constraint row.
So a node pinned to a fixed DC voltage still costs both an unknown *and* a row,
even though its value is a constant.

This is the main remaining inefficiency. `addOpAmp()` needs three fixed rails,
which is why it was so expensive; sharing rails between op-amps on the same
supply cut the op-amp overdrive from n = 28 to n = 22. A general "fixed node"
concept — treating a constant-source node as known, the way the input node
already is — would take it to roughly n = 16, and would speed up every
op-amp-heavy circuit by a further third or so on the linear-algebra side.

The reason it was not done: it changes which quantities are observable, since
an eliminated source no longer has a solved branch current for
`getSourceCurrent()` to return.

### Stereo duplicates work that is identical

`PluginProcessor` builds one `Circuit` per channel. Their reactive state
differs, correctly — but their matrices, factorisations and DK precomputations
are bit-identical, and both are rebuilt from scratch on every knob move. The
per-sample cost is genuinely 2×; the per-knob-move cost need not be.

### The operating point is solved once, at `prepare()`

With a source-stepping fallback if the direct solve fails. If it still fails,
`foundOperatingPoint()` returns false and playback starts from a bad bias
point. Supply sag over time is modelled only insofar as the netlist contains
the components that cause it.

---

## What is not on this list, deliberately

So the list above reads as calibrated rather than exhaustive-by-anxiety:

- `process()` performs **zero allocations** — enforced by a counting
  `operator new` in the test suite, not by inspection.
- The nonlinear solve uses SPICE's own robustness machinery: `gmin`, and
  `pnjlim`-style voltage limiting on every junction.
- The bias point is solved properly, with source stepping, rather than assumed.
- DK versus full Newton is chosen per circuit at `prepare()` time, because
  neither wins universally — DK loses when ports outnumber nodes.
- Component models use published parameters where they exist, and say so at the
  point of definition where they do not (`jrc4558()` and `npnBC109C()` both flag
  their unsourced figures explicitly).
