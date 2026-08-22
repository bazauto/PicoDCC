#!/usr/bin/env bash
# Send a DCC-EX command to the command station and capture the reply.
#
#   ssh <bench> bash -s -- '<s>'          < scripts/bench-dccex.sh   # status
#   ssh <bench> bash -s -- '<#>'          < scripts/bench-dccex.sh   # cab slots
#   ssh <bench> bash -s -- --force '<1>'  < scripts/bench-dccex.sh   # POWERS THE TRACK
#
# THIS TOUCHES THE BOARD. Not on the Claude Code allowlist -- it prompts every
# time, by design.
#
# By default only commands that cannot energise the track or move a locomotive
# are accepted; anything else needs --force. That guard is the point of this
# script over a raw terminal: <1> powers the main track and <t 3 100 1> puts a
# locomotive at speed 100, and neither should be one typo away during what is
# meant to be a post-deploy health check.
#
# Uses python3 rather than picocom, which is not installed on the bench, and
# rather than a bare `echo > /dev/...`, which cannot read the reply back or
# apply a timeout. Only the standard library is used -- no pyserial dependency.
set -uo pipefail

PORT="${PORT:-/dev/picodcc-dccex}"
BAUD="${BAUD:-115200}"
TIMEOUT="${TIMEOUT:-2.0}"

FORCE=0
CMD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --force) FORCE=1; shift ;;
        --port) PORT="${2:-}"; shift 2 ;;
        --timeout) TIMEOUT="${2:-}"; shift 2 ;;
        *) CMD="$1"; shift ;;
    esac
done

ok()  { printf "  ok    %-26s %s\n" "$1" "$2"; }
bad() { printf "  FAIL  %-26s %s\n" "$1" "$2"; }

[ -n "$CMD" ] || { echo "usage: bench-dccex.sh [--force] '<s>'" >&2; exit 2; }

echo
echo "=== DCC-EX probe ==="
echo

# --- Safety guard ----------------------------------------------------------
#
# Opcode is the first character inside the angle brackets. These four cannot
# start a train moving:
#   <s>  status      <#>  cab slot count
#   <0>  power OFF   <!>  emergency stop
# Everything else -- <1> power on, <t> throttle, <F> function, <a> accessory,
# and <E> which writes flash and blocks both cores for ~410ms -- is gated.
OPCODE=$(printf '%s' "$CMD" | sed -n 's/^<\s*\([^>]\).*/\1/p')
if [ -z "$OPCODE" ]; then
    bad "command" "does not look like a DCC-EX command: $CMD (expected <...>)"
    exit 2
fi

case "$OPCODE" in
    s|'#'|0|'!')
        ok "command" "$CMD (cannot energise track or move a loco)" ;;
    *)
        if [ "$FORCE" != "1" ]; then
            bad "command" "$CMD is gated -- it can change track or locomotive state"
            case "$OPCODE" in
                1) echo "          <1> powers the main track." ;;
                t) echo "          <t> sets locomotive speed." ;;
                F) echo "          <F> changes a function output." ;;
                a) echo "          <a> throws an accessory." ;;
                E) echo "          <E> writes flash: blocks BOTH cores ~410ms, DCC stops."
                   echo "              Legal only in LAYOUT_MAINTENANCE with main track unpowered." ;;
            esac
            echo "          Re-run with --force if that is genuinely intended."
            exit 3
        fi
        printf "  --    %-26s %s\n" "command" "$CMD (FORCED -- changes hardware state)" ;;
esac

# --- Port ------------------------------------------------------------------

[ -e "$PORT" ] || { bad "port" "$PORT missing -- re-run scripts/provision-bench.sh"; exit 1; }

# One DCC-EX channel, several things that want it. If layout-orchestrator holds
# it, the probe would otherwise fail in a way that looks like a firmware fault.
holders=$(fuser "$PORT" 2>/dev/null | tr -s ' ')
if [ -n "$holders" ]; then
    bad "port busy" "held by:"
    for pid in $holders; do
        echo "          pid $pid ($(ps -o comm= -p "$pid" 2>/dev/null || echo unknown))"
    done
    echo "          Stop it before probing -- the channel is exclusive."
    exit 1
fi
ok "port" "$PORT free at $BAUD"

echo
python3 - "$PORT" "$BAUD" "$CMD" "$TIMEOUT" <<'PY'
import os, select, sys, termios, time

port, baud, cmd, timeout = sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4])

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    speed = getattr(termios, 'B%d' % baud)

    # Raw 8N1, no flow control. CLOCAL matters: without it the open blocks on
    # carrier detect. HUPCL is cleared so closing does not toggle DTR -- on a
    # USB CDC bridge that pulse can reset whatever is on the other end.
    iflag = 0
    oflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    lflag = 0
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW,
                      [iflag, oflag, cflag, lflag, speed, speed, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)

    os.write(fd, (cmd + "\n").encode())
    print("  sent      %s" % cmd)

    # DCC-EX frames replies as <...>. Read until a frame closes, then keep
    # draining briefly -- some commands answer with more than one frame.
    deadline = time.time() + timeout
    buf = b""
    got_frame = False
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], max(0.0, deadline - time.time()))
        if not r:
            break
        chunk = os.read(fd, 4096)
        if not chunk:
            continue
        buf += chunk
        if b">" in buf:
            if not got_frame:
                got_frame = True
                deadline = min(deadline, time.time() + 0.3)
finally:
    os.close(fd)

text = buf.decode("ascii", "replace").strip()
if not text:
    print("  reply     (none within %.1fs)" % timeout)
    print()
    print("  No answer. Either the firmware is not running, it is wedged, or the")
    print("  probe's CDC interface is not bridged to the command station's uart0.")
    sys.exit(1)

for line in text.splitlines():
    line = line.strip()
    if line:
        print("  reply     %s" % line)

# A diagnostic leaking onto the command UART desynchronises JMRI, so a reply
# that is not <...>-framed is a defect worth naming rather than just printing.
stray = [l.strip() for l in text.splitlines()
         if l.strip() and not (l.strip().startswith("<") and l.strip().endswith(">"))]
if stray:
    print()
    print("  WARNING: unframed output on the command UART:")
    for l in stray:
        print("           %r" % l)
    print("           DCCEX_RESPONSE() is for protocol replies only -- diagnostics")
    print("           belong in the log buffer (lib/pico_diagnostic.h).")
PY
RC=$?
echo
[ $RC -eq 0 ] && echo "PROBE OK" || echo "PROBE FAILED"
exit $RC
