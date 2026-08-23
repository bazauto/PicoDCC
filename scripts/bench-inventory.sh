#!/usr/bin/env bash
# Read-only inventory of the PicoDCC bench machine.
#
#   ssh pbarrett@172.18.10.240 bash -s < scripts/bench-inventory.sh
#
# Answers "is the bench ready, and is anything in the way" in one round trip.
# Touches nothing: no OpenOCD, no serial port opened, no reset, no flash. Safe
# to run unattended and safe to run while the layout is live.
#
# The login shell on the bench is fish, so this is piped to bash explicitly --
# bash syntax passed as an ssh command string fails with a parse error.
set -uo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/.pico-sdk}"
STAGE_DIR="${STAGE_DIR:-$HOME/picodcc-deploy}"

ok()   { printf "  ok    %-26s %s\n" "$1" "$2"; }
bad()  { printf "  FAIL  %-26s %s\n" "$1" "$2"; }
note() { printf "  --    %-26s %s\n" "$1" "$2"; }

newest_under() {
    local root="$1" probe="$2" d
    [ -d "$root" ] || return 1
    while read -r d; do
        [ -e "$root/$d/$probe" ] && { echo "$root/$d"; return 0; }
    done < <(ls -1 "$root" 2>/dev/null | sort -Vr)
    return 1
}

echo
echo "=== PicoDCC bench inventory (read-only) ==="
echo
echo "-- host"
ok "hostname" "$(hostname)"
ok "load" "$(uptime | sed 's/.*load average: //')"

echo
echo "-- probe and devices"

# The CMSIS-DAP side of the Debugprobe is what OpenOCD drives. It is raw USB,
# not a tty, so lsusb is the only place it shows up.
if lsusb 2>/dev/null | grep -q "2e8a:000c"; then
    ok "debugprobe (CMSIS-DAP)" "$(lsusb | grep '2e8a:000c' | sed 's/.*ID //')"
else
    bad "debugprobe (CMSIS-DAP)" "2e8a:000c not on the USB bus -- probe unplugged?"
fi

# ttyACM numbering follows connection order, so these aliases are the only safe
# way to address the devices. If a rule is missing the target may still be there
# under a different name -- say so rather than reporting a hard failure.
for dev in picodcc-dccex layout-feedback layout-scope; do
    if [ -e "/dev/$dev" ]; then
        ok "/dev/$dev" "-> $(basename "$(readlink -f "/dev/$dev")")"
    else
        bad "/dev/$dev" "udev alias missing -- re-run scripts/provision-bench.sh"
    fi
done

echo
echo "-- channel contention"

# One DCC-EX channel, several things that want it. layout-orchestrator holding
# the port is the expected case, not an error -- but it has to be named before a
# protocol probe is attempted, or the failure looks like a firmware fault.
if systemctl list-unit-files "$ORCH_UNIT" >/dev/null 2>&1; then
    if systemctl is-active --quiet "$ORCH_UNIT"; then
        note "$ORCH_UNIT" "active -- flash and dccex will stop it and restart it after"
    else
        ok "$ORCH_UNIT" "inactive -- left alone, and not restarted by flash or dccex"
    fi
fi

if [ -e /dev/picodcc-dccex ]; then
    holders=$(fuser /dev/picodcc-dccex 2>/dev/null | tr -s ' ')
    if [ -n "$holders" ]; then
        for pid in $holders; do
            note "dccex port held by" "pid $pid ($(ps -o comm= -p "$pid" 2>/dev/null || echo unknown))"
        done
    else
        ok "dccex port" "free"
    fi
fi

if pgrep -x openocd >/dev/null 2>&1; then
    note "openocd" "already running (pid $(pgrep -x openocd | tr '\n' ' '))-- a debug session is open"
else
    ok "openocd" "not running"
fi

echo
echo "-- toolchain"
for pair in "sdk:pico_sdk_init.cmake" "toolchain:bin/arm-none-eabi-gcc" \
            "openocd:openocd" "picotool:picotool/picotool"; do
    name="${pair%%:*}"; probe="${pair#*:}"
    if d=$(newest_under "$PICO_ROOT/$name" "$probe"); then
        ok "$name" "$(basename "$d")"
    else
        bad "$name" "not found under $PICO_ROOT/$name"
    fi
done

# OpenOCD needs the Raspberry Pi fork: stock Ubuntu 0.12.0 has no RP2350 target
# at all, and the failure it produces does not point at the cause.
if OCD=$(newest_under "$PICO_ROOT/openocd" "openocd"); then
    if [ -e "$OCD/scripts/target/rp2350.cfg" ]; then
        ok "rp2350 target cfg" "present"
    else
        bad "rp2350 target cfg" "missing -- stock OpenOCD? the RPi fork is required"
    fi
    if ! "$OCD/openocd" --version >/dev/null 2>&1; then
        bad "openocd runs" "failed to start -- missing libhidapi-hidraw0?"
    else
        ok "openocd runs" "$("$OCD/openocd" --version 2>&1 | head -1 | cut -d' ' -f1-4)"
    fi
fi

echo
echo "-- permissions"
for g in dialout plugdev; do
    if id -nG | tr ' ' '\n' | grep -qx "$g"; then
        ok "group $g" "member"
    else
        bad "group $g" "not a member -- OpenOCD/picotool will need sudo"
    fi
done

echo
echo "-- staged firmware"
if [ -f "$STAGE_DIR/PicoDCC.elf" ]; then
    ok "elf" "$(stat -c '%s bytes, %y' "$STAGE_DIR/PicoDCC.elf" | cut -d. -f1)"
    if [ -f "$STAGE_DIR/STAGED" ]; then
        read -r h c t < "$STAGE_DIR/STAGED"
        ok "staged from" "commit $c at $t"
        ok "sha256" "${h:0:16}..."
    else
        note "provenance" "no STAGED marker -- copied by hand?"
    fi
else
    note "elf" "nothing staged in $STAGE_DIR"
fi
echo
