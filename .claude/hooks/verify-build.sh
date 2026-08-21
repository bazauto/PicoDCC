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

set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

CACHE=build/CMakeCache.txt

# Nothing configured yet — the model has not been asked to build. Stay quiet.
[ -f "$CACHE" ] || exit 0

# build/ is shared between modes. If it is currently configured for hardware,
# an incremental build here would kick off an ARM cross-build; not our job.
grep -q '^TEST_BUILD:BOOL=ON' "$CACHE" || exit 0

# NOTE: setting PATH here has no effect on the tests. ctest replaces PATH per
# test from the ENVIRONMENT property that test/CMakeLists.txt bakes in at
# configure time. If the tests cannot load their runtime, fix it there, not here.

if ! build_out=$(cmake --build build 2>&1); then
  {
    echo "Test build is broken — do not report this work as complete."
    echo "--- cmake --build build ---"
    echo "$build_out" | tail -40
  } >&2
  exit 2
fi

if ! test_out=$(cd build && ctest --output-on-failure 2>&1); then
  {
    echo "Unit tests are failing — do not report this work as complete."
    echo "--- ctest --output-on-failure ---"
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
