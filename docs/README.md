# PicoDCC Documentation Index

**Last reviewed against the code**: 2026-08-20, at `main`.

Project rules — build commands, safety constraints, conventions, workflow — live in
[`CLAUDE.md`](../CLAUDE.md) at the repository root. This file is the map of everything else.

---

## Current state of `main`

| Subsystem | State | Notes |
|---|---|---|
| DCC signal generation (PIO) | ✅ Working | Main and programming tracks, separate PIO blocks |
| Throttle / function / accessory | ✅ Working | `<t>` accepts the 3-field form only |
| Track power and overcurrent | ✅ Working | Only active where an ADC channel is configured |
| Emergency stop | ✅ Working | Single DCC broadcast, address `0x00` instruction `0x41` |
| Dual-core queue architecture | ✅ Working | Reminders generated on Core 1, self-regulating |
| LCD + touch UI (LVGL) | ✅ Working | Boot sequence, status screen, log viewer, maintenance UI |
| Diagnostic logging | ✅ Working | 30-entry circular buffer, viewable on the LCD |
| Config storage (flash) | ✅ Working | Last 4KB sector, CRC32, preserved across firmware updates |
| Layout Maintenance Mode | ✅ Working | LCD-only entry, power lockout, `<E>` save gated behind it |
| Runtime ACK tuning | ✅ Working | `<D ACK LIMIT\|MIN\|MAX>` adjusts RAM config |
| `<D CONFIG>` / `<D CAL>` commands | ❌ **Not reachable** | Handlers exist but are never wired in — see Known Gaps |
| ACK detection | ❌ Not implemented | Parameters are tunable; the detection logic does not exist |
| CV programming | ❌ Declarations only | Method bodies are absent on `main` |
| Sensors (`<S>`) | ❌ Not supported | Parsed, then rejected as invalid |

**Tests**: 167 across 11 CMocka suites, all passing. CI runs them on every push and PR.

### Known gaps

These are documented rather than quietly carried, because several documents in this folder
describe them as finished:

1. **`PicoDccExConfig` is dead code.** `lib/PicoDCCEX/pico_dccex_config.cpp` implements
   `<D CONFIG GET/SET/SAVE/RESET/EXPORT>` and the `<D CAL ...>` calibration workflow, and it
   compiles into the `PicoDCCEX` library — but the class is never constructed and never
   called. The packet validator accepts only `<D ACK ...>`, so every other `D` subcommand is
   rejected before reaching a handler. This means `docs/implementation-complete-config-storage.md`
   and `docs/calibration-guide.md` describe commands that do not currently work.
2. **CV programming is stubs.** `verifyCV()`, `readCVByte()`, `readCVBit()`, `writeCVBytes()`
   and `writeCVBit()` are declared in `lib/PicoDCCLoco/pico_dccloco.h` with no definitions.
3. **ACK detection is absent** from `PicoDccTrack`, which is the prerequisite for all of the
   above.

### Work in progress: the `programming` branch

Service mode programming is being developed on `origin/programming`, which is 6 commits and
roughly 4,900 added lines ahead of `main`. It contains a `PicoDCCProgrammer` component with
its own test suite (625 lines), PIO changes for programming-track timing, and a `<->` forget-cab
command. Its most recent commit records the blocker plainly: basic programming works, but the
current reading needed for ACK detection is not right yet — which matches
`docs/ack-detection-analysis.md`'s warning that the ADC approach and the sensor circuit both
have to be correct before ACK detection is reliable.

Nothing from that branch is on `main`. Treat the roadmap in
`docs/service-mode-programming-plan.md` as the plan for landing it, not as a record of what
has shipped.

---

## Document index

### Design and reference

| Document | What it is |
|---|---|
| [`architecture.md`](architecture.md) | **Start here.** Component responsibilities, the Core 0/Core 1 split, queue design, operation modes, known gaps |
| [`gpio-pinout-reference.md`](gpio-pinout-reference.md) | Pin assignments for tracks, LEDs, ADC, LCD and touch |
| [`lcd-integration.md`](lcd-integration.md) | Display hardware, LVGL setup and UI structure |
| [`safety-recommendations.md`](safety-recommendations.md) | Overcurrent, emergency stop, programming-track limits, isolation |
| [`DCC Service Mode Programming.html`](DCC%20Service%20Mode%20Programming.html) | Offline copy of the DCC Wiki article on NMRA S-9.2.3 (the web version is Cloudflare-blocked) |

### Protocol compliance

| Document | What it is |
|---|---|
| [`dccex-compliance-analysis.md`](dccex-compliance-analysis.md) | Command-by-command comparison against the DCC-EX specification |
| [`dccex-jmri-compatibility-todos.md`](dccex-jmri-compatibility-todos.md) | What JMRI expects that this firmware does not yet provide, with testing feedback |

The authoritative list of what actually parses is the opcode table in `architecture.md`, which
is derived from `lib/PicoDCCEX/pico_dccexpacket.cpp`. These two documents give the reasoning;
the code gives the truth.

### Service mode programming (planned / in progress)

| Document | What it is |
|---|---|
| [`service-mode-programming-plan.md`](service-mode-programming-plan.md) | The 6-phase roadmap: ACK detection → Direct Mode packets → DCC-EX integration → address programming → bit-level CV ops → legacy modes |
| [`ack-detection-analysis.md`](ack-detection-analysis.md) | The hard part. ADC sampling rates, the free-running 25 kHz approach, pulse analysis, and the sensor-circuit requirements |

### Configuration storage

| Document | What it is |
|---|---|
| [`non-volatile-storage-options.md`](non-volatile-storage-options.md) | Why flash-last-sector was chosen over EEPROM, SPI flash, SD and LittleFS |
| [`implementation-complete-config-storage.md`](implementation-complete-config-storage.md) | The implementation write-up. ⚠️ The `<D CONFIG>` / `<D CAL>` command sets it describes are not wired in — see Known Gaps |
| [`calibration-guide.md`](calibration-guide.md) | The intended ADC-to-mA calibration procedure. ⚠️ Depends on `<D CAL ...>`, which is not reachable on `main` |
| [`firmware-update-config-preservation.md`](firmware-update-config-preservation.md) | How the config sector survives a firmware flash |
| [`linker-script-implementation-summary.md`](linker-script-implementation-summary.md) | Why `memmap_picodcc.ld` exists and what it changes (FLASH shrunk to 4092k) |

### Testing

| Document | What it is |
|---|---|
| [`hardware-test-quick-reference.md`](hardware-test-quick-reference.md) | What to do at the bench, with a Pico attached |
| [`bench-machine-setup.md`](bench-machine-setup.md) | The Linux bench machine: SSH access, attached devices, provisioning, toolchain update policy |
| [`hardware-test-plan-nv-storage.md`](hardware-test-plan-nv-storage.md) | Detailed test plan for the flash storage subsystem |
| [`vscode-test-integration.md`](vscode-test-integration.md) | CTest and TestMate setup in the IDE |
| [`coverage-quick-start.md`](coverage-quick-start.md) | The short version of the gcov/lcov workflow |
| [`coverage-scripts-overview.md`](coverage-scripts-overview.md) | What each of the four `scripts/Generate-*.ps1` scripts does |
| [`test-coverage-report.md`](test-coverage-report.md) | A captured coverage snapshot — a point-in-time artifact, not live data |
| [`diagnostic-log-display-quick-reference.md`](diagnostic-log-display-quick-reference.md) | Using the LCD log viewer, and the ARM alignment lessons from building it |

---

## Build, test and CI

Full commands are in [`CLAUDE.md`](../CLAUDE.md). In short:

- Two presets in `CMakePresets.json`, **each with its own build tree** — `host` builds into
  `build/host`, `pico` into `build/pico`. Nothing to clear, nothing to switch.
- `host`: host GCC + Ninja + CMocka. 11 suites, ~0.5s. `cmake --build --preset host` then
  `ctest --preset host`. On Windows this needs the MSYS2 toolchain ahead of Git's on `PATH`
  (see `CLAUDE.md`), or the compiler fails to load its DLLs and ninja reports a `FAILED:`
  edge carrying no diagnostic.
- `pico`: ARM GCC + Pico SDK. Requires the LVGL submodule
  (`git submodule update --init --depth 1 lib/external/lvgl`) or CMake fails at
  `add_subdirectory` with no useful hint.
- `.github/workflows/ci.yml` runs the test build and `ctest` on every push and PR. It does
  **not** cross-compile, so hardware-mode breakage has to be caught locally.
- `scripts/Validate-DualMode.ps1` covers both modes, resolving the SDK and ARM toolchain from
  `PICO_SDK_PATH` / `PICO_TOOLCHAIN_PATH` or the newest `~/.pico-sdk` install, and exiting
  non-zero on any failure.

## Hardware debugging

The Windows development machine has no Pico attached. Hard faults, multicore races, PIO
timing and ADC problems need the Linux bench machine at `pbarrett@172.18.10.240`, reached
over SSH. See [`bench-machine-setup.md`](bench-machine-setup.md) for access, stable device
names, provisioning and the OpenOCD/GDB recipes.

Deploy and debug are scripted, split by whether they reach the board:

| Command | Touches the board? |
|---|---|
| `pwsh -NoProfile -File scripts/Deploy-Firmware.ps1` | No — build, validate, stage |
| `bash scripts/bench.sh inventory` | No — probe, devices, contention, toolchain |
| `bash scripts/bench.sh dry-run` | No — flash preflight only |
| `bash scripts/bench.sh flash --expect <sha256>` | **Yes** — programs over SWD |
| `bash scripts/bench.sh fault` | **Yes** — halts both cores |
| `bash scripts/bench.sh config` | **Yes** — halts to read flash |
| `bash scripts/bench.sh dccex '<s>'` | **Yes** — sends a command |

Only the first three are on the Claude Code allowlist; the rest prompt every time, which is
the remote counterpart of the rule that `LAYOUT_MAINTENANCE` is entered only from the LCD.
`Deploy-Firmware.ps1` refuses to stage an image whose LOAD segments reach the config sector,
and `flash` reads that sector before and after to prove it survived.

Flashing and debug attaches both need explicit approval with track power confirmed first —
halting the core stops DCC generation, and decoders that lose the signal fall back to DC.

---

## Keeping this file honest

This index is a **map plus a status snapshot**, not a changelog. When something lands:

- Update the *Current state* table and delete the Known Gap it closed. A gap leaves that list
  when the code changes, not when someone hopes it has.
- Add new documents to the index table for their area. Thirteen documents were previously
  missing from this index; that is how they become unfindable.
- Refresh the "Last reviewed against the code" date at the top, and say which commit.
- Reasoning belongs in the topic document. Rules belong in `CLAUDE.md`. This file only points.

## External references

- [DCC-EX command reference](https://dcc-ex.com/reference/software/command-reference.html) —
  the superset this firmware partially implements
- [DCC Wiki](https://dccwiki.com) — NMRA standards, with the service-mode article mirrored here
- [Raspberry Pi Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/)
