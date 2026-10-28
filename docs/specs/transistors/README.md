# Bipolar transistor sources

| File | What it is | Used for |
|---|---|---|
| `2N3904-Fairchild.PDF` | Fairchild 2N3904/MMBT3904/PZT3904, Rev. B0 (2011) | 2N3904 gain and Vbe(on) curves at 25 °C (fitted); BR, VAF and capacitances from the card printed on its 1999 revision |
| `2N3906-Fairchild.PDF` | Fairchild 2N3906/MMBT3906/PZT3906, Rev. 1.2.2 | 2N3906 gain and Vbe(on) curves (fitted); card's BR, VAF and capacitances |
| `2n5088_2520series-1130989.pdf` | Fairchild 2N5088/2N5089 | Their card and minima |
| `BC10x.pdf` | Comset BC107-109 (Philips typicals) | BC108A/B, BC109C gain points |
| `BC550-D.PDF` | onsemi BC546-550 | Grade windows |
| `MPSA18.PDF`, `MPSA13-D.PDF` | Central / onsemi | MPSA18, MPSA13 |
| `nte102-179916.pdf` | NTE102/NTE103 | Every typical on the sheet |
| `2SC2240.pdf` | Toshiba 2SC2240 | GR and BL grades |

## SPICE cards (`spice/`)

| File | What it is |
|---|---|
| `2N3904-2N3906-Fairchild.lib` | The cards printed on Fairchild's sheets. The 2N3904's gives 0.6 times its own sheet's typical gain; the 2N3906's Vbe is 36 mV high against its sheet |
| `MMBT-Diodes.lib` | Diodes Inc's MMBT3904/3906/2222A and MMBTA13 cards: a different maker's die, compared against only |

onsemi's own 2N3904, 2N3906 and BC547B cards were compared against too, but
they are marked as Symmetry licensed software and are not copied here.
