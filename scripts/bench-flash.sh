#!/usr/bin/env bash
# Flash staged PicoDCC firmware over SWD, and prove the config sector survived.
#
#   bash scripts/bench.sh flash --expect <sha256>
#   bash scripts/bench.sh dry-run
#
# Invoke it through bench.sh rather than by hand: bench.sh prepends
# scripts/bench-orchestrator.sh, which is where orchestrator_stop and
# orchestrator_restore come from. Piping this file on its own will fail at the
# first of those calls.
#
# THIS TOUCHES THE BOARD. Flashing stalls it, and a decoder that loses the DCC
# signal falls back to DC -- which is full speed if the track is live. Track
# power must be off before this runs. It is deliberately NOT on the Claude Code
# allowlist so that it prompts every time; scripts/Deploy-Firmware.ps1 does all
# the safe work so that this stays the only step needing a decision.
#
# What it adds over a bare `openocd -c program`:
#   - refuses an image that is not the one Deploy-Firmware.ps1 validated
#   - stops layout-orchestrator.service first, and restarts it afterwards
#   - reads the 4KB config sector before and after, and compares
#
# That last part matters. docs/firmware-update-config-preservation.md asserts
# that flashing preserves the config sector, reasoning from the linker script.
# The reasoning is sound but it had never been observed. This observes it.
set -uo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/.pico-sdk}"
STAGE_DIR="${STAGE_DIR:-$HOME/picodcc-deploy}"
ELF="$STAGE_DIR/PicoDCC.elf"
SPEED="${SPEED:-5000}"

# Mirrors CONFIG_FLASH_OFFSET / CONFIG_FLASH_SIZE in pico_config_storage.h.
CONFIG_ADDR=0x103FF000
CONFIG_SIZE=4096

EXPECT=""
DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --expect) EXPECT="${2:-}"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        --elf) ELF="${2:-}"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

ok()  { printf "  ok    %-26s %s\n" "$1" "$2"; }
bad() { printf "  FAIL  %-26s %s\n" "$1" "$2"; }

newest_under() {
    local root="$1" probe="$2" d
    [ -d "$root" ] || return 1
    while read -r d; do
        [ -e "$root/$d/$probe" ] && { echo "$root/$d"; return 0; }
    done < <(ls -1 "$root" 2>/dev/null | sort -Vr)
    return 1
}

# Decode the config sector. Layout is pico_config_t: magic, version, five
# floats, two uint16 limits, reserved[3960], then a CRC32 over everything
# before it (zlib CRC-32: init 0xFFFFFFFF, reflected 0xEDB88320, final xor).
decode_config() {
    python3 - "$1" <<'PY'
import struct, sys, zlib
raw = open(sys.argv[1], 'rb').read()
SIZE = 4096                      # sizeof(pico_config_t) -- fills the sector (#13).
                                 # Was 3996. The checksum sits at SIZE-4, so a sector
                                 # written by pre-#13 firmware now reads INVALID --
                                 # correct, and exactly what the firmware itself does.
if len(raw) < SIZE:
    print("    sector too short"); sys.exit(0)
magic, version = struct.unpack_from('<II', raw, 0)
if magic != 0x50444343:
    blank = all(b == 0xFF for b in raw[:SIZE])
    print("    magic    0x%08X  %s" % (magic, "erased/blank" if blank else "NOT 'PDCC' -- no valid config"))
    sys.exit(0)
adc, thr, mn, mx, base = struct.unpack_from('<5f', raw, 8)
main_lim, prog_lim = struct.unpack_from('<HH', raw, 28)
stored, = struct.unpack_from('<I', raw, SIZE - 4)
calc = zlib.crc32(raw[:SIZE - 4]) & 0xFFFFFFFF
print("    magic    'PDCC'  version %d" % version)
print("    adc_to_ma        %.6f" % adc)
print("    ack_threshold    %.1f mA" % thr)
print("    ack_duration     %.1f - %.1f ms" % (mn, mx))
print("    baseline_current %.1f mA" % base)
print("    current_limits   main %d mA, prog %d mA" % (main_lim, prog_lim))
print("    crc32    0x%08X  %s" % (stored, "valid" if stored == calc else "INVALID (calculated 0x%08X)" % calc))
PY
}

echo
echo "=== PicoDCC flash over SWD ==="
echo
echo "  *** This writes firmware to the board. Track power must be OFF. ***"
echo

echo "-- preflight"

OCD_DIR=$(newest_under "$PICO_ROOT/openocd" "openocd") || { bad "openocd" "not found under $PICO_ROOT/openocd"; exit 1; }
OCD="$OCD_DIR/openocd"
ok "openocd" "$(basename "$OCD_DIR")"

if ! lsusb 2>/dev/null | grep -q "2e8a:000c"; then
    bad "debugprobe" "CMSIS-DAP 2e8a:000c not on the USB bus -- nothing to flash through"
    exit 1
fi
ok "debugprobe" "present"

if pgrep -x openocd >/dev/null 2>&1; then
    bad "openocd" "already running (pid $(pgrep -x openocd | tr '\n' ' ')) -- the probe is busy; stop that session first"
    exit 1
fi
ok "probe free" "no other openocd"

[ -f "$ELF" ] || { bad "elf" "$ELF not found -- run scripts/Deploy-Firmware.ps1 first"; exit 1; }
ACTUAL=$(sha256sum "$ELF" | cut -d' ' -f1)
ok "elf" "$(basename "$ELF") ($(stat -c %s "$ELF") bytes)"

# Refuse to flash something other than the image that was validated. Without
# this the staging checks are advisory -- a stale or hand-copied ELF would flash
# just as happily, and the config-sector guarantee would not apply to it.
if [ -n "$EXPECT" ]; then
    if [ "$ACTUAL" != "$EXPECT" ]; then
        bad "sha256" "staged image is not the validated one"
        echo "          expected ${EXPECT:0:32}..."
        echo "          actual   ${ACTUAL:0:32}..."
        exit 1
    fi
    ok "sha256" "matches validated image"
else
    printf "  --    %-26s %s\n" "sha256" "${ACTUAL:0:16}... (no --expect given, not verified against a build)"
fi

if [ -f "$STAGE_DIR/STAGED" ]; then
    read -r _h c t < "$STAGE_DIR/STAGED"
    ok "provenance" "commit $c staged $t"
fi

if [ "$DRY" = "1" ]; then
    echo
    echo "DRY RUN -- preflight only, nothing written to the board."
    exit 0
fi

# --- Before ----------------------------------------------------------------

PRE=$(mktemp)
POST=$(mktemp)
LOG=$(mktemp)

# One EXIT trap, doing both jobs. orchestrator_guard would install its own and
# silently replace this one -- traps do not stack -- so the restart is composed
# in here instead. It runs first: putting the layout's controller back matters
# more than removing three temp files.
trap 'orchestrator_restore; rm -f "$PRE" "$POST" "$LOG"' EXIT

# The orchestrator holds /dev/picodcc-dccex open and drives the board. Flashing
# resets it underneath that connection, so stop the service before writing and
# let the trap restart it. Refuse rather than flash under a live orchestrator:
# track power being off makes that safe, not correct.
echo
echo "-- layout orchestrator"
orchestrator_stop || {
    echo
    echo "Refusing to flash while the orchestrator still has the board."
    exit 1
}

echo
echo "-- config sector before flash"

"$OCD" -s "$OCD_DIR/scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed $SPEED" -c "init" -c "halt" \
    -c "dump_image $PRE $CONFIG_ADDR $CONFIG_SIZE" -c "shutdown" >"$LOG" 2>&1
if [ $? -ne 0 ] || [ ! -s "$PRE" ]; then
    bad "read config sector" "openocd failed"
    tail -15 "$LOG" | sed 's/^/    /'
    exit 1
fi
PRE_HASH=$(sha256sum "$PRE" | cut -d' ' -f1)
ok "read" "$CONFIG_SIZE bytes from $CONFIG_ADDR"
decode_config "$PRE"

# --- Flash -----------------------------------------------------------------

echo
echo "-- flashing"

"$OCD" -s "$OCD_DIR/scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed $SPEED" \
    -c "program $ELF verify reset exit" >"$LOG" 2>&1
RC=$?

if [ $RC -ne 0 ]; then
    bad "program" "openocd exited $RC"
    tail -25 "$LOG" | sed 's/^/    /'
    echo
    echo "BOARD MAY BE IN AN UNKNOWN STATE -- do not power the track until this is resolved."
    exit 1
fi

grep -qi "verified OK" "$LOG" && ok "program" "written and verified" || {
    bad "program" "openocd returned 0 but did not report 'verified OK'"
    tail -25 "$LOG" | sed 's/^/    /'
    exit 1
}
grep -Ei "wrote [0-9]+ bytes" "$LOG" | tail -1 | sed 's/^/  --    written                    /'

# --- After -----------------------------------------------------------------

echo
echo "-- config sector after flash"

"$OCD" -s "$OCD_DIR/scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed $SPEED" -c "init" -c "halt" \
    -c "dump_image $POST $CONFIG_ADDR $CONFIG_SIZE" \
    -c "reset run" -c "shutdown" >"$LOG" 2>&1
if [ $? -ne 0 ] || [ ! -s "$POST" ]; then
    bad "read config sector" "openocd failed after flash"
    tail -15 "$LOG" | sed 's/^/    /'
    exit 1
fi
POST_HASH=$(sha256sum "$POST" | cut -d' ' -f1)
decode_config "$POST"

echo
echo "-- result"
if [ "$PRE_HASH" = "$POST_HASH" ]; then
    ok "config preserved" "sector byte-identical across the flash"
else
    bad "config preserved" "SECTOR CHANGED -- firmware image overwrote calibration"
    echo "          before ${PRE_HASH:0:32}..."
    echo "          after  ${POST_HASH:0:32}..."
    echo "          This contradicts memmap_picodcc.ld. Investigate before flashing again."
    exit 1
fi
ok "board" "reset and running new firmware"

echo
echo "FLASH OK"
echo "  Track power may be restored when you are ready. Verify the firmware answers first:"
echo "    bash scripts/bench.sh dccex '<s>'"
echo
