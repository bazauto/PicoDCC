#!/usr/bin/env bash
# Single entry point from the dev machine to the PicoDCC bench machine.
#
#   bash scripts/bench.sh inventory              # read-only, touches nothing
#   bash scripts/bench.sh dry-run                # flash preflight, writes nothing
#   bash scripts/bench.sh flash --expect <sha>   # TOUCHES THE BOARD
#   bash scripts/bench.sh fault                  # TOUCHES THE BOARD (halts both cores)
#   bash scripts/bench.sh config                 # TOUCHES THE BOARD (halts to read flash)
#   bash scripts/bench.sh dccex '<s>'            # TOUCHES THE BOARD (serial command)
#   bash scripts/bench.sh dccex --repeat 200 '<#>'  # TOUCHES THE BOARD (UART loss test, #6)
#
# Exists so the ssh incantation lives in one place and so each operation is a
# short, distinct command line. That second point is what makes the permission
# rules precise: `inventory` can be allowlisted on its own, while every
# board-touching subcommand keeps prompting. Do not collapse this into a single
# wildcard rule -- the separation is the safety boundary.
#
# The login shell on the bench is fish, so scripts are piped to bash explicitly;
# bash syntax passed as an ssh command string fails with a parse error there.
#
# scripts/bench-orchestrator.sh is prepended to every script sent over, so the
# stop/restart of layout-orchestrator.service lives in one file rather than being
# copied into each caller. It only defines functions, so prepending it to a
# script that does not use them costs nothing.
set -uo pipefail

BENCH="${BENCH:-pbarrett@172.18.10.240}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10)

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

PRELUDE="bench-orchestrator.sh"

run() {
    local script="$1"; shift
    [ -f "$ROOT/scripts/$script" ] || { echo "missing: scripts/$script" >&2; exit 1; }
    [ -f "$ROOT/scripts/$PRELUDE" ] || { echo "missing: scripts/$PRELUDE" >&2; exit 1; }
    if [ $# -gt 0 ]; then
        # ssh joins its command arguments into one string that the *remote login
        # shell* parses -- fish here. A DCC-EX command like <s> is a redirection
        # to fish, which fails before bash -s ever runs. Single-quote each
        # argument so fish passes them through untouched. An argument containing
        # a single quote cannot be quoted this way, and nothing we send needs
        # one, so reject it rather than emit a mis-quoted command line.
        local quoted="bash -s --" arg
        for arg in "$@"; do
            case $arg in
                *"'"*)
                    echo "bench.sh: single quotes are not supported in arguments: $arg" >&2
                    exit 2 ;;
            esac
            quoted="$quoted '$arg'"
        done
        cat "$ROOT/scripts/$PRELUDE" "$ROOT/scripts/$script" \
            | ssh "${SSH_OPTS[@]}" "$BENCH" "$quoted"
    else
        cat "$ROOT/scripts/$PRELUDE" "$ROOT/scripts/$script" \
            | ssh "${SSH_OPTS[@]}" "$BENCH" bash -s
    fi
}

CMD="${1:-}"; shift || true

case "$CMD" in
    inventory) run bench-inventory.sh ;;
    dry-run)   run bench-flash.sh --dry-run "$@" ;;
    flash)     run bench-flash.sh "$@" ;;
    fault)     run bench-debug.sh fault "$@" ;;
    config)    run bench-debug.sh config "$@" ;;
    dccex)     run bench-dccex.sh "$@" ;;
    provision) run provision-bench.sh ;;
    *)
        cat >&2 <<'USAGE'
usage: bash scripts/bench.sh <command>

  safe (never touches the board)
    inventory              probe, devices, port contention, toolchain, staged image
    dry-run                flash preflight only
    provision              re-run host-side bench provisioning

  touches the board -- track power must be off
    (flash and dccex stop layout-orchestrator.service first and restart it after)
    flash --expect <sha>   program over SWD, verify config sector survives
    fault [--no-resume]    halt, registers + backtrace both cores, resume
    config [--no-resume]   read and decode the stored configuration
    dccex [--force] '<s>'  send a DCC-EX command, capture the reply
    dccex --repeat N '<#>' send N back to back, count replies -- the #6 loss test

Build and stage first with: pwsh -NoProfile -File scripts/Deploy-Firmware.ps1
USAGE
        exit 2 ;;
esac
