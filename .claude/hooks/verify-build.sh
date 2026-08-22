#!/usr/bin/env bash
# Stop hook: before Claude reports finished, prove the test build still compiles
# and the suite still passes. Exit 2 (with output) blocks the stop and feeds the
# failure back, so a broken build can never be reported as done.
#
# Deliberately a Stop hook rather than PostToolUse: one incremental build+ctest
# per turn (~1s) instead of one per file edit, and no spurious wake-ups partway
# through a multi-file change that is not meant to compile yet.
#
# Everything explanatory goes to STDERR. A Stop hook's stdout is not surfaced,
# so a failure reported on stdout blocks the turn with no visible reason — an
# unbreakable loop showing only "No stderr output".
#
# This hook must never exit 0 for a reason other than "the tests really passed"
# or "there is nothing built to test, and I said so". Every other path is loud.
# See issue #25: a gate whose silence is indistinguishable from success is worse
# than no gate at all.

set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

BUILD_DIR=build/host
CACHE="$BUILD_DIR/CMakeCache.txt"

# --- Host toolchain on PATH -------------------------------------------------
#
# Git Bash puts its own mingw64/bin near the front of PATH, and that directory
# ships zlib1.dll and libwinpthread-1.dll. Those shadow the MSYS2 copies that
# cc1.exe links against, so the compiler dies at DLL load with
# STATUS_ENTRYPOINT_NOT_FOUND — which surfaces as a FAILED ninja edge carrying
# no compiler diagnostic at all. The same build run from PowerShell succeeds,
# which is what makes it look intermittent and unreal.
#
# So: put the real host toolchain ahead of Git's copies before building.
# Override with PICODCC_HOST_TOOLCHAIN_BIN if the toolchain lives elsewhere.
HOST_BIN="${PICODCC_HOST_TOOLCHAIN_BIN:-/c/msys64/ucrt64/bin}"
[ -d "$HOST_BIN" ] && PATH="$HOST_BIN:$PATH" && export PATH

# --- Nothing to verify ------------------------------------------------------
#
# Not configured yet: the model has not been asked to build. This is the one
# legitimate quiet path, and it still says so — "no output" must not be able to
# mean two different things.
if [ ! -f "$CACHE" ]; then
  echo "verify-build: $BUILD_DIR is not configured, so nothing was verified." >&2
  echo "              Run 'cmake --preset host' if this work needed testing." >&2
  exit 0
fi

# --- The tree must be what it claims to be ----------------------------------
#
# build/host is the test tree by definition; a cache saying otherwise means
# something reconfigured it and the binaries cannot be trusted. Fail loudly
# rather than testing whatever happens to be lying there.
if ! grep -q '^TEST_BUILD:BOOL=ON' "$CACHE"; then
  {
    echo "$BUILD_DIR is configured with TEST_BUILD != ON — refusing to report a result."
    echo "This tree is supposed to be the host test build. Reconfigure it:"
    echo "    cmake --preset host"
  } >&2
  exit 2
fi

# --- Serialise against any other build in this tree -------------------------
#
# The hook fires at the end of a turn, which can overlap a build an agent or a
# terminal already has running in the same directory. Two ninja processes in one
# build dir corrupt each other's work. mkdir is atomic, so it works as a lock.
LOCK="$BUILD_DIR/.verify-build.lock"
for _ in $(seq 1 60); do
  if mkdir "$LOCK" 2>/dev/null; then
    trap 'rmdir "$LOCK" 2>/dev/null' EXIT
    break
  fi
  # A lock older than 10 minutes is a crashed run, not a live one.
  if [ -n "$(find "$LOCK" -maxdepth 0 -mmin +10 2>/dev/null)" ]; then
    rmdir "$LOCK" 2>/dev/null
  fi
  sleep 1
done

if ! build_out=$(cmake --build --preset host 2>&1); then
  {
    echo "Test build is broken — do not report this work as complete."
    echo "--- cmake --build --preset host ---"
    echo "$build_out" | tail -40
    # A FAILED edge with no compiler message is the PATH collision described at
    # the top of this file, not a code error. Say so rather than sending the
    # model hunting through source for a bug that is not there.
    if echo "$build_out" | grep -q '^FAILED:' && ! echo "$build_out" | grep -qE 'error:|Error [0-9]'; then
      echo
      echo "NOTE: a FAILED edge carrying no compiler diagnostic usually means the"
      echo "compiler could not load its DLLs, not that the code is wrong. Check"
      echo "that $HOST_BIN precedes Git's mingw64/bin on PATH."
    fi
  } >&2
  exit 2
fi

if ! test_out=$(ctest --preset host 2>&1); then
  {
    echo "Unit tests are failing — do not report this work as complete."
    echo "--- ctest --preset host ---"
    echo "$test_out" | tail -60
    # 0xc0000139 is a DLL resolution failure, not a test failure: the binaries
    # cannot load their C++ runtime. Check PATH before hunting for a code bug.
    case "$test_out" in
      *c0000139*)
        echo
        echo "NOTE: 0xc0000139 is STATUS_ENTRYPOINT_NOT_FOUND — the test binaries"
        echo "cannot load their C++ runtime, so this is an environment problem and"
        echo "not a broken test. ctest replaces PATH per test from the ENVIRONMENT"
        echo "property set in test/CMakeLists.txt; the compiler's own bin directory"
        echo "must be prepended there. Reconfiguring from a different shell can"
        echo "reintroduce this, because that PATH is a configure-time snapshot."
        ;;
    esac
  } >&2
  exit 2
fi

exit 0
