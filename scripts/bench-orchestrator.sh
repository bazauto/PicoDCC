#!/usr/bin/env bash
# Stop and restart the layout-orchestrator service around a board-touching
# operation. Sourced, not run: scripts/bench.sh prepends this to whichever
# bench-*.sh script it is piping to the bench machine, so there is one copy
# rather than one per caller.
#
# Why this exists. /dev/picodcc-dccex is a single exclusive channel and the
# orchestrator holds it open for the life of the service. A `dccex` probe
# against a held port fails with "port busy", which reads like a firmware fault
# and is not; a `flash` resets the board underneath a connection the
# orchestrator thinks is live. Both wanted the service out of the way first, and
# doing that by hand is exactly the kind of step that gets skipped.
#
# Two rules govern the restart, and both matter:
#
#   - Only restart what we stopped. If the service was already inactive --
#     because Paul stopped it deliberately to work on something -- we leave it
#     alone. That is also the escape hatch: stop it yourself and these scripts
#     will not start it behind you.
#
#   - Always restart, including on failure. orchestrator_guard installs an EXIT
#     trap, so a preflight that bails, an openocd error or a Ctrl-C all still put
#     the layout's controller back. A silent failure here leaves the layout with
#     no controller, so a failed restart is reported loudly with the manual
#     command rather than swallowed.

ORCH_UNIT="${ORCH_UNIT:-layout-orchestrator.service}"
ORCH_PORT="${ORCH_PORT:-/dev/picodcc-dccex}"
ORCH_WAS_ACTIVE=0
ORCH_STOP_TIMEOUT="${ORCH_STOP_TIMEOUT:-10}"

# Own column formatting rather than borrowing the calling script's ok()/bad().
# This file is prepended, so those are not defined yet at parse time, and the
# coupling would be invisible until someone reordered the concatenation.
_orch_ok()   { printf "  ok    %-26s %s\n" "$1" "$2"; }
_orch_note() { printf "  --    %-26s %s\n" "$1" "$2"; }
_orch_bad()  { printf "  FAIL  %-26s %s\n" "$1" "$2"; }

# Stop the service if it is running, and wait for it to let go of the port.
# Returns non-zero only when the service is running and could not be stopped --
# the caller decides whether that is fatal.
orchestrator_stop() {
    if ! systemctl list-unit-files "$ORCH_UNIT" >/dev/null 2>&1; then
        _orch_note "orchestrator" "$ORCH_UNIT not installed -- nothing to stop"
        return 0
    fi

    if ! systemctl is-active --quiet "$ORCH_UNIT"; then
        _orch_note "orchestrator" "already stopped -- left alone, and not restarted after"
        return 0
    fi

    if ! sudo -n true 2>/dev/null; then
        _orch_bad "orchestrator" "$ORCH_UNIT is running and passwordless sudo is unavailable"
        echo "          Stop it yourself, then re-run:  sudo systemctl stop $ORCH_UNIT"
        return 1
    fi

    if ! sudo -n systemctl stop "$ORCH_UNIT" 2>/dev/null; then
        _orch_bad "orchestrator" "failed to stop $ORCH_UNIT"
        return 1
    fi

    # systemctl stop returns once the main process is gone, which closes its
    # descriptors -- but a forked child or a slow USB CDC teardown can hold the
    # node open a moment longer, and that is precisely the race that would make
    # the following probe fail intermittently. Wait for the port to be free.
    local waited=0
    while [ -e "$ORCH_PORT" ] && fuser "$ORCH_PORT" >/dev/null 2>&1; do
        [ "$waited" -ge "$ORCH_STOP_TIMEOUT" ] && {
            _orch_bad "orchestrator" "stopped, but $ORCH_PORT is still held after ${waited}s"
            fuser -v "$ORCH_PORT" 2>&1 | sed 's/^/          /'
            return 1
        }
        sleep 1
        waited=$((waited + 1))
    done

    ORCH_WAS_ACTIVE=1
    _orch_ok "orchestrator" "stopped $ORCH_UNIT (restarted when this finishes)"
    return 0
}

# Restart only if orchestrator_stop stopped it. Safe to call more than once.
orchestrator_restore() {
    [ "$ORCH_WAS_ACTIVE" = "1" ] || return 0
    ORCH_WAS_ACTIVE=0

    echo
    if sudo -n systemctl start "$ORCH_UNIT" 2>/dev/null && \
       systemctl is-active --quiet "$ORCH_UNIT"; then
        _orch_ok "orchestrator" "restarted $ORCH_UNIT"
        return 0
    fi

    # Loud, and with the command to fix it. The layout has no controller until
    # someone acts on this, so it must not look like a footnote.
    _orch_bad "orchestrator" "FAILED to restart $ORCH_UNIT -- the layout has no controller"
    echo "          Start it by hand:  sudo systemctl start $ORCH_UNIT"
    systemctl status "$ORCH_UNIT" --no-pager --lines=5 2>&1 | sed 's/^/          /'
    return 1
}

# Stop the service and arm the restart. Call once, early, from any script that
# resets, halts or opens the serial port on the board.
orchestrator_guard() {
    trap 'orchestrator_restore' EXIT
    orchestrator_stop
}
