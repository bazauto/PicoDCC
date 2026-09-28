# PicoDCC carrier board

The carrier PCB the firmware runs on: a Raspberry Pi Pico 2 soldered down, two BTS7960 H-bridge
modules (main and programming track), their fault LEDs, and headers for the display and debug.
KiCad project, BOM and the Gerbers as sent to fab.

It was designed in KiCad 9.0.1 in April 2025, before this repo had any design review, and it is
imported as it was built. Nothing in the KiCad files has been changed on the way in. The concerns
found while importing it are listed [below](#known-concerns-with-revision-10) rather than fixed,
because the boards in use are this revision.

## Files

| File | What it is |
|---|---|
| `PicoDCC.kicad_pro`, `.kicad_sch`, `.kicad_pcb` | The KiCad project. Keep them together |
| `ProjectSymbols.kicad_sym` + `sym-lib-table` | The one project symbol, the BTS7960 module. Everything else comes from the stock KiCad libraries |
| `bom.csv` | Bill of materials, as exported from the schematic |
| `fab/rev-1.0/` | Gerbers, drill files, Gerber job file and the zipped set, as plotted on 2025-04-26 for the boards in use. A historical record: never regenerate it in place |
| `LICENSE` | CERN-OHL-P-2.0, covering everything in this folder |

The title block carries no revision, so "1.0" is a name given on import for the only revision
there has been. A board change that goes to fab gets a new `fab/rev-X.Y/` folder.

KiCad plots into `Gerber/` by default, which is git-ignored so that a fresh plot is never mistaken
for the fab record. Opening the project in KiCad 10 and saving it upgrades the file format;
do that deliberately, in its own commit.

## Connections

`A1` is the Pico 2. Pin numbers are the Pico's.

| Pico | Net | Goes to |
|---|---|---|
| GP0–GP15 | one net each | **J1** pins 1–16 (GP*n* on pin *n*+1). J1 carries the display, touch and UART, plus the spare GP12–GP15 |
| GP0, GP1 | UART0 | also **J2** pins 3, 4 |
| GP16 | main fault LED | D1 cathode; anode through R5 (330R) to GND — see concern 1 |
| GP17 | main DCC signal | U1 RPWM, TP1; inverted by Q1 (BC108, R1 base, R2 pull-up to VSYS) into U1 LPWM |
| GP18 | main enable | U1 R_EN and L_EN, TP2 |
| GP19 | prog fault LED | D2 cathode; anode through R6 (330R) to GND |
| GP20 | prog DCC signal | U2 RPWM, TP4; inverted by Q2 (R3, R4) into U2 LPWM |
| GP21 | prog enable | U2 R_EN and L_EN, TP5 |
| GP26 / ADC0 | main current | U1 R_IS and L_IS tied, TP3, R7 (1k) to GND |
| GP27 / ADC1 | prog current | U2 R_IS and L_IS tied, TP6, R8 (1k) to GND |
| SWCLK, SWDIO | debug | **J2** pins 6, 5 |
| VSYS | 5 V in | **J4** pin 2, J2 pin 2, TP7, U1/U2 VCC |
| GND | | J4 pin 1, J2 pin 1, TP8, U1/U2 GND |

Not connected: 3V3, 3V3_EN, RUN, VBUS, ADC_VREF, AGND, GP22, GP28.

**J2 (debug)**: 1 GND, 2 VSYS, 3 GP0, 4 GP1, 5 SWDIO, 6 SWCLK.
**J4 (power)**: 1 GND, 2 VSYS.

## Verification

Run on import with KiCad 10.0.5 (`kicad-cli`), against the files in this folder:

```
sch erc --severity-all                4 violations, all lib_symbol_mismatch (BC108 x2, LED_Small x2)
sch erc --severity-error              0
pcb drc --schematic-parity            0 unconnected items, 0 schematic parity issues
pcb drc, all severities               28: 27 lib_footprint_mismatch, 1 starved_thermal (error)
```

The mismatches are library drift: the design embeds the KiCad 9 copies of stock symbols and
footprints, and KiCad 10's libraries have moved on. They are not connection problems. The
starved thermal is concern 4.

The schematic was last saved in August 2025, four months after the board. The zero parity result
shows the board still matches it.

`fab/rev-1.0/` was checked against the board by replotting the Gerbers and drill files with
`kicad-cli` and comparing what each file draws, using the geometry comparison from
`bazauto/block-detection`'s `scripts/check_fab_outputs.py`. All 12 files match. The only textual
difference is that KiCad 10 numbers the three custom pad-shape aperture macros in a different
order from 9.0.1.

## Known concerns with revision 1.0

**1. The fault LEDs are drawn reverse-biased.** GP16 drives D1's cathode and the anode returns
to GND through R5, so driving GP16 high — what the firmware does on a trip — reverse-biases the
LED. D2 on GP19 is the same. Check how the LEDs were actually fitted before relying on them: if
they light, they are in the other way round from the footprint.

**2. 3V3 is not broken out.** J1 carries GP0–GP15 only; neither J1 nor J2 has a 3V3 pin. Anything
that needs a 3.3 V supply from the board — the block detector used for programming-track ACK, for
one — has to take it from the Pico's 3V3 pin (pin 36) directly.

**3. Current sense resolution, and a possible overvoltage on the ADC pins.** The BTS7960's IS
output is a current proportional to load, at a ratio of roughly 8500:1, turned into a voltage by
the 1k to ground. At that ratio 60 mA — a decoder's service-mode ACK — is about 7 µA, about 7 mV,
about 9 ADC counts, which is below the sense offset the part is specified with at low current.
That is why ADC-based ACK detection never worked, and why ACK detection is moving to the block
detector. Separately, the BTS7960 datasheet describes the IS pin sourcing a fixed current of a few
milliamps when the chip latches a fault; if that is ~4.5 mA, 1k turns it into ~4.5 V on GP26/GP27,
and the RP2350's ADC-capable pins are not 5 V tolerant. **Unverified** — check the datasheet
figure before relying on the ADC pins surviving a driver fault.

**4. One starved thermal relief.** J2 pin 1 (GND) connects to the B.Cu ground zone with one
spoke where the zone asks for two. Harmless electrically; worth fixing in any next revision.

## Licence

Copyright © 2025–2026 Paul Barrett.

The design in this folder — schematic, board, project symbol library, BOM, Gerbers and drill
files — is licensed under the **CERN Open Hardware Licence Version 2 – Permissive**
([`LICENSE`](LICENSE)), the same licence as `bazauto/block-detection`. The firmware in the rest
of the repository stays MIT.

Symbols and footprints from the stock KiCad libraries are embedded in the schematic and board,
as KiCad always does. The KiCad libraries are CC-BY-SA 4.0 with an exception that a design using
them is not bound by that licence, so this folder carries no obligation from them. 3D models are
referenced by path, not included. See [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
