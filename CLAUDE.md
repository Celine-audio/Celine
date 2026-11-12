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

    Models are `id|Name|What it is|Group` records separated by semicolons, the group optional — the short name goes under the part and into the dropdown, and the description shows in the inspector under it. **Descriptions are a few words** ("~0.4 V, soft knee", "Low noise, hFE 300-900"), not sentences — the reasoning belongs in the model's header comment — and may be empty, in which case the inspector shows no line (the LM386, the one power amp, has none). **Names are part numbers**; what kind of part it is goes in the group, which the dropdown shows as a heading: the diodes are grouped Silicon, Germanium, Schottky, LED and Zener, and the diode symbol is chosen from that group, never from the name.

    **The id is the file format, and it is the only field that is.** `Element::modelIndex` is still an index in memory, because that is what the renderer and the builder switch want, but it is resolved from the id on load and written back as the id on save. So this table can be reordered, sorted, added to or pruned freely — the constraint that used to force every new model to the end of its list is gone. Ids must be unique per element type, lower case and free of `|`, `;` and spaces; nothing at load can catch a duplicate, so `tests/PluginBasics.cpp` does.

    Every id here begins **`celine:`**, a reserved namespace for the models the plugin ships — which are immutable, authored by Céline (`getModelAuthor()`), and the only models that need no author stored per row, since they all share one. A user library will mint ids as `author:name:hash`, frozen at creation so renaming is a display change rather than a broken sheet, and the prefix is what stops a user model shadowing one of ours or being shadowed by one added later.

    An id a sheet names and the table no longer has falls back to model 0 **and says so in the console**, naming the id and its author so you know who to ask for the file. A sheet with no ids at all — anything written before this change — gets one message saying how many parts fell back. There is no compatibility path and no index in the file: a silent fallback is the failure the scheme exists to remove, since such a sheet still builds and still makes sound with the wrong parts in it.

    The unresolved id is **kept**, in `Element::unresolvedModelId`, and written back out unchanged instead of the fallback's. That matters because the save is often nobody's decision — a host writes session state on its own, so a sheet inside a DAW project would lose the reference just by being opened and closed on a machine without the model, and it would then be unrecoverable even once you obtained it. The part draws its missing model in the unset colour next to a zero value, and the inspector shows it greyed and marked `(missing)`; choosing any model clears it.

    The `ElementType` enum *is* still positional and still written to saved sheets, so that one is still **append, never reorder**.

    That list and the `switch` in `SchematicBuilder` are two halves of one thing and must be extended together. The switch ends in `default:`, so a mismatch doesn't fail — an index the builder doesn't know silently behaves as model 0. That is how the three measured 12AX7s and the KT66/KT77 shipped implemented but unreachable, and why "Every valve model in the palette reaches the engine" in `tests/PluginBasics.cpp` builds a stage per index and insists the answers differ.

    BJTs, JFETs and MOSFETs are separate element types rather than models of one "transistor": the pins mean different things, the symbols differ, and they reach the engine through different device models. Polarity for the arrow direction comes from `isReversePolarity()`, which reads the model's *group* ("PNP", "P-channel", or anything starting with those) — never the part number, since renaming the models once made every transistor silently draw as an NPN. Transistor groups are just the polarity.

    **No doubles.** A model is a row in a dropdown, so two rows that build the same circuit are clutter. When part numbers differ only in something the engine does not model — the BC107/108/109 and BC547-550 are one die sorted for breakdown voltage and noise; one amplifier of a TL072 is a TL071 — they are one model, named for one of them, with the others in its description. Where the difference *is* modelled — those same dice sorted into gain grades A, B and C — they are separate models. The 1N914 is the one listed alias of the 1N4148, kept because half the schematics in the world print it, and the diode dispatch test names it rather than exempting an index. The BS170 is not one: on Vishay's sheet it is the 2N7000's die in another lead-out, so it lives in the 2N7000's description.

    The **Power amp** (the LM386) is its own element type, not an op-amp model: its feedback network is inside the package, so drawn as an op-amp with external feedback it would be wrong by a factor of twenty. Its two extra pins are the datasheet's 1 and 8, and the builder does not report them as dangling when left open, since open is gain 20.

    A pentode has four pins, not five: the suppressor grid is drawn strapped to the cathode inside the envelope, because the Koren model has no term for it and a terminal that cannot change anything is worse than none.
  - `Schematic.h/.cpp` - elements + wires, net extraction, save/load. Net extraction is union-find over connection points, rebuilt from scratch each time: coincident pins connect with no wire, a wire ending partway along another joins it, and terminals *name* their net — which is why two unwired Ground symbols become one node.
  - `SchematicBuilder.h/.cpp` - the one place geometry becomes a netlist. Everything it has to say comes back as `Diagnostic` records rather than joined-up prose: severity, sentence, and **which part, at which grid position**. Six problems in one sentence is a paragraph nobody reads to the end of; six rows each naming one part is a list you work through, and the console can select the part when you click it.

  Two failure modes here exist because they were once silent. A **failed factorisation** means `process()` returns 0 for every sample, and the DC system is a *different matrix* — so the bias point can settle on perfectly sensible voltages while the per-sample one is singular. The build now checks `hasUsableFactorisation()` (guarded on `getSystemSize() > 0`, since a circuit whose every node is ground or input has nothing to factorise and is obvious on the sheet). And an **output terminal wired to ground** is caught by name: the net takes ground's name, so `setOutputNode("out")` invents a node with nothing on it, whose all-zero row is singular — a confusing way to learn you shorted your output. Transformers are the one part it *expands* rather than translates: the **Real** model wraps the ideal device in copper resistance, leakage and magnetising inductance, on internal nodes named after the element id. All four numbers come from `straysFor()`, which derives them from the turns ratio against an assumed 8 Ω secondary — one stated assumption instead of four free parameters, which is also why it is documented as a shape rather than a particular transformer. Returns one `Circuit` per channel plus the `LiveControl`s. Validation failures are sentences ("No ground symbol"), not singular matrices.

  `BuildOptions` holds what a build needs beyond the drawing, and **nothing in it may make the simulation less accurate**. Valve interelectrode capacitance, semiconductor junction capacitance and the transistor Early effect used to be switches there, traded for CPU, along with a predicted Newton starting point. Measured with the solver's reduced Newton step on six real sheets, they bought 0–6% each while moving the output to 7–57 dB below its peak, and the prediction swung ±9% by circuit, so all four went. What is left is `fastMath`, which is not an accuracy trade (its approximations are four orders tighter than Newton's tolerance) but a platform one: 0.3–2.8% *slower* on Apple Silicon, whose libm is excellent, kept for Windows and Linux. A sheet saved with one of the removed switches still carries the property; `restoreDocument()` ignores it, the sheet opens on the full model, and the next save leaves it out. Options ride in `createDocument()`, unlike the window size, because they change what the sheet sounds like; a file that predates an option simply lacks the property and gets the default.
  - `ExampleSchematics.h/.cpp` - starter circuits, laid out by hand on the grid.
- `assets/icons/elements/` - **the part symbols, as SVG.** Replacing a resistor is replacing a file, not editing C++. `source/ui/SymbolArtwork.h` states the convention artwork must follow and why each rule exists: viewBox `-60 -60 120 120` with **(0,0) the part's centre** (strictly, the viewBox centre is, so Affinity's `0 0 120 120` works too), **10 SVG units to a grid square**, and stroke-width `1.2` as the normal line weight (heavier lines are multiples of it). The scale rules are not cosmetic — the canvas draws pins on top of the artwork at the positions in `Element.h`, so a file drawn to any other convention lands its terminals somewhere the wires cannot reach.

  Geometry is parsed once and cached **in grid space**, then stroked by the canvas at `max(1, gridSize * 0.12) * scale` pixels. That floor is the whole reason the artwork is not simply handed to `Drawable::draw`: letting the view transform scale the strokes makes a sheet zoomed out to see all of it fade to nothing. It also means painting allocates nothing — the paths are cached and only an `AffineTransform` changes per part.

  A part whose state changes its symbol gets **one file per state** rather than a flag: `switch-open`/`switch-closed`, the xPyT switch's `spdt-a`/`spdt-b`/`spdt-off` and `sp3t-a`/`sp3t-b`/`sp3t-c` (one per throw the lever can be on, plus an ON-OFF-ON's middle), `capacitor`/`capacitor-polarised`, four diodes, both transistor polarities. So "redraw the open switch" is one drawing. Assets are looked up by **original filename**, never by the C++ identifier JUCE derives from it — that derivation drops hyphens rather than replacing them (`capacitor-polarised.svg` becomes `capacitorpolarised_svg`), and guessing it wrong is silent: the part draws as nothing but its pins. `tests/SymbolArtwork.cpp` walks every name for exactly that reason.

  **Every symbol is drawn as authored, as vectors**: when a file is first used, every stroke is outlined at its authored width with its caps, joins and transform (as SVG specifies, so a stroke inside a squashed group is squashed with it) and every fill is kept, all merged into one filled path per level of opacity -- `SymbolArtwork::ink`. Painting a part is then one `fillPath` per run in whatever colour the part is drawn in, so it tints exactly as a line drawing does: theme colour, `Theme::selected()` when selected, the ghost colour while placing. Sharp at any zoom and density, and a zoom costs what a repaint costs. Colours in the file are ignored; opacity (`fill-opacity`, or an element's `opacity`, which JUCE keeps on a group with no getter and so is measured once by drawing the group) is how a shape is lighter than its outline. The cost, accepted, is that the ink has no stroke floor and thins when zoomed far out. Outlining flattens curves, so it runs at twenty times JUCE's default precision: at the default a valve's bulb was a thirteen-sided polygon at full zoom on a 2x screen. A run is filled by winding, so ink that only overlaps must wind the same way: a fill traced against the stroke outline around it cancelled into a hollow arrow (a P-channel MOSFET's, a PNP's). Every outline is therefore turned to wind positively, and every hole -- an outline nested inside an odd number of others -- negatively, before it joins a run (`oriented()`); fills are flattened there to a quarter of a device pixel at full zoom. The paths are still extracted, for the bounds, the hit test and the tests.

  This replaced a renderer that rasterised each symbol into a mask per zoom and orientation. Through CoreGraphics on the 245 parts of the three `_test schematics` sheets, a frame of symbols went from 3.0 to 2.0 ms steady and 6.2 to 2.2 ms zooming at 2x; at 1x a still frame is a tenth slower (1.75 to 1.95 ms) and a zooming one 39% faster. `tests/SymbolRender.cpp` (git-ignored) renders symbols at chosen zooms beside their raw ink and runs that benchmark: `./Tests "[.symbolperf]"`, `BENCH_NATIVE=1` for CoreGraphics.

  Three things stay in code because they are *readings* of a part's state rather than pictures of it, and no file could hold them: a potentiometer's wiper (it slides with the knob), the IN/OUT lettering (drawn with a font so it stays upright), and Text and Rectangle, which are a note and a frame you drag the corner of.

- `source/ui/` - `SchematicSymbols` (drawing), `SchematicCanvas` (the sheet you draw on), `EditorPanels` (palette, inspector, and the message console). The canvas has **rulers** along its top and left, labelled every 1/2/5/10/… grid squares depending on zoom so the numbers never collide, with a cursor marker on both that follows the pointer — build messages name grid coordinates, so without a ruler you are counting squares to find the part one points at. Each ruler is its own **opaque child component**, and both halves of that matter: the marker has to move whenever the mouse does, and a marker drawn inside `SchematicCanvas::paint()` costs a full canvas repaint every time — measured at **0.44 ms on the default sheet and 2.98 ms on a busy one**, against **0.08–0.11 ms for the two strips alone**, which is 5–28× cheaper and flat in the size of the drawing. *Opaque* is what keeps it that way: a transparent child makes JUCE repaint the parent behind it, putting the whole cost straight back. They take no mouse input, so the sheet still receives everything at its edges.

  The sheet runs edge to edge and **three panels float over it**, each sized to what it is showing; `PluginEditor::layOutFloatingPanels()` places them, and runs again whenever the inspector or the console says its size changed (`onPreferredSizeChanged`) as well as on a resize. **Levels in dBu.** The circuit is driven at one volt peak per unit of full scale and its output is read at the same scale, so a full-scale sine is -0.79 dBu at either end (`PluginProcessor::fullScaleSineDbu()`). The settings menu's **Show input and output in dBu** switch makes the INPUT and OUTPUT knobs read as calibration levels rather than gain: input `gain - 0.79`, output `-0.79 - gain` (the output's gain comes after the circuit, so its sign flips) -- exactly NAM's `input_level_dbu` and `output_level_dbu` for a capture rendered here. Typed values are read in whichever unit shows. The switch is a per-machine choice kept in the settings file beside the update check (`PluginEditor::levelsShownInDbu()`), not in the session. The bottom band's **INPUT** bar ends exactly where the palette's card ends when folded to symbols (`ElementPalette::getFoldedBodyWidth()`, the fold tab not counted), folded or not, so opening the palette never moves the band; the **OUTPUT** bar starts exactly under the inspector's left edge. `layOutBand()` therefore runs after `layOutFloatingPanels()` places them, and again whenever they move. A bar is never narrower than what it holds, and its contents sit against its inner edge (the one facing the sheet) at the strip's padding, so the INPUT and OUTPUT knobs stand the same distance in from it whatever width their bars come out; spare width goes to the window edge. The palette is padded 10 px on all four sides (`palettePad`). The **inspector** is the iPad designer's card, on the panel's light ground like the palette: a heading with the part's symbol in a tile, its name and its model's description (no close button: clicking empty sheet already deselects), a rule, then the fields padded 14 px (`inspectorPad`) under small-capital captions, label first and model second as the designer asks. Rotate, flip and delete are not on it: the toolbar has all three. Its fields and dropdowns carry `inspectorFieldProperty`, which `PluginLookAndFeel` reads to left-align a dropdown's text. Its switches are the plugin's one switch, also the control strip's, and the house one: the kit's `LookAndFeelBase::drawToggleButton` draws it wherever `pillSwitchProperty` is set (a track that turns accent with a dot on the side that is made, name left and track right when it has a name, centred when it has none), and `PluginLookAndFeel::applyPalette()` colours it for the light panels through `pillSwitchTrackColourId` and `pillSwitchDotColourId`; a value field still at zero goes red, box and lettering, like the part's caption on the sheet. The **palette** is a panel down the left, two columns wide: each row is one of the pairs written on a line of `paletteOrder`, always two to a row. The column (`Theme::paletteColumnWidth`) is the fixed thing and the lettering fits it: `ElementPalette::getNameFontHeight()` is the largest size, up to `Theme::paletteFontHeight`, at which the longest name fits, one size for every entry (the full 11.5 pt with the shipped names: the column is sized for MOSFE Transistor, the longest, to fit at it). An earlier version gave a too-long name a row of its own instead, which left the grid ragged. The **arrow tab** standing off its right-hand edge folds it to symbols alone and back — the user's choice, never the window's: an earlier version folded on its own whenever the names did not fit, and a panel that changes what it shows as you resize is one you cannot learn. The folded state lives on the processor (`paletteCollapsed`), so a reopened window keeps it, but in neither the state nor the document. `ElementPalette::hitTest()` claims only the panel and its tab, so the sheet beside the tab still takes clicks. A window too short for the list scrolls it. The palette runs the **full height** of the sheet, as far from the bottom band as from the ruler, because the console goes *between* it and the inspector rather than under it — and the editor's minimum width is worked out (`PluginEditor::narrowestWindow()`) so there is always room for the console there: ruler, palette at its widest (unfolded, scrolling), `MessageConsole::minimumWidth`, the inspector's column, a margin either side of each. Only a host that ignores that minimum gets the fallback, where the palette stops one console line short. The **inspector** is a card down the right from the top, always `Theme::inspectorWidth` wide so it never jumps sideways as the selection changes, and as tall as its fields (they scroll past the room there is). What it shows is fitted to that width: the shortcut list's lettering is the largest size, up to 13 pt, at which its longest line fits with a scrollbar's width to spare. Nothing may leave the sheet less its rulers and a margin, and nothing may overlap anything else; "The floating panels sit on the sheet and clear of one another" walks the window from its minimum size up, with and without a part selected, to hold that. The **`MessageConsole`** is a pill centred above the bottom band, one row per message coloured by severity, selecting the part when a row is clicked. It shows itself whenever something new arrives and fades `MessageConsole::hideDelayMs` (5 s) later — *unless* what it shows includes an error or a warning, which holds it up until a message without one replaces it, and it waits while the pointer is over it. A headline-only update (`setHeadline`) keeps the diagnostics beneath it, so a warning still listed still holds the console up. **Held samples are reported there too**: a sample the engine cannot settle is held (the one before it repeated), and the processor adds every one the live circuits hold to `getHeldSampleTotal()`, an atomic that only grows. The editor's timer compares it with what it last reported and, at most once a second, puts up a warning headline (`setWarningHeadline`) saying how many. It starts from the total when the window opens, so samples held while nobody was looking are not reported late. "Every factory example runs without holding a sample" keeps a shipped sheet from warning on its own. `SchematicCanvas::setFramingInsets()` tells the sheet what the palette and inspector cover — the left and right edges — so `F` and a console click frame the drawing between them; the console is left out of that, since it comes and goes. A part's caption is drawn as **coloured runs rather than one string**, because what a part is called and what it is set to are different kinds of fact: names (the label, and the model) are **yellow**, a value someone entered is **green**, and a value still at zero is **red**. That last one pairs with the table's zero defaults — see `Element.h` — so an unfilled part is loud on the sheet and refuses to build. **Where a caption goes** is chosen per part, per paint (`SymbolPainter::draw`): each type has a natural side in its own frame (`naturalCaptionSide` — the side its wires don't leave from: right for two-pin parts, transistors and valves, left for a pot whose wiper takes the right, below for op-amps and transformers, above for the scope), turned and mirrored with the part, half a square off the drawn body and centred on the part. Beside a part the runs stack, name over value; above or below they share a line. The canvas scores each candidate through `SymbolPainter::captionClutter` — another part or an already-placed caption counts ten, a wire crossing one — and the natural side, then the opposite, then the two across are tried; the first clear one wins, else the least cluttered. Captions are kept off each other in drawing order. It used to be one line a square below the lowest pin, which on a vertical part was on top of the wire leaving that pin and well away from the body. The split between the two panels is by *what an action does to the sheet*: the **palette** holds everything you add — the wire first, then the parts — and the **toolbar** holds what you do to what is already there, **Select** (S) and **Delete** (X). Wire (W) is a tool rather than an element type only because of how it is implemented, which is not a distinction worth making anyone learn, so it sits with the parts. Right-drag pans the sheet and middle-click clones whatever is under it — the Place tool is armed with a *copy* of that part, value, model, label and all, so "five more of that one" is one click and five. `Element::copyPropertiesTo()` is the single place that says what "the same part" means, so a member added to `Element` has one place to be remembered rather than being silently dropped. Right-click no longer deletes — and Delete is `X` rather than `D`, because `D` names the diode — the tool letters (`S`, `W`, `X`, plus `F` to frame the whole drawing) are deliberately none of the parts' initials, so the two sets can grow without colliding. `F` is a verb among nouns, which is allowed because it moves the *view* rather than the sheet — Command stays reserved for acting on the parts, and `Cmd-F` still flips. A bare letter names a part and arms it for placing — `R` resistor, `C` capacitor, `L` inductor, `T` transistor, `D` diode, `G` ground, `I` input, `O` output, `B` box (the group rectangle; `R` was already the resistor, which is why the letter names the thing rather than spelling it) — so the common parts go down without a trip to the palette. Command turns a letter into an action on what is already there: `Cmd-R` rotates and `Cmd-F` flips (mirrors) the part *being placed* when one is armed, otherwise the selection — and both stick across placements, so a run of identically oriented parts is set up once.

The split is deliberate: an unmodified letter always *names* a thing, a modified one always *does* a thing, so the two sets can grow without colliding. It does mean the part letters and the tool letters share one namespace — `R` is the resistor, not Rotate — which is why rotate and flip moved under Command rather than keeping the bare letters they had.

The window is resizable and every pixel of a resize goes to the canvas: the panels float over it at the size of what they show, and the control strip is a row of knobs, so stretching any of them buys nothing. The size lives on the *processor* (`editorWidth`/`editorHeight`), because the editor is destroyed and rebuilt each time the window closes — and it rides in `getStateInformation` but deliberately **not** in `createDocument`, so loading a `.celsch` never resizes your window.

  A group box breaks the "click a part to select it" rule twice over, and both are load-bearing. It is **grabbed by its edge**, not its interior — `Schematic::hitTest()` — and rectangles are searched **after** everything else in `findElementAt()`, because a box is drawn behind the circuit and so has to lose to it under the cursor too. Get either wrong and ringing a finished stage makes every part and wire inside it unselectable and undeletable, which is the exact opposite of what drawing one is for. Select it and its bottom-right corner becomes a resize handle; the drag pins the opposite corner by moving the centre, which works out exactly because `Rectangle<int>::withCentre` and `getCentreX` are inverses at odd sizes too.

  Mirroring is applied **before** rotation, in `Element::getPinPosition()` and in the renderer's `LocalSpace` alike; if those two orders disagreed the pins would stop landing on the symbol. Flip swaps the part's own left and right rather than the sheet's, which is what makes it mean the same thing at every angle.

  **Keyboard focus matters here and is easy to break:** every palette entry and toolbar button sets `setWantsKeyboardFocus(false)`, and `SchematicCanvas::setTool()` grabs focus. Without both, clicking a palette part moves focus to that button and the canvas shortcuts silently stop working until the next click on the sheet — which, for the part being placed, is one action too late. `R` and `F` are the only way to turn a part while placing, since the inspector's buttons need a selection, so anything that takes focus away breaks the feature outright.

  Keyboard shortcuts match on the text character *or* the key code, because a real letter keypress carries the upper-case code while a `KeyPress` built from a character keeps whatever it was given.

  Note `Schematic::getElementBounds()` computes min/max by hand rather than with `Rectangle::getUnion`: a rectangle built from two identical points is zero-sized, JUCE counts that as empty, and `getUnion` returns the *other* operand when either side is empty — so unioning pin positions silently collapses to one pin. That made every part unclickable except exactly on a pin.
- `modules/CelineEngine` - **the circuit simulator, in its own repository** (github.com/Celine-audio/CelineEngine). Included by repository, `#include <CelineEngine/Engine.h>`, and built through `CelineEngine.cmake` exactly as CelineUI is. Its layout, the DK method, how to add a device, the stamping and solver rules and `LIMITATIONS.md` are all in **that repository's CLAUDE.md** — read it before touching the engine. The same two rules as the kit apply: work in the checkout beside the plugins, building with `-DCELINE_ENGINE_DIR=../CelineEngine`, never in `modules/CelineEngine`; and commit and push the engine before moving the submodule. The engine's tests stay here (`tests/CelineEngine.cpp`, `tests/ComponentModels.cpp`, `benchmarks/`), since some build drawn sheets, and so do the datasheets in `docs/specs/`.

- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions, CelineUI — the house kit, see "The house kit" — and CelineEngine, the simulator
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**The plugin is a circuit sandbox.** What is drawn on the sheet *is* the signal path. Two speeds of change, and the split between them is the whole design:

- **Topology** — adding a part, moving a wire, retyping a value. Changes which nodes exist, so the matrix is rebuilt and a fresh bias point solved. That allocates, so it happens on the message thread when the user presses **Rebuild**.
- **Controls** — pots and switches only move a resistance the engine has already stamped, which it re-stamps per block without allocating. So they stay live, and they are the knobs you play with.

`PluginProcessor::rebuild()` builds off-lock (stamping, factorising, one bias solve per channel) and takes a `SpinLock` only to swap pointers. The swap is a **dip through silence**, not a cut: the running circuit fades out over 30 ms in `fadingCircuits`, the new one takes over at the silent sample, and fades in over 30 ms. A fresh circuit starts at its bias point with every capacitor at rest while the old one is mid-waveform, so a one-sample swap popped on every Rebuild with a note sounding — a step of 12 V against a 0.45 V waveform step on the 12AX7 example, and nothing at all on the default diode clipper, which holds no state and is why it went unnoticed. A dip rather than a crossfade, deliberately: only one circuit ever runs at once, so a sheet near a core's worth of CPU cannot overrun during the switch, and it works across a change of oversampling factor, where the old circuit fades out on the filters it was built for (`fadingOversampler`) and the new one comes in on its own — `setOversamplingFactor()` and `restoreDocument()` hand the filters to `rebuildAt()` so the two swap together. The cost, accepted, is a gap of about 60 ms on every Rebuild. The audio thread cannot free the faded circuit, so it flags the fade as done and the editor's timer calls `releaseFadedCircuits()`; `prepareToPlay` swaps straight in with no dip, since audio was stopped. "Pressing Rebuild while a note plays does not click" in `tests/BypassTests.cpp` runs every example sheet through a rebuild and both directions of a factor change, and checks the output really reaches silence at the turn. `processBlock` uses a *try*-lock and passes the block through dry rather than ever blocking. A failed build leaves the previous circuit running and reports why.

**Saving:** the document is a ValueTree of type `CELINESCHEMATIC`, written to `.celsch` files. There is exactly one name for each and no second path — an earlier build wrote `AMPMODELLING`/`.ampsch` and those were read for a while, but that compatibility was removed deliberately. The consequence is the reason it was worth a decision rather than a tidy-up: a sheet saved by one of those builds no longer opens, and a DAW session whose state blob carries the old tag comes back as an empty circuit, which looks exactly like losing the drawing. "Sheets from before the rename are refused" in `tests/PresetLibrary.cpp` pins that, so it stays a choice rather than becoming a regression somebody re-fixes by accident.

`createDocument()` / `restoreDocument()` produce one ValueTree holding the drawing *and* the knob positions, used by both the host's `getStateInformation` and the Save/Load buttons (`.celsch` files) — one format, one code path, because a circuit without its knob settings is half a preset. A file that isn't ours is refused outright rather than half-loaded. `restoreDocument()` fires `onSchematicReplaced` so an open editor reloads the canvas; it's a callback rather than a cast to the editor type, and the editor bounces it through the message thread since some hosts restore state from another one.

**Presets** (`source/PresetLibrary.h`): one folder the user nominates, remembered in a `PropertiesFile` beside the host's own preferences — deliberately **not** in the document, since a preset folder describes the machine someone is sitting at, and a sheet emailed to a friend must not repoint their menu at a directory they haven't got. The library only remembers a path; loading still goes through `restoreDocument()` like every other route in, so a file with the right suffix that isn't ours is refused identically however it was picked. `PluginEditor::loadPresetFile()` is that single route.

The preset field drops down **`PresetLibraryPanel`** (`source/ui/`), where a plain menu used to open: GALLERY's library panel — search with the magnifier, chevron tree and row style — holding circuits, with rows at 14 pt rather than GALLERY's 11.5 since they are read as a menu's are. **It has no header band of its own**: one under the preset field would show the same bar twice, so while the library is up the part of the panel lying exactly over the preset field *is* the field (same place, size and fill), holding the search, and the list hangs below. No icons of its own: the toolbar's Save sits just left of the field, and the **folder icon** at the field's right-hand end is the editor's (`presetFolderButton`), laid over the field whether the library is open or not and brought to the front when it opens, so it is one icon in one place and always clickable; the field (`PresetButton::setTrailingSpace`) and the search (`setFieldTrailingSpace`) both stop short of it. A thin rule divides the **root groups** — New, the factory folder with its examples, each top-level folder with whatever is open under it, and the presets loose at the top as one group — and none divides subfolders (`hasDividerBelow`). The list is exactly as wide as the preset field and a name that does not fit is cut with an ellipsis, stopping short of the in-use mark. It is exactly as tall as its rows, one row's room when a search finds nothing; an earlier floor of three rows left a single match sitting on empty list. The editor tells it where the field is (`setFieldArea`), and `hitTest` claims only the field and the list. It is not in CelineUI; GALLERY's is its own `LibraryPanel`, and this follows it rather than sharing it. It keeps everything the menu did: **New (empty sheet)** first, the shipped examples as a **Factory presets** folder (open by default), then the preset folder **as a tree**, subfolders and all (folders first, natural order, hidden files and anything but `.celsch` left out, depth and count capped as GALLERY's are). The folder icon chooses the preset folder; right-click reveals what was clicked, or the folder. The preset in use is marked and its folder opened, a search reaches into shut folders and opens the way to each match, and the folders somebody opened stay open (the panel is owned by the editor, not made per opening). It is read fresh from disk every time it opens. A click on a preset loads it and puts the library away, as the menu did; so do Escape and a click anywhere outside it — `LibraryDismisser` covers the window behind it while it is up. The search field takes the keyboard on opening and the canvas gets it back on closing. `tests/PresetLibraryPanel.cpp` builds a real folder tree and drives it.

Two things about the first-run prompt are load-bearing. It fires from the **timer, gated on `isShowing()`**, never from the constructor: an editor gets built where nobody is looking — the **VST3 manifest helper builds one at build time** to read its size, and `runWithinPluginEditor` builds one in the test suite — and since the answer is remembered, either would answer the question on the user's behalf and the person who actually runs the plugin would never be asked. That is not hypothetical; the first version of this shipped the flag to `~/Library/Application Support/` on every `cmake --build`. And the flag is set when the dialog **goes up**, not when it comes back, so closing it unanswered is still an answer. `PresetLibrary::redirectSettingsForTesting()` sends the test and benchmark mains at a temp file, belt to the `isShowing()` braces.

**Oversampling** is the only setting that buys accuracy rather than spending it, and the only real fix for aliasing — which `LIMITATIONS.md` still ranks as the biggest audible gap. `PluginProcessor::oversamplingFactor` is one of `oversamplingFactors` — 1, 2, 4, 8 or 16, one halfband stage per doubling — and anything else a document names is rounded down to a power of two in range. `setOversamplingFactor()` re-prepares the JUCE polyphase-IIR halfband chain *and rebuilds the circuits*, because the DK discretisation is built around a fixed timestep and running four times as fast is a different matrix. Measured on the diode clipper, hard-driven: **−23.7 dB alias-to-harmonic at 1×, −43.7 at 2×, −59.8 at 4×, −93.1 at 8×, −99.1 at 16×**, each doubling a little under twice the CPU — 16× is 12× the clipper's 1× cost and 14× a 12AX7 stage's, 65% of a core in stereo at 48 kHz for that one stage. The table is in `LIMITATIONS.md`; 16× is for rendering on anything larger. The bypassed signal is a delay line matched to the reported latency, not the dry signal riding through the halfbands — see **Bypass** below for why that changed.

**Bypass** is the house one, AURA's and GALLERY's: the host's bypass is this plugin's own (`getBypassParameter`), so the power button lights whichever is pressed, and it is a **30 ms crossfade** (`source/dsp/BypassFade`) rather than a switch. The circuit and the dry signal are never in step — the halfbands and every coupling capacitor shift the phase — so swapping one for the other in a sample was a step in the waveform. Two things follow, and both look like something to tidy up; neither is:

- **The circuit, the oversampler and the cabinet all run while bypassed**, and the fade decides how much is heard. The circuit always did, for its capacitors' sake. The cabinet used to be skipped, and a convolution that is not fed keeps the input it last heard and plays it on the way back in — measured at 0.036 of full scale out of silence here, and at 0.99 in GALLERY.
- **The dry copy is taken at the host's rate**, before the oversampler, and delayed by the latency it reports. It used to ride through the halfbands with the circuit, which lined it up but filtered it; the crossfade has to happen after the cabinet, out here, and a copy of the input is the only dry signal there is. So bypassed is now bit-exact, at every factor. The delay line is sized once for `maximumOversamplingLatency` — the factor changes while audio runs, and the line cannot grow then — and a test holds every factor's real latency under it.

Two consequences of the dry path being a delay line. A block that misses the circuit lock, or that has no circuit to run, is the *delayed* dry copy through `BypassFade::substituteDry()`, not an early return — returning early sent the input out undelayed, a jump by the latency, and left the delay line a block behind. And when the factor changes while audio runs, the latency moves with it, so the delay crossfades to its new read position over the same 30 ms rather than skipping samples of the input.

`tests/BypassTests.cpp` holds all of it: the host's bypass, the button, no click at 1× or 4× or with a cabinet, no click on a factor change, no ghost, and bit-exact bypass in stereo and mono modes.

**File input** (`source/dsp/InputFilePlayer`) is for benching: the Input terminal's panel can feed the circuit from an audio file instead of the host's input, once or on a loop, with Play and Stop. It is **deliberately in neither `createDocument()` nor `getStateInformation()`**. A path from one machine does not exist on the next, and a sheet that started playing something when it was opened would be a preset with a side effect. So the player lives on the processor, survives the editor closing, and every session starts empty on live input. `tests/InputFilePlayer.cpp` holds the document and the state blob byte-identical across a load. Three threads, and each has its own job. The message thread only asks. One worker thread, made on first use, decodes the file and **resamples it once to the host's rate**: a Kaiser-windowed sinc with 64 zero crossings, which an offline pass can afford and a realtime one could not. A file already at the session rate is not touched, so it arrives bit-exact. The audio thread copies samples under a try-lock that the worker holds only for a pointer swap. `prepareToPlay` at a new rate drops the buffer (while audio is stopped, the one moment nobody hears it at the wrong pitch) and resamples again from the file on disk. The file is written over the input **before everything**: before the bypass's dry copy, so bypass gives back the file itself, and before the circuit try-lock, so a block that misses the lock does not let the live input slip out. Input gain still applies. The inspector drives the player directly rather than through the editor, because nothing here is undoable, needs a rebuild or marks the preset modified. It re-reads the player on every timer tick, because a file plays to its end and a load finishes without telling anyone.

**Update checks** (`source/UpdateCheck.h`) ask GitHub's `releases/latest` whether a newer Céline is out. **Off until the user says yes**: the first time a window is in front of a person they are asked once (after the preset folder question — the editor puts up one dialog at a time, `openPrompt`), closing it is a no, and the Settings menu has "Check for updates at launch" to change it. The answer lives in the same per-machine settings file as the preset folder (`PresetLibrary::getSettingsFile()`), so the test redirect covers it too. **One request per process, at most**: `UpdateCheck::Session::startIfNeeded()` starts it on a background thread with a 5 s timeout (on Linux through curl, `NEEDS_CURL`: JUCE's own Linux networking speaks only http, and the request is https) and is a no-op forever after, whatever happened — offline, timed out, an error body, nothing retries. Its "already started" flag and its result are plain statics rather than part of the thread object, because a host can shut JUCE down and start it again inside one process, and "once per launch" has to survive that. **Only newer counts**: `isNewer()` is strictly greater, component by component as numbers (1.10 > 1.9, 1.2.02 is 1.2.2, missing components are zero), and false whenever either side cannot be read — so a dev build ahead of the latest release never hears about it. When it finds one, the first window to ask gets "A new update is available" with Download (opens `ProductInfo::repositoryUrl` + `/releases` in the browser), Remind me later (nothing more until the next launch) and Turn off update checks. The tests never touch the network: a test editor is never on screen, so it never starts the check, and everything that decides what the user is told is pure and tested in `tests/UpdateCheck.cpp`.

**Channels:** the `channels` parameter picks Stereo, Mono L, Mono R or Mono L+R. Stereo runs one `Circuit` per side — twice the Newton iterations, which for a valve amp is the single biggest cost in the plugin. The mono modes run `circuits[0]` once and copy its output to both sides, measured at **45–52% of stereo CPU**. Nothing else in the signal path is stereo, so unless the schematic itself is (it can't be — one sheet, one topology), the second circuit was only ever computing the same answer for a different input. Switching *back* to stereo resets the idle circuits, whose capacitors still hold the charge they had when they stopped; that costs one bias solve on a user action and saves a thump. Bypass stays a true bypass in every mode, stereo image and all — only the engaged path collapses.

**Ganging:** parts sharing a label become *one* control that moves them together — an amp's channel switch throwing several contacts at once, a dual-gang volume turning two tracks on one shaft. A `LiveControl` therefore holds *lists* of everything: `pots`, `toggles` and `changeovers`, plus the `elementIds` of every part on it. The two kinds of switch contact gang with each other freely, since a real multi-pole switch mixes make-break and changeover contacts; pots gang with pots. Ganged pots keep their own resistance and taper — what is shared is the shaft, not the track, so a 1M and a 250k on one knob is expressible and does the obvious thing.

Ganging keys on the label because that is the only thing on the drawing that says two parts are one control; an unlabelled one is named by ordinal and stays on its own. The cost of that rule is that two pots you happened to call "Volume" for readability are now one knob — the same trade the switches have always made, and the only alternative is a second way of saying "these two are the same", which is a worse thing to have to learn.

The drawn **xPyT switch** (`ElementType::Spdt`, named for the SPDT it was before it grew the rest, since the enum is the file format) is a single-pole changeover in one of three actions (`SwitchAction`), chosen in the inspector's Action box: **ON-ON** (two positions, two throws — SPDT), **ON-OFF-ON** (three positions, the middle one connecting nothing) and **ON-ON-ON** (three positions, three throws — SP3T). There is deliberately no poles setting: a double-pole switch is two of these with one label, which gangs them, and that is the one way to say it. Its pins follow the action (`Element::getSwitchPinOffset`, read through `getPinPosition`/`getPinCount`): the common, then the throws left to right, A first, matching the drawings. Where the lever sits is `Element::switchPosition`, 0 to positions − 1, the top being throw A, so the strip shows A on the right whatever the action; `Element::getThrowAt()` is the one statement of which throw each position makes, read by the builder and the artwork lookup alike. `closed` is kept in step (true on throw A) and still written, and a sheet with no `action`/`switchPosition` loads as the ON-ON it was. The engine has no such device and needs none: the builder makes every contact — of every switch, plain ones included — a plain `Switch`, recorded in `LiveControl::contacts` with the positions that close it, and `apply()` opens before it closes so a changeover never makes two throws at once. A control with any three-position switch on it has `positions == 3`, and the strip gives it a three-stop `LeverSwitch` instead of the toggle, unlit on an ON-OFF-ON's middle; its parameter reads 0, 0.5 and 1.

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

- **`CelineUI/ThemeRoles.h`** — the house list, as an X-macro. One entry carries four things
  that have to agree: the identifier the code uses, the label the editor shows, the group
  it is edited under, and the value the design ships with. The enum, the info table, the
  file's keys and the editor's rows are all generated from it. This plugin's own colours
  go in **`source/PluginThemeRoles.h`** — the wires, the parts, the captions, the two
  selection-box rules and the six group box colours — and its accessors in
  **`PluginTheme.h`**, which `Theme.h` includes inside the namespace so
  `Theme::groupBox(2)` reads exactly like `Theme::chrome()`.
- **`CelineUI/ThemePalette.h/.cpp`** — the colours in force, the `.celthm` reader and writer,
  and a `ChangeBroadcaster` so a change reaches every open window. One per process, in a
  function-local static: a palette at namespace scope could be read by a look and feel
  constructed before it.
- **`CelineUI/Theme.h`** — the accessors, each a lookup, each documented with what it is *for*.
  Part of the shared kit; anything this plugin decides for itself, geometry
  included, lives in `PluginTheme.h`.
- **`CelineUI/ThemePanel.h/.cpp`** — the editor. Live: a colour changed there reaches the
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

`ThemeReachTests`, from CelineUI, is what keeps all of this honest: it renders the whole
editor, moves every role, renders it again, and fails if any colour the design ships is
still on screen. A control that snapshots its colours and never asks again is otherwise
a silent, invisible bug.

## The house kit

The shared interface — the theming engine and its editor, `LookAndFeelBase`, `Fonts`,
`EmbeddedAssets`, `IconButton`, `AboutPanel`, `ParameterControl` — is not in this
repository. It is **CelineUI** (github.com/Celine-audio/CelineUI), a submodule at
`modules/CelineUI` shared by AURA, GALLERY and Céline, and it is included by repository:
`#include <CelineUI/Theme.h>`. Its README lists what each plugin has to provide —
`source/ProductInfo.h`, `source/PluginThemeRoles.h`, `source/PluginTheme.h`,
`source/ui/PluginLookAndFeel.h`, and the fonts and marks in `assets/` — and the rules
the kit relies on.

- **Don't edit the kit inside `modules/CelineUI`.** A submodule checks out a commit,
  not a branch, and a change made there belongs to another repository. Edit the
  shared checkout beside the plugins (`../CelineUI`) and build against it with
  `-DCELINE_UI_DIR=../CelineUI`; a kit change is a change to every plugin, so try it
  in all of them before committing it.
- **Commit the kit first, then move this plugin's submodule to that commit.** CI builds
  exactly the commit pinned here, and a pin to a kit commit that isn't pushed breaks
  every CI run and every fresh clone. `git config push.recurseSubmodules check`
  refuses that push.
- The kit's own tests, `ThemeTests` and `ThemeReachTests`, live in CelineUI and run in
  this plugin's suite; `CMakeLists.txt` adds them.
- **JUCE is the house's fork too** (github.com/Celine-audio/JUCE), pinned at `JUCE/`,
  and the same two rules apply: edit the checkout beside the plugins (`../JUCE`), build
  against it with `-DCELINE_JUCE_DIR=../JUCE`, and commit and push the fork before
  moving the submodule. Every change in it is marked `// Celine:`.
- When the kit uses something only a newer fork has, it is **guarded on a macro the
  fork defines** rather than assumed, because each plugin pins JUCE and the kit
  separately: a plugin that moves to the new kit before it moves JUCE must still
  build. `JUCE_CELINE_TOOLTIP_FOR_COMPONENT` is the first such macro.

What is *not* shared is anything a plugin decides for itself: the roles it adds to the
palette, the accessors and geometry in `PluginTheme.h`, its `PluginLookAndFeel` drawing,
and everything under `source/Schematic/`. (The engine lives in its own repository too, but it is Céline's rather than the house's.)

**A tooltip is pinned to its control, not to the pointer**: bottom-left on the control's
top-right corner, or below-right when there is no room above. That is the kit's
`LookAndFeelBase::getTooltipBoundsForComponent()`, and it needs the fork: stock JUCE's
`TooltipWindow` only ever tells a look and feel where the mouse is, and the fork's hands
it the component the tip belongs to. Built against a JUCE without it, tips go back to
the pointer and "A tooltip sits at its control's corner" compiles out.

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

`.github/workflows/release.yml` builds on all three platforms and attaches one zip per platform to the GitHub release, plus a **macOS disk image** and a **Windows installer**. Triggered by pushing a `v*` tag; `workflow_dispatch` produces the same files without making a release.

Every archive holds the Standalone app, VST3, CLAP and LV2, plus AU on macOS (universal). The AAX is built but shipped nowhere: it needs Avid's PACE signature to load. Nothing is code signed, so Gatekeeper and SmartScreen will warn — fine for testers, not for public distribution.

- **The disk image** (`packaging/dmg/settings.py`, built by `dmgbuild` in `macos.yml`) is a drag-and-drop window: one row per format, the bundle on the left and a shortcut to the folder it installs into on the right — `/Applications`, then `/Library/Audio/Plug-Ins/Components`, `VST3`, `CLAP` and `LV2` — joined by a wire drawn on the background. `dmgbuild` writes Finder's layout file itself rather than scripting Finder, which is why it works on a headless runner; it is pinned. Finder draws the icons' names itself, white in dark mode and black in light, so each icon sits on a mid-tone tile both read on. Finder takes the window size as the **whole frame, title bar included** (and its position up from the bottom of the screen): a frame the size of the picture cut its bottom off. Finder's tab, path and status bars are the user's own settings now and show in the DMG's window whatever the DMG asks (all three and the title took 124 points of 740 on a CI-built image), so the layout is compact enough to show every row under all of them, and the picture runs on past the last row, so whatever they leave is plain grid. Finder cannot centre the window or keep it off the Dock or menu bar; the position is chosen to fit a 900-point laptop screen. The plugin folders are system-wide, so Finder asks for a password; `CLAP` and `LV2` do not exist on a Mac until something has installed into them, and a disk image cannot create them, so on a fresh machine those two shortcuts lead nowhere.
- **The installer** (`packaging/installer.iss`, Inno Setup in `windows.yml`) puts the plugins in Common Files' `VST3`, `CLAP` and `LV2` and the app under Program Files with a Start menu entry, each format a component, all ticked. Its license page is `COPYING`, the AGPL; the template's EULA, which forbade redistribution and reading the source, is gone. Its `AppId` must never change: it is how Windows knows a new version replaces an old one.
- **The artwork** for both — the DMG background (with @2x), the volume icon, the wizard images at three scales and the setup icon — is drawn by `packaging/make-artwork.swift` from the logo, the app icon, the Jura fonts and the theme's colours, and committed. Run `swift packaging/make-artwork.swift` from the root after changing any of those. The DMG's rows are written in both that script and `settings.py`, and must agree.

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
