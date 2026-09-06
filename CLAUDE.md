# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About This Project

Céline is a JUCE audio plugin: a circuit sandbox, where the circuit you draw on the sheet *is* the signal path. C++23, CMake, cross-platform (macOS, Windows, Linux), built as VST3, AU, LV2, CLAP and Standalone.

The build system and CI came from the [Pamplejuce](https://github.com/sudara/pamplejuce) template; everything the plugin actually does lives in `source/`.

## Build Commands

```bash
# Configure (run once, or after CMakeLists.txt changes)
cmake -B Builds -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build Builds --config Debug

# Run tests (from project root)
ctest --test-dir Builds --verbose --output-on-failure

# Or run tests directly
./Builds/Tests

# Run a single test by name
./Builds/Tests "[test name]"

# Run benchmarks
./Builds/Benchmarks
```

For faster builds, add Ninja: `cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug`

On macOS for universal binary: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`

## Project Structure

- `source/` - Plugin source code (PluginProcessor, PluginEditor)
- `source/Schematic/` - the drawing, and turning it into a circuit. Knows nothing about audio.
  - `Element.h` - what a placed part is. **One table** (`getElementInfo`) holds every type's pin geometry, value semantics and model list; the net extractor, the builder and the renderer all read it, so a part is described in exactly one place. Adding a part type is a table row plus a case in `SchematicBuilder` and a symbol in `SchematicSymbols`.

    **Every numeric default in the table is zero**, not a plausible-looking 10k or 100n. A default that already looks like an answer is one you forget to change, and the drawing has no way to show that you *meant* 10k rather than never having said. Zero draws red and `validate()` refuses to build, naming the parts — which it has to, since a zero-ohm resistor is a short and would hand the solver a singular matrix anyway. Saved sheets are unaffected: they carry real values, and only newly placed parts start empty.

    `ElementType::Text` and `ElementType::Rectangle` are the odd ones out: no pins, so net extraction never sees them and the builder skips both — they exist so a drawing can say what it is. Text's `label` *is* the text, it stays upright however the sheet is rotated (which is why the inspector hides rotate and flip for it, and for anything else pinless), and its hit box comes from `getTextHalfWidth()` rather than from pins it hasn't got. The renderer and the hit test share that one estimate, so what you click is what you see.

    Rectangle is a group box: a frame drawn *behind* everything, to ring a stage and name it. Its geometry comes from `getRectangleBounds()`, which the renderer, the hit test and `Schematic::getElementBounds()` all call for the same reason Text has one estimate. Two things about it are deliberate. Its size lives in `Element::width`/`height` rather than in `value` — putting it in `value` would put a decorative number in front of the build's zero-value check, and a box must never be able to stop a build. And they are the one pair of numbers here that *doesn't* default to zero: the zero defaults exist so an unfilled part is loud and refuses to build, whereas a zero-sized box would simply be invisible — nothing to see, nothing to fix, no build to stop. Its `models` are colours, because which section you are looking at is the whole of what a group box has to say; `SchematicSymbols`' `rectangleColour()` asks `Theme::groupBox()` for the colour at that position, so a colour added there needs a role added in `source/PluginThemeRoles.h`.

    Models are `id|Name|What it is|Group` records separated by semicolons, the group optional — the short name goes under the part and into the dropdown, the sentence shows in the inspector, which is the only place with room to say a BC109C is the low-noise one.

    **The id is the file format, and it is the only field that is.** `Element::modelIndex` is still an index in memory, because that is what the renderer and the builder switch want, but it is resolved from the id on load and written back as the id on save. So this table can be reordered, sorted, added to or pruned freely — the constraint that used to force every new model to the end of its list is gone. Ids must be unique per element type, lower case and free of `|`, `;` and spaces; nothing at load can catch a duplicate, so `tests/PluginBasics.cpp` does.

    Every id here begins **`celine:`**, a reserved namespace for the models the plugin ships — which are immutable, authored by Céline (`getModelAuthor()`), and the only models that need no author stored per row, since they all share one. A user library will mint ids as `author:name:hash`, frozen at creation so renaming is a display change rather than a broken sheet, and the prefix is what stops a user model shadowing one of ours or being shadowed by one added later.

    An id a sheet names and the table no longer has falls back to model 0 **and says so in the console**, naming the id and its author so you know who to ask for the file. A sheet with no ids at all — anything written before this change — gets one message saying how many parts fell back. There is no compatibility path and no index in the file: a silent fallback is the failure the scheme exists to remove, since such a sheet still builds and still makes sound with the wrong parts in it.

    The unresolved id is **kept**, in `Element::unresolvedModelId`, and written back out unchanged instead of the fallback's. That matters because the save is often nobody's decision — a host writes session state on its own, so a sheet inside a DAW project would lose the reference just by being opened and closed on a machine without the model, and it would then be unrecoverable even once you obtained it. The part draws its missing model in the unset colour next to a zero value, and the inspector shows it greyed and marked `(missing)`; choosing any model clears it.

    The `ElementType` enum *is* still positional and still written to saved sheets, so that one is still **append, never reorder**.

    That list and the `switch` in `SchematicBuilder` are two halves of one thing and must be extended together. The switch ends in `default:`, so a mismatch doesn't fail — an index the builder doesn't know silently behaves as model 0. That is how the three measured 12AX7s and the KT66/KT77 shipped implemented but unreachable, and why "Every valve model in the palette reaches the engine" in `tests/PluginBasics.cpp` builds a stage per index and insists the answers differ.

    BJTs and JFETs are separate element types rather than models of one "transistor": the pins mean different things, the symbols differ, and they reach the engine through different device models. Polarity for the arrow direction comes from `isReversePolarity()`, which reads the *description* ("PNP silicon…", "P-channel…") — never the part number, since renaming the models once made every transistor silently draw as an NPN.

    A pentode has four pins, not five: the suppressor grid is drawn strapped to the cathode inside the envelope, because the Koren model has no term for it and a terminal that cannot change anything is worse than none.
  - `Schematic.h/.cpp` - elements + wires, net extraction, save/load. Net extraction is union-find over connection points, rebuilt from scratch each time: coincident pins connect with no wire, a wire ending partway along another joins it, and terminals *name* their net — which is why two unwired Ground symbols become one node.
  - `SchematicBuilder.h/.cpp` - the one place geometry becomes a netlist. Everything it has to say comes back as `Diagnostic` records rather than joined-up prose: severity, sentence, and **which part, at which grid position**. Six problems in one sentence is a paragraph nobody reads to the end of; six rows each naming one part is a list you work through, and the console can select the part when you click it.

  Two failure modes here exist because they were once silent. A **failed factorisation** means `process()` returns 0 for every sample, and the DC system is a *different matrix* — so the bias point can settle on perfectly sensible voltages while the per-sample one is singular. The build now checks `hasUsableFactorisation()` (guarded on `getSystemSize() > 0`, since a circuit whose every node is ground or input has nothing to factorise and is obvious on the sheet). And an **output terminal wired to ground** is caught by name: the net takes ground's name, so `setOutputNode("out")` invents a node with nothing on it, whose all-zero row is singular — a confusing way to learn you shorted your output. Transformers are the one part it *expands* rather than translates: the **Real** model wraps the ideal device in copper resistance, leakage and magnetising inductance, on internal nodes named after the element id. All four numbers come from `straysFor()`, which derives them from the turns ratio against an assumed 8 Ω secondary — one stated assumption instead of four free parameters, which is also why it is documented as a shape rather than a particular transformer. Returns one `Circuit` per channel plus the `LiveControl`s. Validation failures are sentences ("No ground symbol"), not singular matrices.

  `BuildOptions` is where accuracy gets traded for CPU. Everything in it defaults to the accurate answer, and each option is build-time rather than live because each changes what the matrix contains. The first one, `interelectrodeCapacitance`, is off-switchable because it is the most expensive thing in the simulation a player might reasonably decline: three capacitors per valve, every capacitor a DK state variable, measured at **+23% CPU on a six-triode preamp**. Turning it off is implemented by *zeroing the model's cap fields* rather than by a flag in the engine — `addInterelectrodeCapacitance()` already reads a zero as "don't model it", so there is no second switch to keep in step. Options ride in `createDocument()`, unlike the window size, because they change what the sheet sounds like; a file that predates an option simply lacks the property and gets the accurate default.
  - `ExampleSchematics.h/.cpp` - starter circuits, laid out by hand on the grid.
- `assets/icons/elements/` - **the part symbols, as SVG.** Replacing a resistor is replacing a file, not editing C++. `source/ui/SymbolArtwork.h` states the convention artwork must follow and why each rule exists: viewBox `-60 -60 120 120` with **(0,0) the part's centre**, **10 SVG units to a grid square**, and stroke-width `1.2` as the normal line weight (heavier lines are multiples of it). The scale rules are not cosmetic — the canvas draws pins on top of the artwork at the positions in `Element.h`, so a file drawn to any other convention lands its terminals somewhere the wires cannot reach.

  Geometry is parsed once and cached **in grid space**, then stroked by the canvas at `max(1, gridSize * 0.12) * scale` pixels. That floor is the whole reason the artwork is not simply handed to `Drawable::draw`: letting the view transform scale the strokes makes a sheet zoomed out to see all of it fade to nothing. It also means painting allocates nothing — the paths are cached and only an `AffineTransform` changes per part.

  A part whose state changes its symbol gets **one file per state** rather than a flag: `switch-open`/`switch-closed`, `capacitor`/`capacitor-polarised`, four diodes, both transistor polarities. So "redraw the open switch" is one drawing. Assets are looked up by **original filename**, never by the C++ identifier JUCE derives from it — that derivation drops hyphens rather than replacing them (`capacitor-polarised.svg` becomes `capacitorpolarised_svg`), and guessing it wrong is silent: the part draws as nothing but its pins. `tests/SymbolArtwork.cpp` walks every name for exactly that reason.

  Three things stay in code because they are *readings* of a part's state rather than pictures of it, and no file could hold them: a potentiometer's wiper (it slides with the knob), the IN/OUT lettering (drawn with a font so it stays upright), and Text and Rectangle, which are a note and a frame you drag the corner of.

- `source/ui/` - `SchematicSymbols` (drawing), `SchematicCanvas` (the sheet you draw on), `EditorPanels` (palette, inspector, and the message console). The canvas has **rulers** along its top and left, labelled every 1/2/5/10/… grid squares depending on zoom so the numbers never collide, with a cursor marker on both that follows the pointer — build messages name grid coordinates, so without a ruler you are counting squares to find the part one points at. Each ruler is its own **opaque child component**, and both halves of that matter: the marker has to move whenever the mouse does, and a marker drawn inside `SchematicCanvas::paint()` costs a full canvas repaint every time — measured at **0.44 ms on the default sheet and 2.98 ms on a busy one**, against **0.08–0.11 ms for the two strips alone**, which is 5–28× cheaper and flat in the size of the drawing. *Opaque* is what keeps it that way: a transparent child makes JUCE repaint the parent behind it, putting the whole cost straight back. They take no mouse input, so the sheet still receives everything at its edges. The right-hand panel is the inspector over the **`MessageConsole`**, which shows one row per message coloured by severity and selects the part when a row is clicked. A part's caption is drawn as **coloured runs rather than one string**, because what a part is called and what it is set to are different kinds of fact: names (the label, and the model) are **yellow**, a value someone entered is **green**, and a value still at zero is **red**. That last one pairs with the table's zero defaults — see `Element.h` — so an unfilled part is loud on the sheet and refuses to build. The split between the two panels is by *what an action does to the sheet*: the **palette** holds everything you add — the wire first, then the parts — and the **toolbar** holds what you do to what is already there, **Select** (S) and **Delete** (X). Wire (W) is a tool rather than an element type only because of how it is implemented, which is not a distinction worth making anyone learn, so it sits with the parts. Right-drag pans the sheet and middle-click clones whatever is under it — the Place tool is armed with a *copy* of that part, value, model, label and all, so "five more of that one" is one click and five. `Element::copyPropertiesTo()` is the single place that says what "the same part" means, so a member added to `Element` has one place to be remembered rather than being silently dropped. Right-click no longer deletes — and Delete is `X` rather than `D`, because `D` names the diode — the tool letters (`S`, `W`, `X`, plus `F` to frame the whole drawing) are deliberately none of the parts' initials, so the two sets can grow without colliding. `F` is a verb among nouns, which is allowed because it moves the *view* rather than the sheet — Command stays reserved for acting on the parts, and `Cmd-F` still flips. A bare letter names a part and arms it for placing — `R` resistor, `C` capacitor, `L` inductor, `T` transistor, `D` diode, `G` ground, `B` box (the group rectangle; `R` was already the resistor, which is why the letter names the thing rather than spelling it) — so the common parts go down without a trip to the palette. Command turns a letter into an action on what is already there: `Cmd-R` rotates and `Cmd-F` flips (mirrors) the part *being placed* when one is armed, otherwise the selection — and both stick across placements, so a run of identically oriented parts is set up once.

The split is deliberate: an unmodified letter always *names* a thing, a modified one always *does* a thing, so the two sets can grow without colliding. It does mean the part letters and the tool letters share one namespace — `R` is the resistor, not Rotate — which is why rotate and flip moved under Command rather than keeping the bare letters they had.

The window is resizable and every pixel of a resize goes to the canvas: the palette and inspector are lists of fixed-width things and the control strip is a row of knobs, so stretching any of them buys nothing. The size lives on the *processor* (`editorWidth`/`editorHeight`), because the editor is destroyed and rebuilt each time the window closes — and it rides in `getStateInformation` but deliberately **not** in `createDocument`, so loading a `.celsch` never resizes your window.

  A group box breaks the "click a part to select it" rule twice over, and both are load-bearing. It is **grabbed by its edge**, not its interior — `Schematic::hitTest()` — and rectangles are searched **after** everything else in `findElementAt()`, because a box is drawn behind the circuit and so has to lose to it under the cursor too. Get either wrong and ringing a finished stage makes every part and wire inside it unselectable and undeletable, which is the exact opposite of what drawing one is for. Select it and its bottom-right corner becomes a resize handle; the drag pins the opposite corner by moving the centre, which works out exactly because `Rectangle<int>::withCentre` and `getCentreX` are inverses at odd sizes too.

  Mirroring is applied **before** rotation, in `Element::getPinPosition()` and in the renderer's `LocalSpace` alike; if those two orders disagreed the pins would stop landing on the symbol. Flip swaps the part's own left and right rather than the sheet's, which is what makes it mean the same thing at every angle.

  **Keyboard focus matters here and is easy to break:** every palette entry and toolbar button sets `setWantsKeyboardFocus(false)`, and `SchematicCanvas::setTool()` grabs focus. Without both, clicking a palette part moves focus to that button and the canvas shortcuts silently stop working until the next click on the sheet — which, for the part being placed, is one action too late. `R` and `F` are the only way to turn a part while placing, since the inspector's buttons need a selection, so anything that takes focus away breaks the feature outright.

  Keyboard shortcuts match on the text character *or* the key code, because a real letter keypress carries the upper-case code while a `KeyPress` built from a character keeps whatever it was given.

  Note `Schematic::getElementBounds()` computes min/max by hand rather than with `Rectangle::getUnion`: a rectangle built from two identical points is zero-sized, JUCE counts that as empty, and `getUnion` returns the *other* operand when either side is empty — so unioning pin positions silently collapses to one pin. That made every part unclickable except exactly on a pin.
- `source/CelineEngine/` - The circuit simulator:
  - `Components/` - one header per component type, each holding its parameters and its device models. Nothing here knows how to stamp or solve.
    - `Types.h` - `NodeIndex` / `ComponentId` aliases
    - `Ports.h` - the port abstraction every nonlinear device presents to the solver
    - `Junction.h` - p-n junction maths shared by every semiconductor (gmin, exponential I-V, SPICE voltage limiting)
    - `SpaceCharge.h` - the valve equivalent: 3/2-power Child-Langmuir flow, stable softplus, step limiting
    - `Resistor.h`, `Capacitor.h`, `Inductor.h`, `Potentiometer.h/.cpp` - linear parts
    - `VoltageSource.h` - DC supplies and AC windings (needs an MNA branch row, not a conductance)
    - `Diode.h`, `Transistor.h`, `Jfet.h` - semiconductors and their models. A `Diode` with `breakdownVoltage` set is a Zener.
    - `Vccs.h` - voltage-controlled current source (linear); the primitive that makes amplifiers expressible
    - `IdealOpAmp.h` - a nullor: one constraint row, no ports, no saturation. Cheaper and exact where the op-amp's limits don't matter
    - `Transformer.h` - ideal N-winding transformer (one row per winding). Compose with an inductor across the primary for magnetising inductance, series L/R for leakage and losses — which is what the drawn transformer's **Real** model does for you, in `SchematicBuilder`, rather than here
    - `OpAmp.h` - op-amp parameters. **Not a device**: `addOpAmp()` assembles one from a resistor, two `Vccs`, a capacitor and two diodes, so the gain is linear and only the clamp diodes reach the Newton loop. See the header for why a single nonlinear device broke DK.
    - `VacuumDiode.h`, `Triode.h`, `Pentode.h` - valves, Koren models. **Every model is fitted here to a JJ datasheet and reproduces Ia, gm and ra at the quoted operating point** — all four triodes, all five pentodes, all three rectifiers. Koren's own published sets were 20–30% out at that point on the triodes and had plate resistance 2–6× too high on the pentodes, so "it's Koren's set" is not evidence of anything; check before trusting. Each model's header comment records what it was fitted to and how well.

    Triodes carry two formulations, selected by `TriodeModel::formulation`. **Koren** fits published curves and is what the datasheet-derived models use. **Dempwolf-Zolzer** (DAFx-11) is fitted to *measurements* of three individual 12AX7s, and is the only path with a real grid current — Koren has none, so this project bolts a guessed perveance onto it, and that guess draws 60% too much at Vg = +1 V. D-Z costs about 2% more per sample. Both share the same ports, so neither needs solver support; `linearise()` branches once. What fitting a point still doesn't buy you is in **`LIMITATIONS.md`** under "Valves" — read it before trusting a valve result.
    - `FastMath.h` - approximations of `exp`, `log` and `pow`, and the process-wide switch that turns them on. Accurate to ~3e-10 relative, four orders tighter than the Newton tolerance they feed. Whether they are *faster* is a platform question: measured a wash on Apple Silicon, because Apple's libm is already excellent. `sqrt` is deliberately absent — it is one instruction.
    - `Components.h` - umbrella include
  - `Solver.h/.cpp` - dense LU with partial pivoting, allocation-free once sized. Stores the reciprocal of each pivot so back substitution multiplies rather than divides.
  - `Engine.h` - `Circuit`, and the class comment explaining the DK method. Read this first.
  - `Engine.cpp` - netlist construction, component updates, node naming, prepare()/reset()
  - `EngineSolve.cpp` - stamping, DK precomputation, the Newton loops, per-sample process()
  - `Circuits.h/.cpp` - **the engine's test bench, not how the plugin builds circuits.** The plugin builds from the drawing, through `SchematicBuilder`; nothing in `source/` outside this file calls anything here. What keeps it is `tests/CelineEngine.cpp` and `benchmarks/`, which need circuits whose behaviour is known independently of this codebase — a one-pole whose corner is arithmetic, a tone stack with a published curve, an op-amp overdrive transcribed from a SPICE netlist. A drawn schematic can't play that role: it would only confirm that the builder and the engine agree with each other. Anything here with no caller is dead and should go.
  - **`LIMITATIONS.md` - what the simulation does not model, ordered by audible impact. Read before trusting a result or blaming a circuit.**

### Adding a nonlinear device

Nonlinear devices are described entirely by their **ports** (see `Components/Ports.h`) — node pairs whose voltage drives the device and through which its current flows. A device provides four free functions in `CircuitComponents`:

```cpp
constexpr int portCount(const MyDevice&);
void fillPorts(const MyDevice&, Port* out);
bool limitPortVoltages(MyDevice&, double* v);   // damp the Newton step
void linearise(const MyDevice&, const double* v, DeviceLinearisation& out);
```

Then add a `std::vector<MyDevice>` to `Circuit`, an `add*` function, and one line to `forEachNonlinearDevice()` in `EngineSolve.cpp`. That single function defines the port ordering for every pass that walks devices in step (`buildPortList`, `limitPortVoltages`, `linearisePorts`), so there is one place to edit rather than three that have to agree. `clearState()` in `Engine.cpp` still needs its own entry, since each device type resets different members. No stamping or solver code is needed — the solver only ever sees ports.

### Stamping: one description, two systems

The cached per-sample system and the DC operating-point system are the same circuit seen two ways. `stampTopology()` in `EngineSolve.cpp` is written once for everything that stamps identically into both (resistors, `Vccs`, ideal op-amps, transformers, and a voltage source's ±1 pattern), and is templated on what to do with a terminal whose voltage is already known — the input node's coupling in one case, a fixed RHS contribution in the other. Only the genuinely physical differences stay in the callers: capacitors are open at DC, inductors are shorts, and a source's voltage lives in the per-sample RHS so an AC winding can vary without touching the cached matrix. Keep it that way; two copies that must agree exactly is how a bias point quietly stops describing the circuit that actually runs.

### Solver strategy

Two paths, chosen per circuit at `prepare()` time by `SolverStrategy::Auto`:

- **DK** eliminates the linear part once and runs Newton on the `m` ports. Wins when `m < n`, which is the case for anything valve-shaped (lots of linear plumbing, few nonlinear devices).
- **Full Newton** stamps devices into the whole `n × n` and re-factorises each iteration. Wins for small circuits where `m >= n`, e.g. a shunt diode clipper (1 unknown node, 2 ports).

Because DK lifts the nonlinear devices out of the linear matrix, **every port gets `gmin` stamped across it there as well**. Without that, a node whose only company is semiconductor terminals — a transistor's collector wired straight to the next one's base, an ordinary direct-coupled pair — has no conductance in the linear system at all and the factorisation is singular. That failure was silent: the DC system stamps the devices properly, so the bias point solved and looked right, while `process()` saw an invalid factorisation and returned 0 for every sample. This is what SPICE's gmin is for, and the device models already add the same 1e-12 inside their own port currents.

Components that state a *constraint* rather than a current — `VoltageSource`, `IdealOpAmp`, `Transformer` — take extra MNA rows after the node rows, allocated in `prepare()` via `idealOpAmpRowOffset` / `transformerRowOffset`. They are linear, so they cost nothing per sample beyond matrix size.

Force one with `setSolverStrategy()` to benchmark or to check the two agree — they match to ~1e-8 V.
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**The plugin is a circuit sandbox.** What is drawn on the sheet *is* the signal path. Two speeds of change, and the split between them is the whole design:

- **Topology** — adding a part, moving a wire, retyping a value. Changes which nodes exist, so the matrix is rebuilt and a fresh bias point solved. That allocates, so it happens on the message thread when the user presses **Rebuild**.
- **Controls** — pots and switches only move a resistance the engine has already stamped, which it re-stamps per block without allocating. So they stay live, and they are the knobs you play with.

`PluginProcessor::rebuild()` builds off-lock (stamping, factorising, one bias solve per channel) and takes a `SpinLock` only to swap pointers. `processBlock` uses a *try*-lock and passes the block through dry rather than ever blocking. A failed build leaves the previous circuit running and reports why.

**Saving:** the document is a ValueTree of type `CELINESCHEMATIC`, written to `.celsch` files. There is exactly one name for each and no second path — an earlier build wrote `AMPMODELLING`/`.ampsch` and those were read for a while, but that compatibility was removed deliberately. The consequence is the reason it was worth a decision rather than a tidy-up: a sheet saved by one of those builds no longer opens, and a DAW session whose state blob carries the old tag comes back as an empty circuit, which looks exactly like losing the drawing. "Sheets from before the rename are refused" in `tests/PresetLibrary.cpp` pins that, so it stays a choice rather than becoming a regression somebody re-fixes by accident.

`createDocument()` / `restoreDocument()` produce one ValueTree holding the drawing *and* the knob positions, used by both the host's `getStateInformation` and the Save/Load buttons (`.celsch` files) — one format, one code path, because a circuit without its knob settings is half a preset. A file that isn't ours is refused outright rather than half-loaded. `restoreDocument()` fires `onSchematicReplaced` so an open editor reloads the canvas; it's a callback rather than a cast to the editor type, and the editor bounces it through the message thread since some hosts restore state from another one.

**Presets** (`source/PresetLibrary.h`): one folder the user nominates, remembered in a `PropertiesFile` beside the host's own preferences — deliberately **not** in the document, since a preset folder describes the machine someone is sitting at, and a sheet emailed to a friend must not repoint their menu at a directory they haven't got. The library only remembers a path and lists `*.celsch` in it (flat, natural order); loading still goes through `restoreDocument()` like every other route in, so a file with the right suffix that isn't ours is refused identically however it was picked. `PluginEditor::loadPresetFile()` is that single route.

Two things about the first-run prompt are load-bearing. It fires from the **timer, gated on `isShowing()`**, never from the constructor: an editor gets built where nobody is looking — the **VST3 manifest helper builds one at build time** to read its size, and `runWithinPluginEditor` builds one in the test suite — and since the answer is remembered, either would answer the question on the user's behalf and the person who actually runs the plugin would never be asked. That is not hypothetical; the first version of this shipped the flag to `~/Library/Application Support/` on every `cmake --build`. And the flag is set when the dialog **goes up**, not when it comes back, so closing it unanswered is still an answer. `PresetLibrary::redirectSettingsForTesting()` sends the test and benchmark mains at a temp file, belt to the `isShowing()` braces.

**Oversampling** is the only setting that buys accuracy rather than spending it, and the only real fix for aliasing — which `LIMITATIONS.md` still ranks as the biggest audible gap. `PluginProcessor::oversamplingFactor` is 1, 2 or 4; `setOversamplingFactor()` re-prepares the JUCE polyphase-IIR halfband chain *and rebuilds the circuits*, because the DK discretisation is built around a fixed timestep and running four times as fast is a different matrix. Measured on the diode clipper, hard-driven: **−23.7 dB alias-to-harmonic at 1×, −43.3 dB at 2× (+65% CPU), −53.5 dB at 4× (+192%)**. The dry path rides through the halfbands alongside the wet one so a bypassed signal stays lined up with the reported latency; the alternative, a separate delay line, buys bit-exactness in a mode where nothing else is bit-exact.

**Channels:** the `channels` parameter picks Stereo, Mono L, Mono R or Mono L+R. Stereo runs one `Circuit` per side — twice the Newton iterations, which for a valve amp is the single biggest cost in the plugin. The mono modes run `circuits[0]` once and copy its output to both sides, measured at **45–52% of stereo CPU**. Nothing else in the signal path is stereo, so unless the schematic itself is (it can't be — one sheet, one topology), the second circuit was only ever computing the same answer for a different input. Switching *back* to stereo resets the idle circuits, whose capacitors still hold the charge they had when they stopped; that costs one bias solve on a user action and saves a thump. Bypass stays a true bypass in every mode, stereo image and all — only the engaged path collapses.

**Ganging:** parts sharing a label become *one* control that moves them together — an amp's channel switch throwing several contacts at once, a dual-gang volume turning two tracks on one shaft. A `LiveControl` therefore holds *lists* of everything: `pots`, `toggles` and `changeovers`, plus the `elementIds` of every part on it. The two kinds of switch contact gang with each other freely, since a real multi-pole switch mixes make-break and changeover contacts; pots gang with pots. Ganged pots keep their own resistance and taper — what is shared is the shaft, not the track, so a 1M and a 250k on one knob is expressible and does the obvious thing.

Ganging keys on the label because that is the only thing on the drawing that says two parts are one control; an unlabelled one is named by ordinal and stays on its own. The cost of that rule is that two pots you happened to call "Volume" for readability are now one knob — the same trade the switches have always made, and the only alternative is a second way of saying "these two are the same", which is a worse thing to have to learn.

The drawn **SPDT** (`ElementType::Spdt`) becomes a `Circuit::Changeover`, which is two `Switch`es held in opposition — a type of its own in the engine precisely so "never both" can't be broken by accident, which is the invariant `LiveControl::apply()` would otherwise have to maintain by hand on every call. Pin order is common, ON throw, OFF throw; `Element::closed` means the ON throw is made, which is why the inspector's checkbox renames itself "Throw A (on)" for an SPDT — "Closed" says nothing about a switch that is never open. The builder calls `select()` on the new changeover immediately, since `addChangeoverSwitch` always starts on throw A and the bias point is solved before any knob position reaches the control.

**Control order** comes from `Element::controlOrder`, applied by a `stable_sort` in the builder, so everything left at 0 keeps the order it was drawn in and only the parts you numbered move. It's a number in the inspector rather than a drag, because the strip scrolls and dragging something to a position you can't currently see is worse than typing where it goes.

The strip itself scrolls: the drawn controls sit in a `Viewport` with a fixed 96 px cell, so a sheet with more knobs than fit scrolls sideways instead of squeezing everything into slivers.

**Which way the knob positions flow.** A drawn control's position lives in two places — the `knob*` parameter the audio thread reads, and `Element::controlPosition` (or `Element::closed`) on the part itself — and the rule for which wins is the whole of what keeps them honest. **The parameters are the live truth**: they are what the host automates and what gets saved. **The part remembers** so a knob keeps its meaning when the slots shift under it. So the drawing is written *from* the parameters whenever the two could have drifted apart — `PluginProcessor::adoptControlPositions()`, called at the end of `restoreDocument()` and when an editor opens onto a plugin that has been playing without one — and the parameters are written *from* the drawing in exactly two places: a strip slot that changes hands during a refresh, and an inspector edit. Get that backwards and restoring a session written by an older version snaps every knob to noon, because those sheets have no stored positions to restore from.

The slot-change rule is why `ControlWidget` records an `elementId`. Controls map onto the fixed pool in order, so adding a pot that sorts first shifts every control down one; without checking what each slot was working before, Volume's position stays on knob1 and is inherited by whatever now lives there. A slot that keeps its part keeps its value, so pressing Rebuild after retyping a resistor doesn't move anything.

A control owning several parts is why the write-back takes a *list* of element ids: turning a ganged knob has to move every part on the shaft, or the next build seeds it from whichever was drawn first and the other half silently disagrees. **A pot's symbol draws its wiper where the knob actually is** rather than parked mid-track, so turning one in the strip visibly slides the tap along the body — fully clockwise taps pin 0, which is the top, matching `Potentiometer::setPosition` sending the resistance above the wiper to its minimum at position 1.

`Element::getControlPosition()` / `setControlPosition()` give a pot and a switch one currency, so the seeding, the write-back and the strip don't each need to know that one stores a double and the other a bool. And a pot's position and a switch's throw fire `ElementInspector::onControlEdited` rather than `onEdited`: both are live, so they reach the strip immediately instead of lighting the Rebuild button for a change you can already hear.

Drawn pots map onto a fixed pool of generic `knob1..knob16` parameters — parameters must be declared at construction, but a schematic's controls come and go — which is what makes a drawn pot host-automatable.

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `Assets.cmake` - Auto-includes all files in assets/ as binary data
- `Tests.cmake` - Configures Catch2 test target
- `Benchmarks.cmake` - Configures Catch2 benchmark target
- `PamplejuceIPP.cmake` - Intel IPP integration (optional)

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Theming

Every colour is editable at runtime, from **Theme…** in the settings menu, and a theme
can be written to and read from a `.celthm` file to be shared.

The shape of it, in four files:

- **`ui/ThemeRoles.h`** — the house list, as an X-macro. One entry carries four things
  that have to agree: the identifier the code uses, the label the editor shows, the group
  it is edited under, and the value the design ships with. The enum, the info table, the
  file's keys and the editor's rows are all generated from it. This plugin's own colours
  go in **`PluginThemeRoles.h`** beside it — the wires, the parts, the captions, the two
  selection-box rules and the six group box colours — and its accessors in
  **`PluginTheme.h`**, which `Theme.h` includes inside the namespace so
  `Theme::groupBox(2)` reads exactly like `Theme::chrome()`.
- **`ui/ThemePalette.h/.cpp`** — the colours in force, the `.celthm` reader and writer,
  and a `ChangeBroadcaster` so a change reaches every open window. One per process, in a
  function-local static: a palette at namespace scope could be read by a look and feel
  constructed before it.
- **`ui/Theme.h`** — the accessors, each a lookup, each documented with what it is *for*.
  Byte-identical to the other Céline plugins' copy; anything this plugin decides for
  itself, geometry included, lives in `PluginTheme.h`.
- **`ui/ThemePanel.h/.cpp`** — the editor. Live: a colour changed there reaches the
  window behind it on the next repaint, so there is no Apply to forget.

**Renaming a role breaks every theme anybody has saved**, because the identifier is the
key in the file. Adding one is free — an unknown key is ignored and a missing one keeps
its shipped value, which is what lets a theme written by a plugin with more colours than
this one still load.

**Each plugin keeps its own theme**, in `<PRODUCT_NAME>.celthm` under the company
folder — one file per plugin, so every instance of it on the machine wears the same
colours whatever host or format it is loaded as. A cab loader and a circuit designer are
not obliged to look alike.

They stay cross-compatible: a theme exported from one loads into another, and the
ignore-unknown-keys rule is what makes that work — the colours the other plugin does not
have are skipped, and the ones it has that the file omits keep their shipped values.

**Editing a colour writes nothing.** It changes what is on screen and marks the palette
unsaved; `Palette::store()` is the only thing that touches the file, and the theme
editor's **Save** button is the only thing that calls it. A colour picker sends a change
per mouse move, so anything automatic here is a file per mouse move.

**A plugin format is its own loaded module, with its own copy of everything static**, so
the VST3 and the AU open in one session are two palettes that never meet. They are
brought into step by `Palette::refreshFromDisk()`, called when a window opens and when
the theme editor opens — the two moments it can matter. Nothing polls, and nothing runs
in the background. Unsaved colours are left alone, so opening a second window cannot
take away what you are in the middle of choosing.

Two things a theme change has to do, and both are easy to leave out:
`PluginLookAndFeel::applyPalette()` re-reads everything JUCE is *told* rather than asks
for, and `sendLookAndFeelChange()` gives every child a chance to do the same. The window
does both in its `changeListenerCallback`.

A third is easy to leave out and impossible to see: **artwork tinted with
`Assets::tint` has to be re-read from the binary first.** Tinting writes the colour into
the drawable, so a second pass colours the result of the first — which is how the house
mark stayed white through a theme change.

`tests/ThemeReachTests.cpp` is what keeps all of this honest: it renders the whole
editor, moves every role, renders it again, and fails if any colour the design ships is
still on screen. A control that snapshots its colours and never asks again is otherwise
a silent, invisible bug.

## The house kit

`source/ui/` is shared, near-verbatim, with the other Céline plugins — the same files are
in GALLERY, AURA and SPACE. Treat a change to any of them as a change to all of them:

`Theme.h`, `ThemeRoles.h`, `ThemePalette`, `ThemePanel`, `Fonts`, `EmbeddedAssets`,
`AboutPanel`.

What is *not* shared is anything a plugin decides for itself: the roles it adds to the
palette, the accessors and geometry in `PluginTheme.h`, its `PluginLookAndFeel` drawing,
and everything under `source/Schematic/` and `source/CelineEngine/`.

**Every control gets a tooltip**, and the editor owns one `juce::TooltipWindow` parented
to itself, with `setOpaque(false)`: a tooltip paints a rounded panel, and an opaque
component must fill every pixel it owns, so the corners outside the rounding come out as
square spikes of whatever was in the buffer. Without that window nothing draws tooltips
at all — JUCE has no default, and the ones this plugin already set went unseen for it.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (Standalone AU VST3 AUv3)
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

## Releases

`.github/workflows/release.yml` builds on all three platforms and attaches one zip per platform to the GitHub release. Triggered by pushing a `v*` tag; `workflow_dispatch` produces the same zips without making a release.

Ships VST3 and LV2 everywhere, plus AU on macOS (universal). Standalone and CLAP are built but not packaged. Nothing is code signed, so Gatekeeper and SmartScreen will warn — fine for testers, not for public distribution.

Two things worth knowing:
- The macOS zip is made with `ditto -c -k --keepParent`, and must be unpacked in Finder or with `ditto -x -k`. Info-ZIP's `unzip` does not restore the AppleDouble entries and the bundle comes out with a broken signature.
- `PRODUCT_NAME` in `CMakeLists.txt` is the name hosts display and **must not carry the version**. It used to ("Céline 0.7.7"), which is handy while developing — versions install side by side — but it makes every release a differently-named plugin, so hosts cannot find the old one in saved projects.

- `PROJECT_NAME` is the *internal* target name and must be plain ASCII — CMake target names are limited to `[A-Za-z0-9_.+-]`, so an accent there fails the configure with "The target name is reserved or not valid". The accented name belongs in `PRODUCT_NAME`, which is what hosts display. `.github/workflows/release.yml` hardcodes `PROJECT_NAME` a second time (it builds `${PROJECT_NAME}_All` and reads `${PROJECT_NAME}_artefacts`), so a rename has to be made in both places; `build_and_test.yml` reads it from the `.env` CMake writes and follows along on its own.

Adding LV2 required an `LV2URI` in `juce_add_plugin` — JUCE otherwise defaults it to `${company_website}/plugins/<name>`, which is schemeless and fails a static assert when `COMPANY_WEBSITE` is unset. Note that changing `LV2URI` later makes hosts treat it as a different plugin.

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: LSP/clangd often reports false positive diagnostic errors (like "undeclared identifier", "file not found") because it doesn't have full context of the JUCE module system. Ignore these unless the actual build fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. One worth knowing about:

- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
