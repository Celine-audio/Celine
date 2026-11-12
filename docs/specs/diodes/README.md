# Diode sources

Every figure in `source/CelineEngine/Components/Diode.h` comes from one of these;
each model's header comment says which figure, from which file. Curves were
digitised from the PDFs' vector paths, not read by eye.

| File | What it is | Used for |
|---|---|---|
| `1N4148-Vishay.PDF` | Vishay 1N4148, doc 81857 (2024) | 1N4148 forward curve (Fig. 2, 25 °C), fitted 1 µA to 20 mA |
| `1N914.PDF` | 1N914 sheet | The 1N914 alias of the 1N4148 |
| `1N5817-Vishay.PDF` | Vishay 1N5817-19, doc 88525 (2020) | 1N5817 forward curve (Fig. 3, 10 mA to 1 A) and capacitance (Fig. 5) |
| `1N5817-Diodes.PDF` | Diodes Inc 1N5817-19, DS23001 (2023) | Second source: reads 0.15 V higher at 1 A |
| `BZX55-Vishay.PDF` | Vishay BZX55 series, doc 85604 (2024) | The Zeners (BZX55C3V9, C4V3, C5V1, C9V1): forward curve (Fig. 6, 25 °C, 1 µA to 10 mA), capacitance at 2 V reverse (Fig. 5, the whole 5-20 V curve; below 5 V its top, 175 pF, where it leaves the chart), breakdown against the table's limits at 5 mA and 1 mA. Fig. 7 (Z-current) is not used: its voltage axis is mislabelled |
| `WP7113ID-Kingbright.PDF` | Kingbright WP7113ID, GaAsP red | Red LED forward curve |
| `WP7113GD-Kingbright.PDF` | Kingbright WP7113GD, GaP green | Green LED forward curve |
| `WP7113QBC-Kingbright.PDF` | Kingbright WP7113QBC/D, InGaN blue | Blue LED forward curve |
| `BAT41.PDF`, `BAT46.PDF` | General Semiconductor | BAT41 / BAT46 limits |
| `BAT46-ST.PDF` | ST BAT46 | BAT46 forward curves |

`../1N4001.PDF` (Diodes Inc) and `../BAT41.PDF` (ST) sit one level up.

## SPICE cards (`spice/`)

What the models were compared against in ngspice-47.

| File | What it is |
|---|---|
| `1N4148-Nexperia.lib` | Nexperia's own card. Within 3 mV of Vishay's curve to 10 mA, 56 mV high at 100 mA; its capacitance and transit time are the ones used |
| `1N5817-Diodes.lib` | Diodes Inc's card. Fitted at amps: 36 mV at 1 mA, useless at pedal currents |
| `BZT52-Diodes.lib` | Diodes Inc's BZT52C5V1 and C9V1, the SOD-123 Zeners: softer than the BZX55 limits. Its forward transit time, 50.1 ns, is the Zeners' (the BZX55 sheet gives none) |

## Not here, and why

- **1N34A:** no maker publishes a curve, only limits (Microsemi, MCC), so the
  model is a typical germanium diode that sits inside those limits.
- **onsemi's own cards** for the 1N4148 and 1N5817 are marked as Symmetry
  licensed software. They were only compared against, not copied.
