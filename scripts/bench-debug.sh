#!/usr/bin/env bash
# Attach to the PicoDCC board and capture state, without an interactive session.
#
#   ssh <bench> bash -s -- fault  < scripts/bench-debug.sh    # halt, registers, backtrace, resume
#   ssh <bench> bash -s -- config < scripts/bench-debug.sh    # read and decode the config sector
#
#   ... bash -s -- fault --no-resume < scripts/bench-debug.sh # leave the core halted
#
# THIS TOUCHES THE BOARD. Halting the core stops DCC packet generation, which
# produces exactly the same hazard as flashing: a decoder that loses the signal
# falls back to DC, meaning full speed on a live track. Track power must be off.
# Not on the Claude Code allowlist -- it prompts every time, by design.
#
# `fault` exists because the useful thing after a hard fault is one batch of
# state, not a live gdb session: both cores' registers and backtraces, captured
# and printed, with the core put back the way it was found. An interactive
# session holds the core halted for as long as it is open, which is the thing
# to avoid.
set -uo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/.pico-sdk}"
STAGE_DIR="${STAGE_DIR:-$HOME/picodcc-deploy}"
ELF="${ELF:-$STAGE_DIR/PicoDCC.elf}"
SPEED="${SPEED:-5000}"
GDB_PORT="${GDB_PORT:-3333}"

CONFIG_ADDR=0x103FF000
CONFIG_SIZE=4096

MODE="${1:-}"; shift || true
RESUME=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-resume) RESUME=0; shift ;;
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

OCD_DIR=$(newest_under "$PICO_ROOT/openocd" "openocd") || { bad "openocd" "not found"; exit 1; }
OCD="$OCD_DIR/openocd"
TC_DIR=$(newest_under "$PICO_ROOT/toolchain" "bin/arm-none-eabi-gcc") || TC_DIR=""
GDB="$TC_DIR/bin/arm-none-eabi-gdb"

preflight() {
    lsusb 2>/dev/null | grep -q "2e8a:000c" || { bad "debugprobe" "CMSIS-DAP not on the USB bus"; exit 1; }
    ok "debugprobe" "present"
    if pgrep -x openocd >/dev/null 2>&1; then
        bad "probe busy" "openocd already running (pid $(pgrep -x openocd | tr '\n' ' ')) -- stop it first"
        exit 1
    fi
    ok "probe free" "no other openocd"
}

# --- config ----------------------------------------------------------------

if [ "$MODE" = "config" ]; then
    echo
    echo "=== PicoDCC config sector ==="
    echo "  *** Reading flash requires halting the core. Track power must be OFF. ***"
    echo
    preflight

    DUMP=$(mktemp); LOG=$(mktemp)
    trap 'rm -f "$DUMP" "$LOG"' EXIT

    # An array, not a string: "-c reset run" word-splits into three arguments and
    # openocd rejects `run` as a stray positional ("Unexpected command line
    # argument: run"), so `config` and `fault` failed outright before reading anything.
    RESUME_CMD=(-c "reset run")
    [ "$RESUME" = "0" ] && RESUME_CMD=()

    "$OCD" -s "$OCD_DIR/scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
        -c "adapter speed $SPEED" -c "init" -c "halt" \
        -c "dump_image $DUMP $CONFIG_ADDR $CONFIG_SIZE" "${RESUME_CMD[@]}" -c "shutdown" >"$LOG" 2>&1

    if [ $? -ne 0 ] || [ ! -s "$DUMP" ]; then
        bad "read" "openocd failed"; tail -15 "$LOG" | sed 's/^/    /'; exit 1
    fi
    ok "read" "$CONFIG_SIZE bytes from $CONFIG_ADDR"
    [ "$RESUME" = "1" ] && ok "board" "reset and running" || ok "board" "left HALTED (--no-resume)"
    echo

    python3 - "$DUMP" <<'PY'
import struct, sys, zlib
raw = open(sys.argv[1], 'rb').read()
SIZE = 4096                      # sizeof(pico_config_t) -- fills the sector (#13).
                                 # Was 3996. The checksum sits at SIZE-4, so a sector
                                 # written by pre-#13 firmware now reads INVALID --
                                 # correct, and exactly what the firmware itself does.
magic, version = struct.unpack_from('<II', raw, 0)
if magic != 0x50444343:
    blank = all(b == 0xFF for b in raw[:SIZE])
    print("  magic    0x%08X  %s" % (magic, "erased/blank -- firmware will use factory defaults"
                                     if blank else "NOT 'PDCC' -- no valid config stored"))
    sys.exit(0)
adc, thr, mn, mx, base = struct.unpack_from('<5f', raw, 8)
main_lim, prog_lim = struct.unpack_from('<HH', raw, 28)
stored, = struct.unpack_from('<I', raw, SIZE - 4)
calc = zlib.crc32(raw[:SIZE - 4]) & 0xFFFFFFFF

print("  magic              'PDCC'   version %d" % version)
print("  adc_to_ma          %.6f" % adc)
print("  ack_threshold      %.1f mA" % thr)
print("  ack_duration       %.1f - %.1f ms" % (mn, mx))
print("  baseline_current   %.1f mA" % base)
print("  main_current_limit %d mA" % main_lim)
print("  prog_current_limit %d mA" % prog_lim)
print("  crc32              0x%08X  %s" % (stored, "valid" if stored == calc
                                           else "INVALID (calculated 0x%08X)" % calc))

# The firmware's own validateConfig() rejects out-of-range values even when the
# CRC is good, and then silently falls back to defaults. Run the same ranges here
# so a config that is intact but unusable is visible rather than looking fine.
for name, val, lo, hi in [("adc_to_ma", adc, 0.0, 1.0),
                          ("ack_threshold", thr, 30.0, 100.0),
                          ("ack_min_duration", mn, 1.0, 10.0),
                          ("ack_max_duration", mx, 1.0, 10.0)]:
    if not (lo < val <= hi if name == "adc_to_ma" else lo <= val <= hi):
        print("  WARNING: %s = %g is outside %g..%g -- firmware will reject this config"
              % (name, val, lo, hi))
PY
    echo
    exit 0
fi

# --- fault -----------------------------------------------------------------

if [ "$MODE" = "fault" ]; then
    echo
    echo "=== PicoDCC fault triage ==="
    echo "  *** Halts both cores. DCC output stops. Track power must be OFF. ***"
    echo
    preflight

    [ -x "$GDB" ] || { bad "gdb" "arm-none-eabi-gdb not found under $PICO_ROOT/toolchain"; exit 1; }
    [ -f "$ELF" ] || { bad "elf" "$ELF not found -- symbols are needed for a backtrace"; exit 1; }
    ok "symbols" "$(basename "$ELF")"

    OLOG=$(mktemp); GLOG=$(mktemp)
    OCD_PID=""
    cleanup() {
        [ -n "$OCD_PID" ] && kill "$OCD_PID" 2>/dev/null
        rm -f "$OLOG" "$GLOG"
    }
    trap cleanup EXIT

    "$OCD" -s "$OCD_DIR/scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
        -c "adapter speed $SPEED" >"$OLOG" 2>&1 &
    OCD_PID=$!

    # Wait for the gdb port rather than sleeping a fixed interval.
    for _ in $(seq 1 50); do
        if grep -q "Listening on port $GDB_PORT" "$OLOG" 2>/dev/null; then break; fi
        if ! kill -0 "$OCD_PID" 2>/dev/null; then
            bad "openocd" "died during startup"; tail -15 "$OLOG" | sed 's/^/    /'; exit 1
        fi
        sleep 0.2
    done
    grep -q "Listening on port $GDB_PORT" "$OLOG" || {
        bad "openocd" "did not open port $GDB_PORT in 10s"; tail -15 "$OLOG" | sed 's/^/    /'; exit 1
    }
    ok "openocd" "listening on $GDB_PORT"

    RESUME_CMD="-ex \"monitor resume\""
    [ "$RESUME" = "0" ] && RESUME_CMD=""

    "$GDB" -batch -nx "$ELF" \
        -ex "set confirm off" \
        -ex "set pagination off" \
        -ex "target extended-remote localhost:$GDB_PORT" \
        -ex "monitor halt" \
        -ex "echo \n--- threads (one per core) ---\n" \
        -ex "info threads" \
        -ex "echo \n--- core 0 ---\n" \
        -ex "thread 1" -ex "info registers pc lr sp xpsr" -ex "bt 12" \
        -ex "echo \n--- core 1 ---\n" \
        -ex "thread 2" -ex "info registers pc lr sp xpsr" -ex "bt 12" \
        >"$GLOG" 2>&1

    # Strip gdb's connection chatter; keep the state.
    sed -n '/--- threads/,$p' "$GLOG" | sed 's/^/  /'

    if [ "$RESUME" = "1" ]; then
        "$GDB" -batch -nx -ex "target extended-remote localhost:$GDB_PORT" \
               -ex "monitor resume" -ex "detach" >/dev/null 2>&1
        echo
        ok "board" "resumed -- DCC generation restarted"
    else
        echo
        printf "  --    %-26s %s\n" "board" "left HALTED (--no-resume). DCC output is stopped."
    fi
    echo
    exit 0
fi

echo "usage: bench-debug.sh {fault|config} [--no-resume] [--elf PATH]" >&2
exit 2
