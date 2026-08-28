# PicoDCC

DCC command station firmware for the Raspberry Pi Pico 2 (RP2350, `PICO_BOARD=pico2`).
Speaks a **partial** DCC-EX protocol to JMRI and other throttles over UART, generates DCC
track signal via PIO, and drives a Waveshare ST7789T3 LCD through LVGL.

This is embedded firmware that puts current on rails. A bug here is a locomotive moving
when nobody asked it to. Weight decisions accordingly.

---

## Build and test

Two mutually exclusive modes, each with **its own build tree**, defined by `CMakePresets.json`:
`host` builds into `build/host`, `pico` into `build/pico`. They are independent — no cache
clearing, no mode to restore, and both stay warm instead of each full rebuild costing the other.
Always drive the build through a preset; a bare `cmake -B build` re-creates the shared tree this
project deliberately got rid of (issue #25).

```bash
# host — host compiler + Ninja + CMocka. This is what you run for almost everything.
cmake --preset host
cmake --build --preset host
ctest --preset host                       # 11 suites, ~0.5s

# pico — ARM GCC cross-build, produces build/pico/src/PicoDCC.uf2
# Needs PICO_SDK_PATH and PICO_TOOLCHAIN_PATH (VS Code sets both; see .vscode/settings.json).
# Local install is Pico SDK 2.2.0 + ARM GCC 14.2.Rel1, both under ~/.pico-sdk.
cmake --preset pico
cmake --build --preset pico
```

**On Windows, the host build needs `C:\msys64\ucrt64\bin` at the FRONT of PATH — in every
shell, PowerShell included.** Ninja reports `FAILED:` with **no compiler diagnostic at all**,
which looks unreal and is the signature of this and nothing else.

The cause is DLL shadowing. `cc1plus.exe` lives in `ucrt64/lib/gcc/x86_64-w64-mingw32/<ver>/`,
**not** in `ucrt64/bin`, so the loader's "application directory first" rule does not protect
it: it falls through to PATH and picks up whichever `zlib1.dll`, `libgmp-10.dll`,
`libmpfr-6.dll`, `libisl-23.dll` or `libwinpthread-1.dll` it finds first. An older copy gives
`0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND` before the compiler prints anything.

Any directory shipping those DLLs will do it. Observed culprits on this machine: Git Bash's
`mingw64/bin`, `C:\Strawberry\c\bin` (Strawberry Perl) and `E:\win-build\bin`. This file
used to blame Git Bash alone and say "the same build from PowerShell succeeds" — that was true
of one culprit and is not true in general. PowerShell is not safe by virtue of being PowerShell.

Diagnosing it: `c++ --version` **succeeds** even when the toolchain is broken, because it never
loads `cc1plus`. Compile something instead, or run `cc1plus.exe --version` directly and look at
the exit code. To find the shadowing copy:

```powershell
foreach ($n in 'zlib1.dll','libgmp-10.dll','libmpfr-6.dll','libisl-23.dll') {
    "$n -> $((Get-Command $n -ErrorAction SilentlyContinue).Source)"
}
```

Anything outside `C:\msys64\ucrt64\bin` is the problem. The Stop hook
(`.claude/hooks/verify-build.sh`) prepends the toolchain for itself and
`PICODCC_HOST_TOOLCHAIN_BIN` overrides the location, but an interactive build has to do it by
hand. A machine-local `CMakeUserPresets.json` (gitignored) is the place for any other
environment fix.

`scripts/Validate-DualMode.ps1` runs both presets in sequence and is the quickest way to check
a change end to end. It resolves the SDK and ARM toolchain from `PICO_SDK_PATH` /
`PICO_TOOLCHAIN_PATH`, falling back to the newest install under `~/.pico-sdk`, and exits
non-zero if anything fails. It needs Ninja for both modes, and leaves both trees configured.

Both modes generate `generated/version.h` inside their own tree from `cmake/generate_version.cmake` — build
date plus `git rev-parse --short HEAD`, with a trailing `+` when the tree is dirty. That
string *is* the DCC-EX identity (`PICODCC_IDENTITY` in `lib/dccex_communication.h`), so the
`<s>` reply names the commit the running image was built from. Generation happens at configure
time and again on every build; it deliberately carries no time-of-day field, so an unchanged
commit leaves the header untouched and nothing recompiles.

The hardware build **requires the LVGL submodule**. If `lib/external/lvgl` is empty, CMake
fails at `add_subdirectory` with no useful hint:

```bash
git submodule update --init --depth 1 lib/external/lvgl
```

Firmware is linked with `memmap_picodcc.ld`, which shrinks FLASH to 4092k so the last 4KB
sector survives a firmware update — that sector is `PicoConfigStorage`. Never link with the
SDK default script.

CI (`.github/workflows/ci.yml`) runs the `host` preset and its ctest on every push and PR —
the same commands you run locally, so the two cannot drift. It does not cross-build firmware,
so **hardware-mode breakage is not caught by CI** — run the `pico` preset locally before
merging anything that touches shared headers, `lib/*/CMakeLists.txt`, `PicoDCCDisplay`, or
code behind `#ifdef TEST_BUILD`. It builds into its own tree, so nothing needs clearing or
restoring afterwards.

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

**Everything is constructed inside `main()`, after `diag_log_init()`**, and reached from
Core 1 through the `pico_controller` pointer. `PicoDccController` used to be a file-scope
static, so its constructor ran during dynamic initialisation — before `main()`, where
`log_diagnostic()` returns early because the buffer is not initialised yet, and where
`diag_log_init()` would then `memset` the entries anyway. Its boot banner and
`PicoConfigStorage::load()`'s diagnostic were dropped on every boot (#46). Nothing else on
the happy path logs, so an empty diagnostic screen was the normal state and could not be
told apart from a broken log pipeline. Do not move construction back to file scope.

### 7. `dcc_millis()` is the only millisecond clock

`lib/dcc_time.h`. **Never write `time_us_32() / 1000`.** Unsigned delta arithmetic
(`now - then`) survives counter wrap only when the counter wraps at 2^32. `time_us_32()`
does; dividing first produces a value that wraps at 4,294,967 — every **71.6 minutes** of
uptime — and across that boundary every timeout in the firmware fires at once. That was
issue #32, and it was seen in operation: both tracks powered off with "DCC timing violation
detected", the error LED clearing on the next pass while the power stayed off.

`dcc_millis()` is `to_ms_since_boot(get_absolute_time())` — a latched read of the 64-bit
hardware timer, equally multicore-safe, wrapping at 49.7 days with correct deltas across
that wrap. Compare differences, never absolute stored timestamps.

---

## Architecture in one screen

**Core 0** — `PicoDCCEX` parses commands off UART, `PicoDCCController` orchestrates and owns
the main command queue (explicit commands, repeat logic) and the operation-mode state machine.
It is also the **only** core that writes to the DCC-EX UART: a fault on Core 1 sets a latch and
Core 0 turns it into `<p0 MAIN>` / `<p0 PROG>` (#4, #42), because a blocking `uart_puts` in the
DCC hot path is the very stall the timing monitor exists to catch.

**Core 1** — `PicoDCCTrack::loop()` drives the PIO state machine, monitors current via ADC,
and **generates locomotive reminders** when the single-buffered hardware queue is empty. This
is self-regulating by design: reminders are hardware-paced, so the queue cannot overflow.

Priority is explicit commands > reminders > idle packets.

**The main track is the pacer, and only the main track may block.** Core 1 services both
tracks in one pass; the main track's `pio_sm_put_blocking` is what stops that pass outrunning
the hardware. The programming track is passed `Pacing::NonBlocking` and skips a pass rather
than waiting. When both blocked, the pass ran at the *slower* track's rate -- programming
packets carry six more preamble bits -- so the main FIFO drained faster than it refilled, and
at the time an empty FIFO parked the signal pin high rather than going quiet, putting
2.2-2.5ms of DC on the main track between packets (#34, #35). Do not make the programming
track block again, and do not add a third blocking point.

**A starved FIFO now idles on `1` bits rather than parking the pin** (#34). `dcc.pio` pulls
with `pull noblock` and branches to a `starved` loop that emits legal `1` bits until data
arrives, so an empty FIFO costs throughput rather than putting DC on the rails. The cycle
budget is shared between the starved loop and the packet path -- both complete the low half
of a bit cell -- so changing any delay in the header, the gap or `starved` means re-checking
all four paths. `docs/architecture.md` has the arithmetic.

**The program now fills all 32 PIO instruction slots.** Adding anything to `dcc.pio` means
taking something out first. The last slot went to the guard at `have_packet`: a header
claiming **zero** bytes is discarded rather than transmitted, because `jmp x--` turned that 0
into `0xFFFFFFFF` and the state machine spent the rest of the session emitting raw FIFO words
as data bytes with no preamble — only a reboot cleared it (`docs/DCC_Broken.png`). That is
what a FIFO slipped by one word looks like; **what causes the slip is still unknown**, so
treat "the waveform is garbage and a reboot fixes it" as a known open fault, not a solved one.

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
- Emergency stop is a **repeated DCC broadcast** (address `0x00`, instruction `0x41`, sent
  `DCC_ESTOP_BROADCAST_REPEATS` times) that clears the main and hardware queues — not per-loco
  commands. It then **holds** every known loco at speed 0, direction preserved, rather than
  forgetting them: the reminder stream is what keeps asserting "stopped" for a loco that
  missed the broadcast, and it needs the table to do it (#3). Track power is left on.
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
- `test/pico_dcc_wire_format_tests.cpp` pins the **exact bytes** put on the rails and sent
  back to the host. The throttle encoding was arrived at empirically against JMRI and the
  reasoning was never recorded, so changing an assertion there means the wire format moved
  and the change needs re-testing on hardware, in isolation. Some assertions deliberately
  record current *defective* behaviour and name the issue; that is so a fix shows up as a
  reviewable diff rather than a silent change.
- `test/pico_dcc_pio_tests.cpp` goes one step further and asserts on the **waveform**. It
  drives the real `sendCommand()`, then runs the words it pushed through an emulator
  (`test/pio_emulator.cpp`) that executes the *assembled* `dcc.pio`, and decodes the result
  back into bits and bytes. That is what catches a defect in the packing or in the PIO
  timing, neither of which is visible from `data[]` or `cmd_data` — see #31 and #33. Unlike
  the wire-format suite, these assert what is **correct**, not what is current.
  **If you edit `lib/PicoDCCTrack/dcc.pio`, regenerate its assembly:**
  ```bash
  pioasm -o hex lib/PicoDCCTrack/dcc.pio test/dcc_pio_program.hex
  ```
  The `.hex` is committed so CI works without the Pico SDK. Where `pioasm` is found, the
  host build re-assembles `dcc.pio` and **fails configure** if the two disagree, so the
  committed copy cannot fall behind. Set `PICODCC_PIOASM` to point at a specific binary.
  The emulator models only the instructions `dcc.pio` uses and fails loudly on anything
  else — it will never silently approximate a program it does not understand.
- The mocks model behaviour the firmware depends on, not just signatures: per-queue storage
  with real capacity, per-ADC-channel readings, counting semaphores that record an acquire
  that would have blocked, and an `assert()` that records failures. Prefer extending
  `test/mocks.cpp` over writing a test that cannot observe the thing it claims to check.

---

## Hardware debugging

Windows dev machine has **no Pico attached** — test mode and code only. Hard faults,
multicore races, PIO timing and ADC problems need the **Linux bench machine**
(`pbarrett@172.18.10.240`), reachable over SSH and provisioned by
`scripts/provision-bench.sh`. Device names, OpenOCD and GDB recipes, and the toolchain
update policy are in `docs/bench-machine-setup.md`.

**Every flash and every debug attach needs Paul's explicit approval, with track power
confirmed first.** Flashing stalls the board, and halting the core under OpenOCD stops DCC
packet generation — both drop the signal, and a decoder that loses it falls back to DC,
which is full speed on a live track. Read-only host inventory over SSH is fine; anything
that reaches the board is not.

Deploy and debug are scripted — use the scripts, do not hand-roll `openocd`, `scp` or `ssh`
one-liners. The `bench` skill routes to them. The split is the safety boundary:

- **Safe, allowlisted, runs unattended**: `scripts/Deploy-Firmware.ps1` (build, validate,
  stage), `bash scripts/bench.sh inventory`, `bash scripts/bench.sh dry-run`.
- **Touches the board, prompts every time**: `bash scripts/bench.sh flash|fault|config|dccex`.

Do not add the board-touching subcommands to `.claude/settings.json`, and do not widen the
existing rules to a `scripts/bench.sh:*` wildcard — that per-use prompt is the enforcement
of the rule above.

`Deploy-Firmware.ps1` fails the build if any LOAD segment reaches the config sector, and
`flash` reads that sector before and after and refuses to report success if it changed.
`dccex` gates any command that can energise the track or move a locomotive behind `--force`.

There is no long-running OpenOCD daemon and no port 50002 — that was the VS Code Raspberry
Pi extension starting it invisibly. OpenOCD is launched on demand.

---

## Workflow

- **Everything reaches `main` through a PR.** Branch from `origin/main`, never fast-forward
  onto `main` locally. Rebase onto main; never merge main into a branch. The one exception is
  a stack layer, which branches from the layer below — see below.
- **Docs move with the code, in the same PR.** If a change falsifies `docs/architecture.md`,
  `docs/README.md` or this file, fix it in the same branch — never as a follow-up.
- Pass long PR bodies to `gh` via `--body-file`, never a here-string.
- Run the test suite before reporting anything complete. Run the hardware build too if the
  change could affect it.

### Stacked PRs — use `gh stack`, not hand-rolled base branches

Two branches in flight that touch the same lines happens often here. Any two changes that both
add tests will both edit the per-suite counts in `docs/architecture.md`, and any two that touch
the same component will both edit its section — so independent branches off `main` conflict on
merge even when the code does not overlap.

(That used to be much worse: the exact test total was repeated in four files, so *every* change
touched all four. It now lives in `docs/architecture.md` alone. Keep it that way — a number
duplicated across files is a number that will disagree with itself.)

**Stack them instead.** `gh-stack` (`gh extension install github/gh-stack`) is installed:

```bash
gh stack init <bottom-branch> --base main   # adopt an existing branch as layer 1
gh stack add <next-branch>                  # create layer 2 on top of layer 1, and switch to it
gh stack view                               # see the whole stack
gh stack switch                             # move between layers
gh stack submit --auto --open               # push every layer, create/update the PRs, link the stack
```

`gh stack submit` opens an interactive editor in a real terminal. Non-interactive shells —
including the agent's — must pass `--auto`, which generates titles and creates **drafts**
unless `--open` is also given. Auto-generated titles are not good enough for this repo, so
follow up with `gh pr edit <n> --title ... --body-file ...`.

Rules that still apply inside a stack:

- **Merge bottom-up.** Layer 1 first. GitHub rebases and retargets the layers above.
- Each layer must be green on its own: `ctest --preset host` passes at every layer, and test
  counts in the docs are **cumulative** — layer 2's docs state the total including layer 1.
- Docs still move with the code, in the layer that changes the behaviour.
- Never fix a conflict by merging `main` into a layer. Rebase the bottom layer, then let
  `gh stack submit` restack the rest.

Do **not** hand-roll this by setting a PR's base to another branch. That works right up until
the bottom PR merges, at which point the upper branch still carries the bottom's pre-rebase
commit, GitHub reports `CONFLICTING`, and it has to be rebased and force-pushed by hand.

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
  validator accepts only `<D ACK ...>` and `<D SPEED28|SPEED128 [cab]>`, so every other
  `D` subcommand is rejected.
- **CV programming methods are declarations only** — no bodies in `pico_dccloco.cpp`.
- **ACK detection does not exist** in `PicoDccTrack`. Its parameters are tunable and
  persistable, which makes it look implemented; it is not.

Service mode programming is being built on the `programming` branch, not on `main`.
