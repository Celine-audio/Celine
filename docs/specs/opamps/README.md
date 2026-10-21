# Op-amp sources

Every figure in `source/CelineEngine/Components/OpAmp.h` comes from one of these.
The header comment of each model says which figure, from which file.

| File | What it is | Used for |
|---|---|---|
| `TL071.PDF` | TI SLOS080J (2005), TL07x | TL072 gain, bandwidth, slew rate, bias current; swing vs supply (Fig. 10); power bandwidth at ±5/±10/±15 V (Figs. 5–6) |
| `TL07x-TI-revW.PDF` | TI SLOS080W (2025), TL07x incl. the new TL07xH die | Legacy common-mode range and offset vs VCM (Fig. 5-57) |
| `TL072-Diodes.PDF` | Diodes Inc. TL072 second source (obsolete) | Cross-check of swing at ±5 V |
| `JRC4558.PDF` | ArtChip's sheet for a 4558 clone | Output resistance (75 Ω), follower overshoot behind the 60° phase margin |
| `NJM4558.PDF` | Nisshinbo (NJR) NJM4558, Ver. 20260723 — the original maker | JRC4558 gain, bandwidth, slew rate, input resistance, bias current, PNP inputs, swing vs supply |
| `NE5532.PDF` | TI SLOS075H (2004) | NE5532 swing into 600 Ω, output impedance, follower overshoot |
| `NE5532-TI-revK.PDF` | TI SLOS075K (2025) | NE5532 gain, bandwidth, slew rate, input resistance, bias current |
| `NE5532-Philips.PDF` | Philips product specification (1997) | NE5532 swing at 600 Ω and 2 kΩ, ±15 and ±18 V; common-mode range vs supply |
| `NE5532-onsemi.PDF` | onsemi NE5532 | Common-mode range vs supply (Fig. 7), cross-check |
| `LM108-N.PDF` | TI LM108-N (2013) | LM308 bandwidth, slew rate, phase, swing vs output current |
| `LM308-National.PDF` | National LM108/LM208/LM308 (1994) | LM308 typical bias current (1.5 nA), swing |
| `LM741.PDF` | TI SNOSC25D, LM741 | LM741 gain, input resistance, swing, bias current |
| `UA741-TI.PDF` | TI SLOS094H (2026), µA741 | Open-loop gain vs supply (Fig. 5-6) |
| `MC4741C-Motorola.PDF` | Motorola MC4741C, a quad MC1741 | 741 swing on each side vs supply (Figs. 3–4), gain vs supply (Fig. 8) |

## SPICE cards (`spice/`)

The references the tests in `tests/CelineEngine.cpp` were run against, in ngspice-47.

| File | What it is |
|---|---|
| `TL072-TI.lib` | TI's TL072 Boyle macromodel |
| `UA741-TI.lib` | TI/National's µA741 Boyle macromodel |
| `NE5534-TI-SLOJ049.lib` | TI's NE5534 macromodel (SLOJ049). Uncompensated; with 22 pF across pins 6–7 it stands in for the NE5532, which has no card of its own |

Not here, and why: NJR's NJM4558 model is encrypted and behind a registration,
and no LM308 card is published. Analog Devices' LM108A/LM308A sheet may plot
LM308 swing against supply, but analog.com could not be reached when this was
assembled.
