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
