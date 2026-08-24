# PicoDCC

DCC command station firmware for the Raspberry Pi Pico 2 (RP2350). It generates the DCC track
signal in PIO, speaks a partial [DCC-EX](https://dcc-ex.com/) protocol to JMRI and other
throttles over UART, and drives a Waveshare ST7789T3 touchscreen through LVGL.

Part of a four-repo control stack for the Westgate Hollow model railway:
[layout-orchestration](https://github.com/bazauto/layout-orchestration) (backend and operator
UI), [layout-feedback](https://github.com/bazauto/layout-feedback) (MicroPython sensor nodes),
[esp-layout-controller](https://github.com/bazauto/esp-layout-controller) (touchscreen
throttle), and this — the command station that puts the signal on the rails.

## What works

Throttle, function and accessory commands; main and programming track signal generation on
separate PIO blocks; track power and overcurrent detection; emergency stop; the LCD UI and
diagnostic log; and flash-backed configuration.

**Command coverage is partial and some things that look implemented are not** — CV programming
is declarations without bodies, ACK detection does not exist despite having tunable parameters,
and `<D CONFIG>` / `<D CAL>` are unreachable. [`docs/README.md`](docs/README.md) carries the
authoritative current-state table and the known-gaps list. Read it before assuming a feature
works, and do not infer behaviour from the upstream DCC-EX documentation.

## Hardware

| | |
|---|---|
| Board | Raspberry Pi Pico 2 (RP2350), `PICO_BOARD=pico2` |
| Host link | UART0 on GP0/GP1, 115200 — this is the DCC-EX command channel |
| Main track | PIO signal GP17, enable GP18, fault LED GP16, current on ADC0/GP26 |
| Programming track | PIO signal GP20, enable GP21, fault LED GP19, current on ADC1/GP27 |
| Display | ST7789T3 over SPI0 (GP2–GP7), CST328 touch over I2C0 (GP8–GP11) |

Full pin map, including what is free for expansion, in
[`docs/gpio-pinout-reference.md`](docs/gpio-pinout-reference.md).

`printf()` goes to USB CDC, not the UART — USB output is for bring-up only, never the protocol
channel.

## Build

Two mutually exclusive modes, each with its own build tree, driven by `CMakePresets.json`.
Always go through a preset.

```bash
# host — host compiler + Ninja + CMocka. This is what you run for almost everything.
cmake --preset host
cmake --build --preset host
ctest --preset host                       # 11 suites, ~0.5s

# pico — ARM GCC cross-build, produces build/pico/src/PicoDCC.uf2
cmake --preset pico
cmake --build --preset pico
```

The hardware build needs `PICO_SDK_PATH` and `PICO_TOOLCHAIN_PATH`, and the **LVGL submodule**:

```bash
git submodule update --init --depth 1 lib/external/lvgl
```

CI runs the `host` preset on every push and PR. It does **not** cross-build firmware, so
hardware-mode breakage is not caught by CI — run the `pico` preset locally before merging
anything that touches shared headers or code behind `#ifdef TEST_BUILD`.

## Architecture

Work is split across both cores:

- **Core 0** parses DCC-EX commands off the UART, orchestrates, and owns the main command queue
  and the operation-mode state machine.
- **Core 1** drives the PIO state machines, monitors current via ADC, and generates locomotive
  reminders when the hardware queue empties. Reminders are hardware-paced, so the queue cannot
  overflow.

Priority is explicit commands > reminders > idle packets. The main track is the pacer and the
only track allowed to block; the programming track skips a pass rather than waiting.

Component responsibilities, the queue design and the operation modes are in
[`docs/architecture.md`](docs/architecture.md).

## Testing

Tests are CMocka, one suite per component, and two of them go further than unit testing:

- `test/pico_dcc_wire_format_tests.cpp` pins the **exact bytes** put on the rails and returned
  to the host. Some assertions deliberately record current *defective* behaviour and name the
  issue, so a fix shows up as a reviewable diff rather than a silent change.
- `test/pico_dcc_pio_tests.cpp` asserts on the **waveform**. It drives the real `sendCommand()`,
  runs the words it pushed through an emulator that executes the *assembled* `dcc.pio`, and
  decodes the result back into bits. That is what catches defects in the packing or the PIO
  timing, neither of which is visible from the packet buffers.

## Safety

This is firmware that puts current on rails, and two constraints are not negotiable.

**Flash writes block both cores for ~410ms.** DCC packets stop, and a decoder that loses the
signal falls back to DC mode — which on a powered track means full speed. Flash writes are
therefore only legal in Layout Maintenance Mode, which requires the main track to be unpowered
and can only be entered from the LCD. Physical presence, never remotely.

**The DCC-EX UART carries protocol replies only.** Errors, warnings and traces go to the
diagnostic log and the LCD. A stray diagnostic on the command UART desynchronises JMRI.

The full analysis is in
[`docs/safety-recommendations.md`](docs/safety-recommendations.md). Project rules, conventions
and workflow are in [`CLAUDE.md`](CLAUDE.md).

## Licence

MIT — see [`LICENSE`](LICENSE). Third-party components, the scope of the MIT grant, and why
implementing DCC itself needs no licence are recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
