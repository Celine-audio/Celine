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
2x or 4x.

Measured on the diode clipper driven hard, the ratio of fold-down to genuine
harmonic content is **-23.7 dB at 1x, -43.3 dB at 2x and -53.5 dB at 4x**, for
+65% and +192% CPU respectively. So the first doubling buys about 20 dB for
two-thirds more CPU, which is the best trade on the list; the second buys
another 10 dB for a lot more.

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

Note also that the measurements in `benchmarks/` are all at 1×, so budget
accordingly: the op-amp overdrive at ~6% of a core per channel is roughly 10% at
2× and 18% at 4×.

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

### Op-amp: no slew rate

`OpAmp` models finite gain, one dominant pole, and rails it clips against. It
does not model slew limiting. On the slow parts that matters: a JRC4558 slews
at 2.2 V/µs and an LM308 (the ProCo Rat) far slower, and on a fast transient
that rounds the waveform in a way the frequency response alone does not
capture. `OpAmpModel::jrc4558()` documents this at its definition.

Also absent: input bias and offset current, CMRR, PSRR, and the higher-order
poles a real part has.

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
  small-signal pair exactly, datasheet Cobo for the 2N2222A/2N5133/BC109C —
  and both are **on by default**, like everything else here — turn them off
  under Settings → Performance if you need the CPU back. VAF
  reads as an output resistance ro = VAF/Ic, a few percent of a stage's gain;
  the capacitances bite through Miller, and measured here on a gain-of-110
  stage driven through 100k they put the pole at 7.5 kHz where the estimate
  said 8.7. The capacitances are fixed values — a real junction's falls off
  with reverse bias (VJE/MJE/VJC/MJC stay out) — and with no transit time
  (TF/TR) fT still doesn't degrade with current. High-level injection
  (IKF/IKR) is still out too, so gain keeps climbing past the knee a real part
  has (on the 2N2222A card, 19.5 mA); terminal resistances and the reverse
  Early effect as well.
- **JFET (Shichman-Hodges):** simple square law, no capacitances.
- **Diodes:** no junction capacitance, no reverse recovery. Negligible for
  audio-rate clipping; slightly wrong for a rectifier.
- **Capacitors:** ESR is modelled. Dielectric absorption, ESL, and the large
  voltage-dependence of Class 2 ceramics are not — a ceramic can lose half its
  capacitance under DC bias, which is a real effect in a real pedal.
- **Everything is at a fixed 300 K.** `thermalVoltage = 0.025852` throughout,
  no self-heating and no drift. Germanium parts in particular are noticeably
  temperature-dependent in reality.
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
  point of definition where they do not (`ne5532()` and `npnBC109C()` both flag
  their guessed figures explicitly).
