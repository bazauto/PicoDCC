#!/usr/bin/env bash
#
# Provision the Linux bench machine that hosts the PicoDCC hardware.
#
# Idempotent - safe to re-run, and re-running is how a toolchain update is
# picked up. Host-side only: this never touches the board, never flashes, and
# never attaches a debugger.
#
# Usage:  ssh pbarrett@<bench> bash -s < scripts/provision-bench.sh
#
# The bench machine's login shell is fish, so bash syntax has to be piped to
# bash explicitly rather than passed as an ssh command string.

set -euo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/.pico-sdk}"
UDEV_RULES=/etc/udev/rules.d/60-picodcc-bench.rules
FISH_CONF="$HOME/.config/fish/conf.d/picodcc-bench.fish"

say() { printf "\n=== %s ===\n" "$1"; }

# Resolve the newest version directory under $1 that contains the probe path $2.
# Nothing here hardcodes a version, so dropping in a newer SDK and re-running is
# the whole update procedure.
newest_under() {
    local root="$1" probe="$2" d
    [ -d "$root" ] || return 1
    while read -r d; do
        [ -e "$root/$d/$probe" ] && { echo "$root/$d"; return 0; }
    done < <(ls -1 "$root" 2>/dev/null | sort -Vr)
    return 1
}

say "1. Runtime libraries"
# OpenOCD from the Raspberry Pi fork links against hidapi-hidraw, which nothing
# else on a stock Mint/Ubuntu install pulls in. Without it OpenOCD dies at
# startup with a missing-shared-object error and no hint that this is the cause.
missing=()
for p in libhidapi-hidraw0 libhidapi-libusb0; do
    dpkg -l "$p" 2>/dev/null | grep -q '^ii' || missing+=("$p")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "installing: ${missing[*]}"
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "${missing[@]}"
else
    echo "already present"
fi

say "2. udev rules"
# Two jobs. First, let a plugdev user drive the CMSIS-DAP probe without root.
# Second, give each device a stable name: ttyACM numbering follows connection
# order, so ttyACM0/ttyACM1 swap between the command station's probe and the
# layout-feedback Pico depending on what enumerated first.
#
# The serial numbers below are specific to this bench. Replacing a device means
# updating its rule - find the new serial with:
#     udevadm info -q property -n /dev/ttyACM0 | grep ID_SERIAL_SHORT
sudo tee "$UDEV_RULES" >/dev/null <<'RULES'
# PicoDCC bench devices. Managed by scripts/provision-bench.sh - edit there, not here.

# --- Raspberry Pi Debugprobe (CMSIS-DAP) wired to the PicoDCC Pico ---
# Raw USB + HID access so OpenOCD runs without sudo.
SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000c", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0660", GROUP="plugdev", TAG+="uaccess"
# The probe's CDC interface is the DCC-EX channel to the command station.
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", SYMLINK+="picodcc-dccex"

# --- RP2 in BOOTSEL mode, so picotool works without root ---
SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="0003", MODE="0660", GROUP="plugdev", TAG+="uaccess"

# --- Second Pico: layout feedback (sensors, point position, MQTT over Ethernet) ---
# Matched on serial so it can never be confused with the command station.
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{serial}=="0d9a62134acbf42d", SYMLINK+="layout-feedback"

# --- Digital oscilloscope (FTDI FT232). NOT a serial console. Do not open it as one. ---
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{serial}=="FTQC600L", SYMLINK+="layout-scope"
RULES
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty --subsystem-match=usb --subsystem-match=hidraw
echo "written $UDEV_RULES and reloaded"

say "3. Toolchain discovery"
SDK_DIR=$(newest_under "$PICO_ROOT/sdk" "pico_sdk_init.cmake") || SDK_DIR=""
TC_DIR=$(newest_under "$PICO_ROOT/toolchain" "bin/arm-none-eabi-gcc") || TC_DIR=""
OCD_DIR=$(newest_under "$PICO_ROOT/openocd" "openocd") || OCD_DIR=""
PT_DIR=$(newest_under "$PICO_ROOT/picotool" "picotool/picotool") || PT_DIR=""
CM_DIR=$(newest_under "$PICO_ROOT/cmake" "bin/cmake") || CM_DIR=""
NJ_DIR=$(newest_under "$PICO_ROOT/ninja" "ninja") || NJ_DIR=""

for pair in "SDK:$SDK_DIR" "toolchain:$TC_DIR" "openocd:$OCD_DIR" "picotool:$PT_DIR" "cmake:$CM_DIR" "ninja:$NJ_DIR"; do
    printf "  %-10s %s\n" "${pair%%:*}" "${pair#*:}"
done

if [ -z "$SDK_DIR" ] || [ -z "$TC_DIR" ]; then
    echo
    echo "ERROR: no Pico SDK or ARM toolchain under $PICO_ROOT."
    echo "Install them first - see docs/bench-machine-setup.md."
    exit 1
fi

say "4. Toolchain on PATH (fish)"
mkdir -p "$(dirname "$FISH_CONF")"
{
    echo "# PicoDCC bench toolchain. Managed by scripts/provision-bench.sh."
    echo "# Regenerate by re-running that script - do not hand-edit."
    echo "set -gx PICO_SDK_PATH $SDK_DIR"
    echo "set -gx PICO_TOOLCHAIN_PATH $TC_DIR"
    [ -n "$TC_DIR" ]  && echo "fish_add_path $TC_DIR/bin"
    [ -n "$PT_DIR" ]  && echo "fish_add_path $PT_DIR/picotool"
    [ -n "$OCD_DIR" ] && echo "fish_add_path $OCD_DIR"
    [ -n "$CM_DIR" ]  && echo "fish_add_path $CM_DIR/bin"
    [ -n "$NJ_DIR" ]  && echo "fish_add_path $NJ_DIR"
} > "$FISH_CONF"
echo "written $FISH_CONF"

say "5. Group membership"
for g in dialout plugdev; do
    if id -nG "$USER" | tr ' ' '\n' | grep -qx "$g"; then
        echo "$USER already in $g"
    else
        echo "adding $USER to $g (needs re-login to take effect)"
        sudo usermod -aG "$g" "$USER"
    fi
done

say "6. Verify"
# Capture rather than pipe to head: under `set -o pipefail` the SIGPIPE that
# head sends back to a still-writing tool aborts the whole script.
first_line() {
    local out
    out=$("$@" 2>&1) || true
    printf '%s\n' "${out%%$'\n'*}"
}
[ -n "$OCD_DIR" ] && first_line "$OCD_DIR/openocd" --version
[ -n "$PT_DIR" ]  && first_line "$PT_DIR/picotool/picotool" version
first_line "$TC_DIR/bin/arm-none-eabi-gcc" --version
first_line "$TC_DIR/bin/arm-none-eabi-gdb" --version

echo
echo "RP2350 OpenOCD target config:"
if [ -n "$OCD_DIR" ] && [ -e "$OCD_DIR/scripts/target/rp2350.cfg" ]; then
    echo "  present at $OCD_DIR/scripts/target/rp2350.cfg"
else
    echo "  MISSING - this OpenOCD build cannot drive an RP2350"
fi

echo
echo "stable device names:"
for d in /dev/picodcc-dccex /dev/layout-feedback /dev/layout-scope; do
    if [ -e "$d" ]; then
        printf "  %-22s -> %s\n" "$d" "$(readlink "$d")"
    else
        printf "  %-22s absent (device not plugged in?)\n" "$d"
    fi
done

say "Done"
echo "Nothing here touched the board. Flashing and debugging are separate,"
echo "and per CLAUDE.md need track power confirmed first."
