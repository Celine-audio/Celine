<img alt="Céline logo" src="/assets/icons/logo.svg" title="Céline logo" width="250"/>

Céline is an audio-centered circuit sandbox to design, emulate, and play music through circuits in real time, replicating the sound and feel of the drawn schematic.

This is achieved live, with no pre-computation or curve-fitted approximation. The engine generates the corresponding system of equations from the drawn schematic and solves it every sample.

Céline comes pre-bundled with a library of parts required to build most effects pedals. The non-linear devices like the valves, diodes, and transistors are fitted to published models and manufacturer datasheets.

<img alt="Céline logo" src="/docs/screenshots/interface.png" title="Céline logo" width="1000"/>


---

## What it does

Céline lets you experiment, design, and play with circuits with no latency.

- **Draw your own circuit :** Place parts, wire them and then press _Rebuild_ to load the schematic. The circuit is sent to the solver and is ready to be played through.
- **Potentiometers and switches :** Every potentiometer and switch you draw becomes a knob in the strip along the bottom. They are host-automatable and can be adjusted without needing a rebuild.
- **Oversampling.** 2× or 4× oversampling is available. Lessens the effects of aliasing but computationally intensive.
- **Schematics.** `.celsch` holds the drawing and the knob positions. They can be loaded, imported to a current drawing, and exported directly from the editor. A preset folder can be assigned for easy access to a library of schematics.

The simulation's limits are documented. Read
[`source/CelineEngine/LIMITATIONS.md`](source/CelineEngine/LIMITATIONS.md)
to learn more about the current known limitations of the engine.

---

## Available components

**Terminals** — Ground, Input, Output, and Node. The input and output are where the audio will be sent and where it will be probed. They are purely sending audio and don't represent any load or have any bundled electrical component. Nodes carrying the same label are the same point in the circuit however far apart they are drawn.

**Passives** — Resistor, Capacitor, Inductor, and Transformers.

**Potentiometers** — three tapers: linear, logarithmic, and reverse logarithmic.
Giving potentiometers the same name will link them. They keep their own resistance and taper: what is shared is the shaft (knob position).

**Switches** — xPST and xPDT. The same naming rule applies, multiple switches with the same name will become linked together and act as one switch.

**Semiconductors** — diodes, transistors (BJTs and JFETs) and op-amps.

**Valves** — triodes, pentodes and rectifiers, modelled after **Norman Koren**'s work on [triode SPICE emulation](https://www.normankoren.com/Audio/index.html), plus three triodes using the **Dempwolf–Zölzer** equations from "*[A Physically-Motivated Triode Model for Circuit Simulations](https://dafx.de/paper-archive/2011/Papers/76_e.pdf)*".
Valves also model **interelectrode capacitance** the "Miller effect". It is a real internal action that changes the effective capacitance between grid and anode and so changes the frequency response under gain. It is on by default and can be switched off for CPU savings.

**Annotation** — Text notes and coloured boxes. Purely cosmetic, they make circuits easier to read.

---

## Keyboard Shortcuts

| |                                                                     |
|---|---------------------------------------------------------------------|
| `S` `W` `X` | Select, Wire, Delete                                                |
| `F` | Fit the drawing to the window                                       |
| `R` `C` `L` `T` `D` `G` `B` | Arm a resistor, capacitor, inductor, transistor, diode, ground, box |
| `Cmd`/`Ctrl` + `R` / `F` | Rotate / flip the highlighted part.                                 |

Right-drag to pans.
Mouse wheel to zoom.
Middle-click to clone.

---

## Formats

Built as **VST3®**, **AU** (macOS), **LV2**, **CLAP** and **Standalone**, on Windows, macOS, and Linux.

The Windows standalone supports **ASIO®** alongside WASAPI and DirectSound.

Nothing is code-signed, so Gatekeeper and SmartScreen will warn on first run.

<p>
  <img alt="ASIO Compatible. ASIO is a registered trademark of Steinberg Media Technologies GmbH"
       src="docs/logos/ASIO.png" height="78">
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/VST.png">
    <img alt="VST Compatible. VST is a registered trademark of Steinberg Media Technologies GmbH" src="docs/logos/VST_2.png" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/AU-onwhite.svg">
    <img alt="Audio Units" src="docs/logos/AU.svg" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/CLAP-white.png">
    <img alt="CLAP" src="docs/logos/CLAP.svg" height="70">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/lv2_white.svg">
    <img alt="LV2" src="docs/logos/lv2_black.svg" height="52">
  </picture>
</p>

---

## Built on

[JUCE](https://juce.com) 9, with the build system derived from
[Pamplejuce](https://github.com/sudara/pamplejuce). CLAP support comes from
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions).

---

## Building

Needs **CMake 3.25** or newer and a **C++23** compiler.

The submodules are not optional — JUCE, the shared CMake modules and the CLAP
wrapper all live in them, and one has submodules of its own:

```bash
git clone --recursive https://github.com/Celine-audio/Celine.git
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Then:

```bash
cmake -B Builds -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
```

Add `-G Ninja` if you have it. On macOS, `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
builds a universal binary — keep the quotes, or the shell eats the semicolon and
you get one architecture.

To run the tests:

```bash
ctest --test-dir Builds --output-on-failure
```

---

## Disclaimer

This software is provided "as is", without warranty of any kind. No liability can be claimed for any harm or damage caused by its use.

While this tool aims to faithfully emulate analogue circuits to the best of its abilities, Céline makes no guarantee regarding the accuracy or fidelity of its simulations.

**The audio engine can produce extremely loud output.** Take care when designing and testing and protect your equipment and your hearing.

---

## Licence and credits

Céline being free open-source software using the [JUCE](https://juce.com) framework, and using its free licence, it inherits its AGPLv3 terms.
Céline is then under the [GNU AGPL v3](COPYING) licence. The full notices are in
[`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES`](THIRD-PARTY-NOTICES), and the same summary is available within the plugin under **Settings → About**.

<p>
  <img alt="Licensed under the GNU AGPL v3" src="docs/logos/AGPLv3.svg" height="62">
</p>

### What that means in practice

Using Céline costs nothing and obliges nothing. The licence governs the distribution *of the software*.
The audio you record through it and the circuits you draw are your own work.
You are able to fork this repo and modify it, provided you do so under the AGPLv3 licence and respect its conditions.

### Credits

- Icons from [Font Awesome Free](https://fontawesome.com), used under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
- Typeface [Jura](https://github.com/ossobuffo/jura), by Daniel Johnson with
  Alexei Vanyashin and Mirko Velimirovic
- Typeface [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono),
  © 2020 The JetBrains Mono Project Authors, under the SIL OFL 1.1
- VST® and ASIO® are registered trademarks of Steinberg Media Technologies GmbH
- ASIO Interface Technology by Steinberg Media Technologies GmbH. The
  [ASIO SDK](https://www.steinberg.net/developers/asiosdk-open/) is dual-licensed
  under Steinberg’s own licence or the GPLv3; Céline takes the GPL option, which
  is what lets an ASIO-enabled build stay AGPLv3
- Valve models after [Norman Koren](https://www.normankoren.com/Audio/index.html)
  and after Dempwolf & Zölzer,
  "[A physically-motivated triode model for circuit simulations](https://dafx.de/paper-archive/2011/Papers/76_e.pdf)"
  (DAFx-11)

### AI disclosure
Céline contains no AI whatsoever. However, AI assistants were used alongside the authors during development; Céline remains the authors' work.