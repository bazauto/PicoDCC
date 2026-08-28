#!/usr/bin/env bash
# Send a DCC-EX command to the command station and capture the reply.
#
#   bash scripts/bench.sh dccex '<s>'                  # status
#   bash scripts/bench.sh dccex '<#>'                  # cab slots
#   bash scripts/bench.sh dccex --repeat 200 '<#>'     # UART loss test (#6)
#   bash scripts/bench.sh dccex --force '<1>'          # POWERS THE TRACK
#
# Invoke it through bench.sh rather than by hand: bench.sh prepends
# scripts/bench-orchestrator.sh, which is where orchestrator_guard comes from.
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
# --repeat N sends the command N times back to back at line rate and counts the
# replies. That is the measurement #6 asks for, from the outside: uart0 RX has no
# flow control, the 32-byte FIFO holds ~2.8ms at 115200, and core 0 shares its
# loop with LVGL rendering and an SPI flush to the LCD. If a single pass of
# display.loop() ever runs longer than that window, received characters are
# dropped on the floor with nothing in the DCC-EX framing to notice. Sending a
# burst and counting what comes back measures the outcome directly, with no
# firmware instrumentation and no guessing about which pass was slow.
#
# Use it with <#>, whose reply is short and which cannot change any state. Do it
# once with the display idle and again during a screen transition and a touch
# interaction -- those are the passes that would blow the window.
#
# Uses python3 rather than picocom, which is not installed on the bench, and
# rather than a bare `echo > /dev/...`, which cannot read the reply back or
# apply a timeout. Only the standard library is used -- no pyserial dependency.
set -uo pipefail

PORT="${PORT:-/dev/picodcc-dccex}"
BAUD="${BAUD:-115200}"
TIMEOUT="${TIMEOUT:-2.0}"
REPEAT=1
PACE=0

FORCE=0
CMD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --force) FORCE=1; shift ;;
        --port) PORT="${2:-}"; shift 2 ;;
        --timeout) TIMEOUT="${2:-}"; shift 2 ;;
        --repeat) REPEAT="${2:-}"; shift 2 ;;
        --pace) PACE="${2:-}"; shift 2 ;;
        *) CMD="$1"; shift ;;
    esac
done

ok()  { printf "  ok    %-26s %s\n" "$1" "$2"; }
bad() { printf "  FAIL  %-26s %s\n" "$1" "$2"; }

[ -n "$CMD" ] || { echo "usage: bench-dccex.sh [--force] [--repeat N] '<s>'" >&2; exit 2; }

case "$REPEAT" in
    ''|*[!0-9]*) bad "repeat" "--repeat needs a positive integer, got '$REPEAT'"; exit 2 ;;
esac
[ "$REPEAT" -ge 1 ] || { bad "repeat" "--repeat must be at least 1"; exit 2; }
[ "$REPEAT" -le 10000 ] || { bad "repeat" "--repeat capped at 10000"; exit 2; }

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
#
# --repeat does not widen this. A burst of a gated command is still gated, and
# repeating one is a worse idea than sending it once, not a better one.
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

[ "$REPEAT" -eq 1 ] || ok "repeat" "$REPEAT copies back to back at $BAUD"

# --- Port ------------------------------------------------------------------

[ -e "$PORT" ] || { bad "port" "$PORT missing -- re-run scripts/provision-bench.sh"; exit 1; }

# One DCC-EX channel, several things that want it, and the orchestrator holds it
# open for the life of the service. Stop it first and restart it on the way out
# -- a probe that fails with "port busy" reads like a firmware fault and is not.
orchestrator_guard || true

# Anything still holding the port is not the orchestrator, so it is not ours to
# stop. Report it rather than guess.
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
python3 - "$PORT" "$BAUD" "$CMD" "$TIMEOUT" "$REPEAT" "$PACE" <<'PY'
import os, select, sys, termios, time

port, baud, cmd, timeout, repeat, pace = (
    sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4]),
    int(sys.argv[5]), float(sys.argv[6]) / 1000.0)

wire = (cmd + "\n").encode()


def configure(fd):
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


def take_frames(buf):
    """Split a read buffer into complete <...> frames plus the trailing remainder.

    Framing is the only structure DCC-EX gives us -- there is no sequence number
    and no checksum -- so a frame count is the whole measurement.
    """
    frames = []
    while True:
        i = buf.find(b'<')
        if i < 0:
            return frames, b''
        j = buf.find(b'>', i + 1)
        if j < 0:
            return frames, buf[i:]
        frames.append(buf[i:j + 1])
        buf = buf[j + 1:]


QUIET = 0.75   # seconds of silence, after the last byte is written, that ends a run


def pump(fd, payload, deadline, want=None, pace=0.0, unit=0):
    """Write payload while draining replies. Returns (frames, trailing bytes).

    Reads and writes are interleaved rather than sequenced: at 115200 a long
    burst can fill the receive buffer while we are still writing, and losing
    bytes to our own end would be indistinguishable from the firmware losing
    them, which is the entire thing being measured.

    A run ends when every expected frame has arrived, or after QUIET seconds of
    silence once everything has been written, or at the deadline. The silence
    window has to be generous: ending a run the moment one select times out
    scores a slow reply as a lost one, which is the easiest way for this tool to
    manufacture the defect it is supposed to be measuring.

    pace/unit throttle the send. With unit set to one command's length, a chunk
    of that size goes out every `pace` seconds, which turns a pass/fail into a
    threshold -- the rate at which loss begins is the number worth knowing, and
    a bridge that saturates on raw byte rate behaves differently from a core 0
    loop that occasionally runs long.
    """
    frames, buf, sent = [], b'', 0
    chunk_size = unit if unit else 4096
    next_write = time.time()
    last_rx = time.time()

    while time.time() < deadline:
        if want is not None and len(frames) >= want:
            break

        now = time.time()
        pending = sent < len(payload)
        may_write = pending and now >= next_write
        wlist = [fd] if may_write else []

        # Sleep exactly until the next write is due, never a fixed poll interval.
        # A flat cap here silently becomes the floor on --pace: the send rate
        # collapses to one command per poll, the run hits its deadline still
        # holding unsent commands, and every one of those is then counted as a
        # dropped reply. That reads as catastrophic loss that gets worse as you
        # slow the burst down, which is the exact opposite of how a FIFO
        # overflows, and it is the tool measuring itself.
        if pending and not may_write:
            budget = max(0.0, min(next_write - now, deadline - now))
        else:
            budget = max(0.0, min(0.05, deadline - now))

        r, w, _ = select.select([fd], wlist, [], budget)

        if w:
            try:
                n = os.write(fd, payload[sent:sent + chunk_size])
                sent += n
                if pace:
                    next_write = time.time() + pace
            except BlockingIOError:
                pass

        if r:
            data = os.read(fd, 4096)
            if data:
                buf += data
                last_rx = time.time()
                new, buf = take_frames(buf)
                frames.extend(new)

        if sent >= len(payload) and (time.time() - last_rx) > QUIET:
            break

    return frames, buf, sent


fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    configure(fd)

    # ---- Single command: the post-deploy health check -----------------------
    if repeat == 1:
        os.write(fd, wire)
        print("  sent      %s" % cmd)

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

        # A diagnostic leaking onto the command UART desynchronises JMRI, so a
        # reply that is not <...>-framed is a defect worth naming rather than
        # just printing.
        stray = [l.strip() for l in text.splitlines()
                 if l.strip() and not (l.strip().startswith("<") and l.strip().endswith(">"))]
        if stray:
            print()
            print("  WARNING: unframed output on the command UART:")
            for l in stray:
                print("           %r" % l)
            print("           DCCEX_RESPONSE() is for protocol replies only -- diagnostics")
            print("           belong in the log buffer (lib/pico_diagnostic.h).")
        sys.exit(0)

    # ---- Burst: the #6 loss measurement ------------------------------------
    #
    # How many frames one command answers with is a property of the opcode --
    # <#> replies with one, <s> with three -- so it is measured rather than
    # assumed. Everything after this is counted against that baseline.
    probe, _, _ = pump(fd, wire, time.time() + timeout, want=None)
    if not probe:
        print("  reply     (none within %.1fs)" % timeout)
        print()
        print("  No answer to the calibration command, so there is nothing to count")
        print("  a burst against. Check the firmware is running before retrying.")
        sys.exit(1)

    per_reply = len(probe)
    print("  calibrate %s answers with %d frame(s): %s"
          % (cmd, per_reply, b' '.join(probe).decode('ascii', 'replace')))

    if any(f.startswith(b'<X') for f in probe):
        print()
        print("  The firmware rejects this command, so a burst of it measures nothing.")
        print("  Use an opcode it accepts -- <#> is the intended one.")
        sys.exit(1)

    expected = repeat * per_reply
    payload = wire * repeat

    # Wire time for the burst, plus the caller's timeout as slack. Without this
    # a large --repeat would be judged against a 2s deadline it could never meet
    # and every run would look like catastrophic loss.
    est_in = expected * max(len(f) for f in probe)
    wire_seconds = (len(payload) + est_in) * 10.0 / baud
    deadline = time.time() + wire_seconds + timeout

    if pace:
        deadline += repeat * pace

    termios.tcflush(fd, termios.TCIOFLUSH)
    started = time.time()
    frames, leftover, written = pump(fd, payload, deadline, want=expected,
                                    pace=pace, unit=len(wire) if pace else 0)
    elapsed = time.time() - started
finally:
    os.close(fd)

# The run is only a measurement if every command actually went out. Anything
# left unsent is the harness falling behind, and counting it as a missing reply
# would blame the firmware for the tool's own shortfall.
if written < len(payload):
    print("  ABORTED   sent only %d of %d bytes before the deadline"
          % (written, len(payload)))
    print()
    print("  This is a harness failure, not a firmware result. The burst could not")
    print("  be delivered in the time allowed, so the replies that never came back")
    print("  were for commands that were never sent. Raise --timeout, or lower")
    print("  --repeat, and re-run. Do not read anything into it.")
    sys.exit(2)

rejected = [f for f in frames if f.startswith(b'<X')]
received = len(frames)
lost = expected - received

print("  sent      %d x %s (%d bytes)" % (repeat, cmd, len(payload)))
print("  received  %d of %d expected frames in %.2fs (%.0f commands/s)"
      % (received, expected, elapsed, repeat / elapsed if elapsed else 0))

if rejected:
    print("  rejected  %d frame(s) came back as <X>" % len(rejected))
if leftover.strip():
    print("  partial   trailing unframed bytes: %r" % leftover.strip()[:80])

print()

if lost <= 0 and not rejected and not leftover.strip():
    print("  NO LOSS. %d commands sent back to back at %d baud, %d replies, none"
          % (repeat, baud, received))
    print("  rejected and nothing truncated.")
    print()
    print("  That is the #6 measurement: core 0 drained uart0 fast enough to keep")
    print("  the 32-byte FIFO from overflowing for the whole burst. Re-run it during")
    print("  a screen transition and a touch interaction before concluding, since")
    print("  those are the passes long enough to blow the ~2.8ms window.")
    sys.exit(0)

print("  LOSS DETECTED.")
if lost > 0:
    print("    %d frame(s) missing -- characters were dropped, and a truncated" % lost)
    print("    command either failed validation or merged with the next one.")
if rejected:
    print("    %d command(s) answered <X> -- corrupted in transit but caught by" % len(rejected))
    print("    the validator, which is the benign half of the failure mode.")
print()
print("  #6 is real at this rate. The next step is attribution: instrument the")
print("  interval between PicoDccEx::processCommand() entries, and the duration")
print("  of PicoDCCDisplay::loop(), and find which pass exceeds 2.8ms.")
sys.exit(1)
PY
RC=$?
echo
[ $RC -eq 0 ] && echo "PROBE OK" || echo "PROBE FAILED"
exit $RC
