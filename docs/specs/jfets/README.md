# JFET sources

| File | What it is | Used for |
|---|---|---|
| `InterFET-spice-models.xlsx` | InterFET's SPICE models, measured on their own parts (interfet.com) | J201, 2N5457, 2N5485 (-L grade), 2N5460 (-H grade): VTO, BETA, LAMBDA, CGS, CGD and IS, verbatim |
| `InterFET-CTC-023.PDF` | InterFET CTC-023, how they derive those models | Method |
| `J201-Central.PDF` | Central Semiconductor J201 | J201 limits (VGS(off) −0.3 to −1.5 V, Idss 0.2 to 1 mA) |
| `Siliconix-1983-excerpt.PDF` | Pages from Siliconix's 1983 Small Signal FET Design Catalog: 2N5457-9, 2N5484-6, J201-2 and the NRL/NH process curves | Cross-check of the Idss–pinch-off relation |

The 2N5952's sheet (Fairchild, limits only) is in `../unused/`.

## SPICE cards (`spice/`)

| File | What it is |
|---|---|
| `InterFET.lib` | InterFET's models as SPICE text, the ones Céline uses |
| `Fairchild-onsemi.lib` | Fairchild's J201, 2N5460 and 1N914 cards as onsemi distributes them. **The J201 card is wrong**: a −3.95 V pinch-off and 25 mA of Idss, outside the J201's own limits |
