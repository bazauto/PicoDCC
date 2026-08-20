#!/usr/bin/env bash
# Stop hook: before Claude reports finished, prove the test build still compiles
# and the suite still passes. Exit 2 (with output) blocks the stop and feeds the
# failure back, so a broken build can never be reported as done.
#
# Deliberately a Stop hook rather than PostToolUse: one incremental build+ctest
# per turn (~1s) instead of one per file edit, and no spurious wake-ups partway
# through a multi-file change that is not meant to compile yet.

set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

CACHE=build/CMakeCache.txt

# Nothing configured yet — the model has not been asked to build. Stay quiet.
[ -f "$CACHE" ] || exit 0

# build/ is shared between modes. If it is currently configured for hardware,
# an incremental build here would kick off an ARM cross-build; not our job.
grep -q '^TEST_BUILD:BOOL=ON' "$CACHE" || exit 0

build_out=$(cmake --build build 2>&1)
if [ $? -ne 0 ]; then
  echo "Test build is broken — do not report this work as complete."
  echo "--- cmake --build build ---"
  echo "$build_out" | tail -40
  exit 2
fi

test_out=$(cd build && ctest --output-on-failure 2>&1)
if [ $? -ne 0 ]; then
  echo "Unit tests are failing — do not report this work as complete."
  echo "--- ctest --output-on-failure ---"
  echo "$test_out" | tail -60
  exit 2
fi

exit 0
