# Third-party notices

PicoDCC itself is released under the MIT Licence — see [`LICENSE`](LICENSE). That licence
covers the first-party firmware only: `src/`, `lib/` excluding `lib/external/`, `test/`,
`scripts/`, `cmake/generate_version.cmake` and the documentation.

The components below are **not** covered by it. Each is redistributed or consumed under its
own licence, reproduced in the location named.

| Component | Used as | Licence | Notice location |
|---|---|---|---|
| [LVGL](https://github.com/lvgl/lvgl) | Git submodule at `lib/external/lvgl`, linked into the firmware | MIT | `lib/external/lvgl/LICENCE.txt` |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | External dependency; `pico_sdk_import.cmake` is a verbatim copy from the SDK | BSD-3-Clause | Header comment in `pico_sdk_import.cmake` |
| [cmocka](https://cmocka.org/) CMake modules | Vendored in `cmake/cmocka/` | BSD (2-clause style) | `cmake/cmocka/COPYING-CMAKE-SCRIPTS` |
| [cmocka](https://cmocka.org/) library | Host test builds only; found via `find_package`, never redistributed | Apache-2.0 | Upstream |

## Notes

**LVGL is a submodule, not vendored.** Cloning without `--recursive` leaves
`lib/external/lvgl` empty and the hardware build fails at `add_subdirectory`. Nothing under
`lib/external/` is first-party code.

**Binary distribution has an extra obligation.** No release artefacts are published today. If
a `.uf2` is ever attached to a release, it links LVGL (MIT) and the Pico SDK (BSD-3-Clause),
and both licences require their notices accompany the binary — so ship this file alongside it.

**The DCC-EX protocol is implemented, not copied.** PicoDCC speaks a partial DCC-EX protocol
so JMRI can talk to it. No code originates from the upstream CommandStation-EX project, which
is GPLv3; the protocol is reimplemented from its documented wire format. Protocol
compatibility carries no licensing obligation, and none of the GPL's terms apply here. Keep it
that way: do not paste code from CommandStation-EX into this tree.

## The DCC protocol itself

Implementing DCC needs no licence. This was checked rather than assumed, because "is DCC
patented?" is a reasonable thing to wonder and the answer is not obvious from the outside.

**The core standards carry no patent clause.** [S-9.2][s92] (the packet format this firmware
generates) and [S-9.2.1][s921] (extended packet formats) contain no patent or sub-licence
language at all — only a warranty disclaimer and a copyright notice on the document. The
original Lenz DCC patents from the late 1980s expired long ago.

**The one patent clause is in [S-9.3.2][s932], RailCom**, which is a translated Lenz document.
It grants a no-cost sub-licence for personal non-commercial use, and requires an NMRA
Conformance & Inspection Warrant for commercial products. That clause is now moot — every
patent it names has expired: EP 1 380 326 B1 (July 2022), US 6,853,312 (lapsed February 2017),
US 6,539,292 (June 2021) and US 6,494,410 (~March 2020). PicoDCC does not implement RailCom
in any case.

**Trademarks are the live constraint, and they apply regardless of commercial intent:**

- The **DCC logo** is an NMRA trademark. Do not put it on the project, the README or the LCD.
- **"NMRA Conformance Seal" / "Conformance Warrant"** means hardware submitted to the NMRA for
  testing. Do not claim conformance or certification without one.
- **"RailCom" is a registered trademark of Lenz** (DE 301 16 303, US Reg. 2,746,080). An
  expired patent does not free the name. If cutout detection is ever added, describe what it
  does rather than branding it RailCom.

Descriptive factual statements — "implements the NMRA DCC protocol per S-9.2",
"DCC-compatible" — are nominative use and need no permission. That is what this repo does.

**Do not commit copies of the standards.** The NMRA standards documents are copyrighted and
the NMRA states explicitly that publishing them for adoption waives nothing. Cite them by
number and link them, as below. This is the same reason a saved copy of a DCC Wiki article was
removed from `docs/`; replacing it with the NMRA PDFs would reintroduce the identical problem.

[s92]: https://www.nmra.org/sites/default/files/s-92-2004-07.pdf
[s921]: https://www.nmra.org/sites/default/files/standards/sandrp/DCC/S/s-9.2.1_dcc_extended_packet_formats.pdf
[s932]: https://www.nmra.org/sites/default/files/s-9.3.2_2012_12_10.pdf
