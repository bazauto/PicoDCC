---
name: bench
description: Build, validate, stage, flash and debug PicoDCC firmware on the Linux bench machine. Use when asked to deploy or flash firmware, test the deployment process, check whether the bench is ready, triage a hard fault, read the config sector, or probe DCC-EX against real hardware.
---

Two entry points cover everything. Run the one that fits and report its output.

Do not re-derive any of this by hand: no ad-hoc `openocd`, no `ssh` one-liners, no
`cmake`, `readelf`, `nm` or `scp`. The scripts already do every check, and
reaching the same answer manually costs a great deal of context.

## Safe — run freely, no approval needed

Neither touches the board.

```bash
bash scripts/bench.sh inventory                      # is the bench ready?
pwsh -NoProfile -File scripts/Deploy-Firmware.ps1    # build + validate + stage
```

`Deploy-Firmware.ps1` configures and builds the `pico` preset into `build/pico`, leaving
the host test tree untouched, then checks size, that no LOAD segment reaches the config
sector at `0x103FF000`,
that `__flash_binary_end` agrees with the segment table, and that the SHA256 matches
on both machines. On success it prints the exact flash command including `--expect`.

Useful switches: `-NoStage` (build and validate only), `-SkipBuild` (re-stage what
is already built).

## Needs approval every time — these touch the board

Each stops DCC output. A decoder that loses the signal falls back to DC, which is
full speed on a live track. **Confirm track power is off in the same turn before
running any of them**, and name the hazard. They are deliberately absent from the
allowlist, so they prompt — that is working as intended, not an obstacle to route
around.

`flash` and `dccex` stop `layout-orchestrator.service` first and restart it
afterwards, including on failure. A service that was already inactive is left
alone and not started. Do not stop or start it by hand around these — the scripts
own it, and starting it yourself after they ran would double up.

```bash
bash scripts/bench.sh flash --expect <sha256>   # program over SWD
bash scripts/bench.sh fault                     # halt, registers + backtrace, resume
bash scripts/bench.sh config                    # read and decode stored config
bash scripts/bench.sh dccex '<s>'               # DCC-EX health check
```

`bash scripts/bench.sh dry-run` runs the full flash preflight and writes nothing,
so it is safe to use to check the path is ready before asking for approval.

`fault` and `config` resume the board when done; pass `--no-resume` to leave it
halted for further inspection, which leaves DCC output stopped.

`dccex` gates anything that can move a locomotive or power the track: `<s>`, `<#>`,
`<0>` and `<!>` run as-is, everything else needs `--force`.

## The normal deploy

1. `Deploy-Firmware.ps1` — unattended; ends with `STAGED OK` and the flash command.
2. Ask for approval, confirming track power is off. Quote the commit being flashed.
3. Run the printed flash line verbatim — the `--expect` hash makes it refuse
   anything other than the image just validated.
4. `bash scripts/bench.sh dccex '<s>'` to confirm the firmware answers. The
   reply carries `PICODCC_IDENTITY` — build date plus git short hash — so check
   it names the commit you just flashed. A trailing `+` means the tree was dirty.

Stop after step 1 and report if the user has not confirmed track power.

## Reading the output

Every script prints `ok` / `FAIL` / `--` lines and a final verdict. Relay failures
verbatim — they name their own fix. Three that mean something specific:

- **`orchestrator: FAILED to restart`** — the layout has no controller. The line
  carries the command to fix it; relay it and do not move on.
- **`config preserved: SECTOR CHANGED`** — the image overwrote calibration,
  contradicting `memmap_picodcc.ld`. Stop; do not flash again until understood.
- **`WARNING: unframed output on the command UART`** — a diagnostic is leaking
  through `DCCEX_RESPONSE()`, which desynchronises JMRI. A real defect; file it.
- **`crc32 INVALID`** or a range `WARNING` from `config` — the stored config will be
  rejected by the firmware, which then silently falls back to factory defaults.

If a script fails on a missing prerequisite, the fix is nearly always
`bash scripts/bench.sh provision`, which is host-side only and never touches the
board.
