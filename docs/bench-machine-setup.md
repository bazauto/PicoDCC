# Bench machine setup

The Linux bench machine hosts the actual PicoDCC hardware. The Windows dev machine has no
Pico attached, so anything involving hard faults, multicore races, PIO timing or ADC
behaviour happens here.

This document is the rebuild recipe. If the machine is reinstalled, everything needed to get
back to a working bench is below, and `scripts/provision-bench.sh` automates the parts that
can be automated.

---

## Access

```bash
ssh pbarrett@172.18.10.240
```

Key-based auth from the Windows dev machine. Two things that bite:

- **The login shell is fish.** Bash syntax passed as an `ssh host '...'` command string fails
  with a parse error. Pipe scripts to bash explicitly:
  ```bash
  ssh pbarrett@172.18.10.240 bash -s < scripts/provision-bench.sh
  ```
  Arguments go through fish too, before `bash -s` ever runs. A DCC-EX command like `<s>`
  reads as a redirection and dies with `Expected a string, but found end of the input`, so
  each argument has to be single-quoted inside the command string. `scripts/bench.sh` does
  that for you, which is the reason to go through it rather than building the `ssh` line by
  hand.
- Non-interactive sessions have no controlling tty, so nothing that prompts will work. Use
  `-o BatchMode=yes` so a would-be prompt fails fast instead of hanging.

---

## Attached devices, and why the names matter

Three USB devices. `ttyACM` numbering follows **connection order**, so the command station's
probe and the layout-feedback Pico swap places depending on what enumerated first. Never
address them by raw `ttyACM*` name.

`scripts/provision-bench.sh` installs udev rules giving each a stable alias:

| Stable name | Device | Role |
|---|---|---|
| `/dev/picodcc-dccex` | Raspberry Pi Debugprobe, CDC interface | **DCC-EX channel** to the command station. This is what JMRI used to talk to, and what `layout-orchestrator` will use. |
| `/dev/layout-feedback` | Second Pico (serial `0d9a62134acbf42d`) | Unrelated project: sensors and point-position monitoring over MQTT, on its own Ethernet. **Not** the command station. |
| `/dev/layout-scope` | FTDI FT232 (METACHIP BS100U) | A digital oscilloscope. **Not a serial console — never open it as one.** |

The same physical Debugprobe serves two purposes: its CDC interface is the DCC-EX channel
above, and its CMSIS-DAP interface is what OpenOCD drives over raw USB for flashing and
debugging. The CMSIS-DAP side is not a tty and has no `/dev/tty*` entry.

If a device is ever replaced, its serial number changes and the matching rule in
`scripts/provision-bench.sh` needs updating. Find the new serial with:

```bash
udevadm info -q property -n /dev/ttyACM0 | grep ID_SERIAL_SHORT
```

`/dev/serial/by-id/` also gives stable, serial-derived names with no setup at all, and is a
reasonable fallback if the udev rules are ever lost.

---

## Provisioning

```bash
ssh pbarrett@172.18.10.240 bash -s < scripts/provision-bench.sh
```

Idempotent — re-running is also how a toolchain update is picked up. It is host-side only:
it never flashes, never attaches a debugger, and never touches the board.

What it does, and why each part is needed:

1. **`libhidapi-hidraw0`, `libhidapi-libusb0`.** The Raspberry Pi OpenOCD fork links against
   hidapi-hidraw, which nothing on a stock Mint/Ubuntu install pulls in. Without it OpenOCD
   dies at startup with `libhidapi-hidraw.so.0: cannot open shared object file` and no hint
   that a package is the fix. This was the *only* thing actually missing on the current build.
2. **udev rules** — stable device names, plus `plugdev` access to the CMSIS-DAP probe and to
   an RP2 in BOOTSEL mode, so OpenOCD and picotool run without `sudo`.
3. **Toolchain discovery and a fish `conf.d` snippet** putting the SDK, ARM toolchain,
   OpenOCD, picotool, CMake and Ninja on `PATH`, and exporting `PICO_SDK_PATH` /
   `PICO_TOOLCHAIN_PATH`.
4. **Group membership** in `dialout` and `plugdev`.

### The one thing it cannot do

It cannot install the Pico SDK itself. That currently arrives via the VS Code Raspberry Pi
extension, which unpacks everything under `~/.pico-sdk`. On a rebuilt machine, either install
that extension once and let it fetch the SDK, or unpack an SDK tree into `~/.pico-sdk`
matching the layout below, then run the provisioning script.

```
~/.pico-sdk/
  sdk/<version>/            pico_sdk_init.cmake, src/, lib/
  toolchain/<version>/bin/  arm-none-eabi-gcc, arm-none-eabi-gdb, ...
  openocd/<version>/        openocd, scripts/
  picotool/<version>/picotool/picotool
  cmake/<version>/bin/cmake
  ninja/<version>/ninja
```

---

## Toolchain versions

Both machines are currently on the same versions, which matters: a different SDK generates
different firmware, so drift between them turns "works on mine" into a real difference in
what runs on the rails.

| Component | Version | Notes |
|---|---|---|
| Pico SDK | 2.2.0 | Upstream 2.3.0 is available |
| ARM GCC | 14.2.Rel1 (14.2.1) | |
| OpenOCD | 0.12.0+dev (RPi fork) | Stock Ubuntu's 0.12.0 has **no RP2350 target** — the fork is required |
| picotool | 2.2.0-a4 | |
| CMake | 3.31.5 | |
| Ninja | 1.12.1 | |

### Update policy

Nothing in `scripts/provision-bench.sh` or `scripts/Validate-DualMode.ps1` hardcodes a
version — both resolve the newest install under `~/.pico-sdk`. So the mechanical part of an
update is just: install the new version alongside the old, re-run the provisioning script,
done. The old version can stay in place; discovery picks the newest.

The mechanics are the easy half. **An SDK bump is not a routine dependency update on this
project.** The SDK supplies the multicore, PIO, flash and ADC primitives that DCC timing
depends on, which is exactly the code where a regression means a locomotive moving when
nobody asked. CI does not cross-compile, so an SDK change is completely invisible to it.

Treat it accordingly:

- Update on its own branch, never alongside a feature change.
- Re-run the hardware build and diff the firmware size — a large unexplained change is worth
  understanding before it reaches the rails.
- Re-test on hardware, in isolation, before merging. Particularly DCC packet timing and
  current sensing.
- Update **both machines together**, and update the table above in the same PR.

Not chasing releases is a defensible position for embedded work: a pinned, known-good
toolchain is a feature. Check periodically rather than continuously, and update when there is
a reason — an RP2350 erratum mitigation, or a fix in multicore, flash, PIO or ADC. The SDK
changelog is the thing to read, not the version number.

---

## Deploy and debug

> **Every flash and every debug attach needs explicit approval, with track power confirmed
> first.** Flashing stalls the board; attaching and halting the core under OpenOCD stops DCC
> packet generation. Both produce the same failure: a decoder that loses the signal falls back
> to DC, which means full speed if the track is live. Read-only host inventory over SSH is
> safe; anything that reaches the board is not.

These operations are scripted. Prefer the scripts over the raw recipes below — they carry
the preflight checks, and the flash script proves the config sector survived rather than
assuming it.

```bash
# Safe: never touches the board
pwsh -NoProfile -File scripts/Deploy-Firmware.ps1   # build, validate, stage to the bench
bash scripts/bench.sh inventory                     # probe, devices, contention, toolchain
bash scripts/bench.sh dry-run                       # flash preflight, writes nothing

# Touches the board: approval + track power off
bash scripts/bench.sh flash --expect <sha256>       # program over SWD
bash scripts/bench.sh fault                         # halt, registers + backtrace, resume
bash scripts/bench.sh config                        # read and decode the config sector
bash scripts/bench.sh dccex '<s>'                   # DCC-EX health check
```

`Deploy-Firmware.ps1` prints the exact `flash` line, including the `--expect` hash, so the
flash refuses any image other than the one just validated. The split is deliberate: the safe
half runs unattended, and the board-touching half stays behind a per-use approval prompt.

### The underlying recipes

Flash over SWD, no BOOTSEL button needed:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
        -c "adapter speed 5000" \
        -c "program /path/to/PicoDCC.elf verify reset exit"
```

Interactive debug — start OpenOCD as a server, then attach GDB:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"
# in another session:
arm-none-eabi-gdb PicoDCC.elf -ex "target extended-remote localhost:3333"
```

`bash scripts/bench.sh fault` is usually the better tool: it captures both cores' registers
and backtraces in one batch and puts the core back, where an interactive session holds it
halted — and therefore holds DCC output stopped — for as long as it is open.

OpenOCD's telnet console is on 4444 by default. Earlier versions of this document referred to
a long-running server on port 50002; there is no such daemon — OpenOCD is started on demand,
and the VS Code Raspberry Pi extension used to do it invisibly.

DCC-EX traffic, for protocol-level testing against real hardware — 115200 8N1 on the probe's
CDC interface. **`picocom` is not installed on the bench**, so `scripts/bench-dccex.sh` drives
the port with the Python standard library instead (no `pyserial` dependency either). It also
refuses commands that can energise the track or move a locomotive unless given `--force`,
which a raw terminal will not do for you.

---

## Contention

There is one DCC-EX channel and several things that will want it. `layout-orchestrator` runs on
this same machine as an enabled systemd unit and holds `/dev/picodcc-dccex` for the life of the
service. While it holds the port, protocol-level debugging cannot use it, and vice versa.

**`flash` and `dccex` handle this themselves.** Both stop `layout-orchestrator.service` before
touching the board and restart it on the way out, via `scripts/bench-orchestrator.sh` — one
copy of the logic, prepended by `bench.sh` to whichever script it pipes over. Two rules govern
it:

- **Only what we stopped gets restarted.** A service that was already inactive is left alone
  and not started afterwards. That is the escape hatch: stop it yourself and the scripts will
  not start it behind you.
- **The restart happens on any exit**, installed as an `EXIT` trap, so a failed preflight, an
  OpenOCD error or a Ctrl-C all still put the layout's controller back. A restart that fails is
  reported loudly with the manual command, because the layout has no controller until someone
  acts on it.

`bench-flash.sh` composes the restart into its existing temp-file trap rather than calling
`orchestrator_guard` — traps do not stack, and installing a second `EXIT` trap would silently
replace the first. `dry-run` stops nothing, because it writes nothing.

`fault` and `config` do **not** stop the service. They halt the board rather than contend for
the serial port, so there is no port conflict to resolve; the orchestrator will see the DCC-EX
channel go quiet for the duration. Stop it by hand if that matters for what you are debugging.

`bench.sh inventory` reports the unit's state alongside the port holders, so a held channel
identifies itself before you run anything.

The machine itself is not short of headroom — 4 cores, 7.6 GB RAM, load average around 0.15
idle — so running OpenOCD, the orchestrator and an MQTT client together is not a resource
concern.
