# PicoDCC

DCC command station firmware for the Raspberry Pi Pico 2 (RP2350, `PICO_BOARD=pico2`).
Speaks a **partial** DCC-EX protocol to JMRI and other throttles over UART, generates DCC
track signal via PIO, and drives a Waveshare ST7789T3 LCD through LVGL.

This is embedded firmware that puts current on rails. A bug here is a locomotive moving
when nobody asked it to. Weight decisions accordingly.

---

## Build and test

Two mutually exclusive modes selected by the `TEST_BUILD` CMake flag. **Both share the
`build/` directory**, so switching modes needs the cache cleared first — that is the single
most common way to lose an hour here.

```bash
# Test mode — host compiler + Ninja + CMocka. This is what you run for almost everything.
cmake -B build -G Ninja -DTEST_BUILD=ON
cmake --build build
cd build && ctest --output-on-failure     # 9 suites, 113 tests, ~0.5s

# Hardware mode — ARM GCC cross-build, produces build/src/PicoDCC.uf2
# Needs PICO_SDK_PATH and PICO_TOOLCHAIN_PATH (VS Code sets both; see .vscode/settings.json)
rm -f build/CMakeCache.txt && rm -rf build/CMakeFiles     # REQUIRED when switching modes
cmake -B build -G Ninja -DTEST_BUILD=OFF
cmake --build build
```

`scripts/Validate-DualMode.ps1` runs both in sequence. Its hardware branch hardcodes a
stale Pico SDK v1.5.1 toolchain path and will warn-and-skip on this machine; prefer the
commands above.

The hardware build **requires the LVGL submodule**. If `lib/external/lvgl` is empty, CMake
fails at `add_subdirectory` with no useful hint:

```bash
git submodule update --init --depth 1 lib/external/lvgl
```

Firmware is linked with `memmap_picodcc.ld`, which shrinks FLASH to 2044k so the last 4KB
sector survives a firmware update — that sector is `PicoConfigStorage`. Never link with the
SDK default script.

CI (`.github/workflows/ci.yml`) builds test mode and runs ctest on every push and PR. It
does not cross-build firmware, so **hardware-mode breakage is not caught by CI** — run the
hardware build locally before merging anything that touches shared headers, `lib/*/CMakeLists.txt`,
or code behind `#ifdef TEST_BUILD`.

---

## Non-negotiable rules

### 1. Flash writes are a safety event

A flash write blocks **both cores for ~410ms**. DCC packets stop. Decoders that lose the
signal fall back to DC mode — and if the track is powered, DC mode means **full speed**.

Flash writes are therefore only legal in `OperationMode::LAYOUT_MAINTENANCE`, which
`PicoDCCController` will only enter when the main track is unpowered, and which can only be
entered from the LCD (physical presence — never remotely). Do not add a flash write anywhere
else, do not add a timeout-based exit, and do not auto-restore track power on exit.

### 2. Never pollute the DCC-EX UART with diagnostics

`DCCEX_RESPONSE()` is **only** for genuine DCC-EX protocol replies to a client command.
Errors, warnings, traces and status go to the diagnostic log (`lib/pico_diagnostic.h`):
`LOG_CRITICAL` / `LOG_ERROR` / `LOG_WARNING` / `LOG_INFO`. Logs land in a 30-entry circular
buffer (~2KB RAM) and are viewable on the LCD. A stray diagnostic on the command UART
desynchronises JMRI.

### 3. `#ifdef TEST_BUILD` is for hardware abstraction only

Mocks vs. real peripherals, nothing else. Never branch business logic, error handling,
validation or diagnostic text on it. If a test needs different behaviour, the design is
wrong — inject the dependency instead (see `PicoDCCDisplay`, which takes `LcdDriver` and
`LvglRenderer` by reference).

### 4. ARM Cortex-M33 memory safety

These are not style preferences; each one has already caused a hard fault on hardware.

- **Never `strncpy()`** — it causes UNALIGNED faults. Copy byte by byte.
- **Never assign structs** (`a = b`) across cores — use `memcpy()`.
- **No large stack allocations**, especially in multicore or frequently-called paths. Use
  `static char buf[N] __attribute__((aligned(8)))`.
- **`sem_try_acquire()`, not blocking acquire**, for Core 0 display reads — blocking Core 1
  corrupts DCC timing.
- Cap iteration and buffer sizes (e.g. 20 log entries, 2KB display buffer).

### 5. Shared state needs the semaphore, always

`PicoDccLocos` is written by Core 0 and read by Core 1. Acquire the semaphore **before any**
`std::vector` operation — including `size()` and `empty()`. Checking container state outside
the lock is the race, not just mutating it.

### 6. `main()` stays trivial

`src/pico_dcc.cpp` holds construction plus a 3-line loop. Components own their own timing,
data gathering and update logic (`init()`, `runBootSequence()`, `loop(controller)`). No timer
variables, no cross-component queries, no calculations in `main()`.

---

## Architecture in one screen

**Core 0** — `PicoDCCEX` parses commands off UART, `PicoDCCController` orchestrates and owns
the main command queue (explicit commands, repeat logic) and the operation-mode state machine.

**Core 1** — `PicoDCCTrack::loop()` drives the PIO state machine, monitors current via ADC,
and **generates locomotive reminders** when the single-buffered hardware queue is empty. This
is self-regulating by design: reminders are hardware-paced, so the queue cannot overflow.
Priority is explicit commands > reminders > idle packets.

| Component | Lives in | Owns |
|---|---|---|
| `PicoDCCController` | `lib/PicoDCCController/` | Orchestration, main queue, `OperationMode` |
| `PicoDCCEX` | `lib/PicoDCCEX/` | DCC-EX command parsing and config commands |
| `PicoDCCLoco` / `PicoDccLocos` | `lib/PicoDCCLoco/` | Loco state; semaphore-protected collection |
| `PicoDCCTrack` | `lib/PicoDCCTrack/` | PIO transmission, hardware queue, current monitoring |
| `PicoDCCDisplay` | `lib/PicoDCCDisplay/` | All LCD logic — self-contained, DI'd drivers |
| `PicoConfigStorage` | `lib/PicoConfigStorage/` | Flash config, CRC32, runtime/persistent split |
| `PicoDiagnostic` | `lib/PicoDiagnostic/` | Circular log buffer |

Full detail in `docs/architecture.md`.

---

## Transport facts (easy to get wrong)

- **DCC-EX commands arrive on raw `uart0`**, initialised by `setup_default_uart()` in
  `PicoDCCEX::init()` — GPIO 0/1, 115200.
- **`printf()` goes to USB CDC, not the UART** — `src/CMakeLists.txt` sets
  `pico_enable_stdio_usb(1)` / `pico_enable_stdio_uart(0)`. USB output is for bring-up
  only; it is not the protocol channel.
- Throttle commands accept **only the 3-field `<t cab speed dir>` form**.
- Emergency stop is a **single DCC broadcast** (address `0x00`, instruction `0x41`) that
  clears the main queue, hardware queue and all loco state — not per-loco commands.
- Command coverage is partial. Read `lib/PicoDCCEX/` for what is actually implemented;
  never assume behaviour from the upstream DCC-EX docs.

---

## Conventions

- `snake_case` for files and functions, `CamelCase` for classes.
- Tests are CMocka, one suite per component in `test/`, registered in `test/CMakeLists.txt`.
  Shared mocks in `test/mocks.cpp`; `pico_dcc_display_tests` and `pico_diagnostic_tests`
  deliberately link a reduced set (see the `if`/`elseif` in `test/CMakeLists.txt`).
- Adding a test file means adding it to `TEST_TARGETS` — it will not be picked up otherwise.
- Update the test count in `docs/architecture.md` and this file when suites change.

---

## Hardware debugging

Windows dev machine has **no Pico attached** — test mode and code only. Hard faults,
multicore races, PIO timing and ADC problems need the **Linux hardware test machine** with
OpenOCD on telnet port 50002:

```bash
echo -e "targets rp2350.cm1\nreg" | nc localhost 50002 -q 1
```

If a problem needs that, say so and stop rather than guessing from the Windows side.

---

## Workflow

- **Everything reaches `main` through a PR.** Branch from `origin/main`, never fast-forward
  onto `main` locally. Rebase onto main; never merge main into a branch.
- **Docs move with the code, in the same PR.** If a change falsifies `docs/architecture.md`,
  `docs/README.md` or this file, fix it in the same branch — never as a follow-up.
- Pass long PR bodies to `gh` via `--body-file`, never a here-string.
- Run the test suite before reporting anything complete. Run the hardware build too if the
  change could affect it.

---

## Documentation map

`docs/README.md` is the index, and carries a **current-state table and a known-gaps list** —
read it before assuming a feature works. High-value entries:

- `docs/architecture.md` — component responsibilities, dual-core design, operation modes, and
  the authoritative table of which DCC-EX opcodes actually parse
- `docs/service-mode-programming-plan.md` — CV programming roadmap (the `programming` branch)
- `docs/dccex-compliance-analysis.md` + `docs/dccex-jmri-compatibility-todos.md` — what
  JMRI expects vs. what is implemented
- `docs/safety-recommendations.md` — safety analysis
- `docs/gpio-pinout-reference.md` — pin assignments
- `docs/coverage-quick-start.md` — gcov/lcov workflow via `scripts/Generate-*.ps1`
- `docs/hardware-test-quick-reference.md` — what to do at the bench

## Things that are in the tree but do not work

Do not mistake these for working features, and do not plan against them:

- **`PicoDccExConfig` is dead code.** `<D CONFIG ...>` and `<D CAL ...>` are implemented in
  `lib/PicoDCCEX/pico_dccex_config.cpp` but the class is never constructed. The packet
  validator accepts only `<D ACK ...>`, so every other `D` subcommand is rejected.
- **CV programming methods are declarations only** — no bodies in `pico_dccloco.cpp`.
- **ACK detection does not exist** in `PicoDccTrack`. Its parameters are tunable and
  persistable, which makes it look implemented; it is not.

Service mode programming is being built on the `programming` branch, not on `main`.
