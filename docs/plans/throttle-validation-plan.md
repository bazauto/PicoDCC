# Throttle command validation — implementation plan (#11, #12, #16, #2)

Status: plan only. No production code has been written. Target branch: new branch off
`origin/main`, single PR.

---

## Goal

Close the four defects in the `<t>`/`<F>` validation surface as one change: speed `-1` must
emergency-stop a single locomotive instead of commanding full speed (#11); cab address `0`
must be refused instead of broadcasting to every decoder (#12); addresses above 10239 must be
refused instead of emitting idle/reserved packets (#16); and no malformed command may reach
`std::terminate` and abort the firmware (#2). The fix establishes one source of truth for DCC
address and speed limits (`lib/dcc_types.h`), makes `validatePacket()` the protocol gate,
replaces `PicoDccLoco`'s throwing constructor with an inert-object failure mode that reuses the
existing `isValid()` machinery, and keeps a defensive guard in `generateThrottleCommand()` so
that no code path — validated or not — can put an address outside 1..10239 on the rails.

Two further defects were found in the same lines while planning and are fixed here because the
same code is being rewritten: `<F cab func state>` currently writes the *function number* into
the loco's speed and the *function state* into its direction (so pressing F8 on a stopped loco
commands speed 8), and `getDccExCabUpdate()`'s 16-byte buffer truncates the `<l>` reply for any
address of 4 digits or more. Both need issues filed for traceability; see "Follow-up issues".

---

## Design decisions

### 1. Which core does this run on?

**Core 0, entirely.** Every function changed — `PicoDccExPacket::decodePacket`/`validatePacket`,
`PicoDccLoco`'s constructors, `update`, `updateControl`, `generateThrottleCommand`,
`PicoDccLocos::addLoco`, `PicoDccController::dccexLoop` — runs on Core 0 in the command path.

Core 1 is affected only in that it *reads* the `raw_dcc_cmd_t` these functions produce, through
`PicoDccLocos::getNextReminder()` (called from `PicoDccTrack::loop()`,
`lib/PicoDCCTrack/pico_dcctrack.cpp:171`). No new Core 1 code, no new work inside the Core 1
critical section, and no change to reminder pacing. The one new Core 1-visible behaviour is
that a locomotive may now hold an **emergency-stop** throttle command as its steady state, which
Core 1 will repeat as its reminder until the throttle sends a new speed. That is correct and
deliberate — a decoder that missed the estop gets it again on the next refresh, which is what
DCC-EX itself does.

One invariant Core 1 depends on: `PicoDccTrack::sendCommand()` has no guard against
`cmd.length == 0` (`lib/PicoDCCTrack/pico_dcctrack.cpp:194`) — it would transmit a 1-byte
garbage packet. `getNextReminder()` must therefore never return `true` with a zero-length
command. Step 3 adds that guard explicitly alongside the existing `isValid()` check.

### 2. What shared state does it touch, and where is the semaphore acquired?

The shared state is `PicoDccLocos::locos` (the `std::vector<PicoDccLoco>`, written by Core 0,
read by Core 1) and, transitively, each `PicoDccLoco::cmd`. `locos_lock` is the semaphore.

Named critical sections after the change:

- `PicoDccLocos::addLoco()` — construction and validation of the candidate `PicoDccLoco` happen
  **outside** the lock (they touch no shared state). The lock is acquired immediately before the
  `locos.size()` capacity check and released after `push_back()` and the `cmd` copy. The
  capacity check is inside the lock, not before it: per CLAUDE.md rule 5, reading `size()`
  outside the lock *is* the race.
- `PicoDccLocos::updateLocoThrottle()` — unchanged. The lock already spans the search,
  `locos[i].update(packet)` and `getThrottleCommand()`. `PicoDccLoco::update()` gains
  validation, which runs inside that section; it is branch-only arithmetic, no allocation, no
  blocking, so the section does not get materially longer.
- `PicoDccLocos::getNextReminder()` — unchanged structure; the new zero-length check goes
  next to the existing `isValid()` check, inside the lock.

`PicoDccLoco::isValid()` is called from both cores. It reads one `uint16_t` member and is
called from Core 1 only from inside `getNextReminder()`'s critical section, so its redefinition
introduces no new sharing.

No new semaphore, no change to acquisition order, nothing on Core 0 acquires blocking that did
not already.

### 3. Does it allocate?

No new heap allocation and no new stack buffers.

- `PicoDccExPacket::dccex_cab_update` grows from `char[16]` to `char[24]` — a class member, and
  `PicoDccExPacket` is a stack local in `dccexLoop()` and in `sendEmergencyStopResponses()`, so
  this is +8 bytes of Core 0 stack in a function that is not deeply nested. Well inside the
  "no large stack allocations" rule; 24 bytes holds the longest possible reply
  `<l 10239 0 255 0>` (17 chars + NUL) with room to spare.
- The new `MAX_LOCO` cap in `addLoco()` **removes** an allocation hazard: `locos.reserve(50)`
  in the constructor means `push_back()` beyond 50 reallocates the vector's buffer, moving it
  while `findLoco()`-returned raw pointers may still be held. Capping at `MAX_LOCO` (which is
  what `<#>` advertises) means the reserved buffer is never outgrown.
- No `strncpy()` is introduced. No struct is assigned across cores; the existing
  `cmd = newLoco.getThrottleCommand()` in `addLoco()` is Core 0 writing a Core 0 local and is
  left alone.

### 4. Does it write flash?

**No.** Nothing here is persisted, no `PicoConfigStorage` call is added or moved, and
`OperationMode::LAYOUT_MAINTENANCE` is not touched. The maintenance-mode lockout in
`dccexLoop()` (throttle and accessory commands silently ignored) is preserved exactly as it is;
validation happens before that check and rejection is equally silent, so the two do not
interact.

### 5. How is it testable without hardware?

All of it, in CMocka, at four levels: `pico_dcc_packet_tests` (the validation gate),
`pico_dcc_loco_tests` (encoding and the non-throwing failure mode), `pico_dcc_locos_tests`
(collection-level rejection and reminders), `pico_dcc_controller_tests` (end-to-end from a UART
string, which is the path that used to abort), and `pico_dcc_wire_format_tests` (the exact
bytes). The `#2` crash path is directly testable: today `addLoco()` with a non-throttle packet
reaches `std::terminate`, which aborts the test binary; after the fix the suite runs to
completion, and that *is* the assertion.

Two things tests cannot settle, both flagged in #11's warning that the encoding was tuned
empirically against JMRI and never written down:

- **Whether real decoders act on `0x61`/`0x41` as an emergency stop.** Bench check: with the
  loco running, `<t N -1 1>`, confirm an immediate stop rather than a coasting stop, then
  `<t N 20 1>` and confirm normal control resumes. Run this in isolation from other firmware
  changes, per #11.
- **Whether JMRI stays in sync** given (a) the `<l>` reply for an estop now reports speedByte 1
  instead of 254, (b) the `<l>` reply for long addresses is no longer truncated mid-string, and
  (c) `<F>` no longer produces an `<l>` at all. Bench check: drive a loco from a JMRI throttle,
  use the estop button, press function keys, confirm the throttle's speed slider does not jump
  and the connection does not desync.

Use `bash scripts/bench.sh dccex` for both; commands that energise the track or move a
locomotive need `--force` and Paul's explicit approval first.

### 6. Does it change the UART contract?

Yes, in three ways, all of them real DCC-EX protocol replies — no diagnostics go near `uart0`:

| Change | Before | After |
|---|---|---|
| `<t cab -1 dir>` reply | `<l cab 0 254 0>` (near full speed) | `<l cab 0 129 0>` forward, `<l cab 0 1 0>` reverse — DCC-EX speedByte 1 is emergency stop |
| `<t 10239 126 1>` reply | `<l 10239 0 253 ` — truncated by the 16-byte buffer, no closing `>` | `<l 10239 0 253 0>` |
| `<F cab func state>` reply | `<l cab 0 <func-1> 0>` — reports the function number as a speed | no reply |

The 0..126 speed mapping in `getDccExCabUpdate()` (`s > 1 → s - 1`) is **left exactly as it
is**. #11 notes it disagrees with the packet encoder and suspects it is what was tuned for
JMRI; changing it is a separate change with its own bench test. Every rejected command stays
**silent** on the wire, which is the current behaviour for invalid packets
(`PicoDccEx::processCommand` drops them at `lib/PicoDCCEX/pico_dccex.cpp:88`). Issue #4 covers
reporting rejections and is out of scope — do not add `<X>` or any other new reply.

Every rejection reason is recorded with `LOG_WARNING(COMPONENT_DCCEX, ...)` or
`LOG_WARNING(COMPONENT_CONTROLLER, ...)` into the diagnostic ring buffer.

---

## Design decisions the brief asked to be resolved

### D1. Speed `-1`: reject, or encode a real emergency stop?

**Encode a real single-locomotive emergency stop.** DCC-EX semantics are unambiguous (`-1` is
estop, not a malformed speed), and refusing it would leave JMRI's estop button doing nothing —
worse than today only in the sense that today it does something catastrophic.

**The bytes.** This firmware emits the 28-step speed-and-direction instruction `01DCSSSS`,
where the 5-bit speed value is `SSSS` with `C` appended as the least significant bit. Values
0 and 1 are *stop*; values 2 and 3 are *emergency stop*; 4..31 are speed steps 1..28. So a
single-loco emergency stop is `SSSS = 0001, C = 0`, i.e. instruction byte:

```
0x41 | (forward ? 0x20 : 0x00)     // 0x61 forward, 0x41 reverse
```

This is **the same instruction byte as the existing `<!>` broadcast**, which builds
`0x00 0x41` in `PicoDccController::dccexLoop()` (`lib/PicoDCCController/pico_dcccontroller.cpp:137-138`) —
the per-loco form is that byte addressed to one locomotive, with the direction bit preserved so
the decoder knows which way to go when the throttle resumes. Staying byte-identical to the
broadcast is the point: one estop encoding in the firmware, not two.

**Interaction with stored speed and reminders.** `PicoDccLoco` stores `uint8_t speed`. Estop is
represented by the sentinel `DCC_SPEED_ESTOP = 255`, which cannot collide with a legal throttle
speed (0..126) — a `static_assert` in `lib/dcc_types.h` pins that. Choosing a sentinel rather
than adding a `bool estop` member is deliberate: `PicoDccLoco`'s copy constructor is
hand-written (`lib/PicoDCCLoco/pico_dccloco.h:37`) and a new member silently left out of it
would be uninitialised in every copy that lands in the vector. It also makes the failure mode
of the *original* bug fail-safe: a stray 255 arriving in a `uint8_t` speed parameter now stops
the locomotive instead of commanding 0x7F.

The sentinel is stored, so `getThrottleCommand()` keeps returning the estop packet, so Core 1's
round-robin reminders keep re-sending it (with `repeats = 0`, as all reminders do) until the
throttle sends a new speed. That is deliberate and matches DCC-EX. It also means the estop is
*not* self-clearing: only a subsequent `<t cab speed dir>` moves the loco again.

**Interaction with speed 0.** Note that speed 0 today encodes to `0x71`/`0x51` — value 3, which
is *also* an emergency stop (the "ignore direction" form) rather than the controlled stop
(value 0, `0x60`/`0x40`) that the DCC-EX host asked for. That is a genuine defect, and it is
**deliberately not fixed here**: it changes the most frequently sent command in the system, it
is JMRI-facing, and #11 is explicit that a change to this encoding must be bench-tested in
isolation. File it as a follow-up (see below). The consequence for this plan is that after the
fix, `<t N 0 1>` and `<t N -1 1>` both stop the locomotive but with different bytes (`0x71` vs
`0x61`); both are emergency stops until the follow-up lands. Say so in the wire-format test
comment rather than leaving a reader to discover it.

### D2. Valid ranges, and where the check belongs

- **Cab address: 1..10239.** Zero is the DCC broadcast address and is *rejected, never
  clamped* — silently retargeting a command to a different locomotive would be worse than
  refusing it (#12). 10239 is the top of the long-address space; `HIGHEST_SHORT_ADDR` (127)
  stays the short/long boundary and keeps its name and value, but moves into the shared header
  so that all four limits live together.
- **Throttle speed: 0..126, plus `-1`.** 126 is the highest value a DCC-EX host sends; `-1` is
  emergency stop. 127 and above are rejected. A useful side effect: the legacy 4-field
  `<t reg cab speed dir>` form, which this parser misreads as `cab=reg, speed=cab`, is now
  rejected outright whenever the real cab address exceeds 126 — the misinterpretation #2
  mentions largely stops being reachable.
- **Direction is deliberately *not* range-checked.** Any non-1 value means reverse, which is a
  defined state, not a safety hazard, and tightening it carries a non-zero JMRI-compat risk for
  no safety gain. Leave it.
- **Function number is deliberately not range-checked** either — after the fix, `<F>`'s
  `param1` is not used to command anything (see D5), so an out-of-range value is inert.

**Where.** Both, with a clear division of labour and no duplicated literals:

1. **The numeric limits and the predicates that apply them live in exactly one place:**
   `lib/dcc_types.h`. It is already included by `lib/PicoDCCTrack/pico_dcctrack.h`, which both
   `pico_dccexpacket.h` and `pico_dccloco.h` include, so both libraries see them with no new
   include and no new CMake dependency.
2. **`PicoDccExPacket::validatePacket()` is the protocol gate.** A command that fails it is
   never marked valid, so `PicoDccEx::processCommand()` drops it and it never reaches the
   controller, the loco collection or the queue. This is the *only* place that decides whether
   a client's command is accepted.
3. **`PicoDccLoco` re-checks as defence in depth**, using the same predicates. It has to: its
   `PicoDccLoco(uint16_t)` and `PicoDccLoco(uint16_t, uint8_t, bool)` constructors are public
   and bypass the protocol layer entirely, and `updateControl()` is public. The rule is that
   the loco layer can never be driven into a state that would emit an illegal packet, whatever
   the caller does.

### D3. Throwing: remove it

**Remove the throws.** `PicoDccLoco`'s three `throw std::invalid_argument` sites
(`lib/PicoDCCLoco/pico_dccloco.cpp:11, 17, 22`) are the hazard: with no handler anywhere on the
path (`addLoco` → `dccexLoop` → `main()`), a malformed command reaches `std::terminate` and
aborts the command station while trains are moving. Adding a `try`/`catch` would fix the abort
but keeps exception unwinding in the firmware image for something that is ordinary, expected,
attacker-reachable input.

The replacement reuses machinery that already exists rather than inventing an
`optional`/factory shape: a `PicoDccLoco` built from a bad packet sets
`address = INVALID_LOCO_ADDR`, zero speed, forward, and a zero-length `cmd`. `isValid()`
already means "this loco is real", `getNextReminder()` already erases invalid entries, and
`dccexLoop()` already refuses to queue a zero-length command. `addLoco()` becomes
`bool addLoco(...)`, returning `false` without pushing when the candidate is invalid or the
collection is full.

**What happens to a rejected command:** nothing goes on the rails, no loco is created or
modified, nothing is queued, and **nothing is written to the UART** — the status quo for
invalid input. A `LOG_WARNING` is recorded. Issue #4 owns changing that; do not design a wire
response here.

`-fno-exceptions` is **not** enabled in this PR: `std::vector::push_back` can still throw
`bad_alloc`, so removing unwinding needs its own change. Note it as a follow-up.

### D4. Masking in `generateThrottleCommand()`: defence in depth, and masking alone is not enough

Keep the validation *and* guard the generator — but note that the mask suggested in #16 does
not actually work. `((65535 >> 8) & 0x3F) | 0xC0` is `0xFF`: still the idle address. Six bits
is the width of the field, but the *usable* long-address prefix range is `0xC0`-`0xE7` (39
values), so masking cannot confine it. The generator therefore does a real range check:

```cpp
if (!isValid()) { cmd.length = 0; cmd.repeats = 0; return; }   // emit nothing at all
```

and keeps the `& 0x3F` mask as well, so the shift can never spill into the instruction bits
even if `isValid()` is ever loosened. `isValid()` is redefined from `address != INVALID_LOCO_ADDR`
to `address >= DCC_MIN_LOCO_ADDR && address <= DCC_MAX_LOCO_ADDR`, which keeps
`INVALID_LOCO_ADDR` (65535) invalid as before and additionally makes every out-of-range address
inert. Emitting nothing is the right failure: a locomotive that does not respond is a bug
report, a broadcast or an idle-address packet is a layout incident.

### D5. `<F>` must stop writing the function number into the loco's speed (new)

`PicoDccLoco::update()` currently does `speed = packet->getSpeed()` for function commands too
(`lib/PicoDCCLoco/pico_dccloco.cpp:50-56`), and `getSpeed()` returns `param1`, which for `<F>`
is the function number; `getDirection()` returns `param2`, which is the function *state*. So
`<F 3 8 1>` sets loco 3 to speed 8 forward, and `<F 3 8 0>` sets it to speed 8 reverse. JMRI
sends function commands constantly. This is the same class of defect as #11 and cannot be left
in place while rewriting these exact lines.

Fix: `update()` handles throttle commands only and returns `false` for `<F>`; the packet
constructor initialises an `<F>`-created loco to speed 0, forward. Function support itself
remains the stub it is (`updateFunct()` has an empty body) — this change makes `<F>` inert
rather than dangerous, it does not implement functions.

Because the loco no longer moves, the `<l>` reply must stop claiming it does; `dccexLoop()`
sends the cab update for throttle commands only (see the UART table above).

---

## Steps

Each step leaves `cmake --build --preset host && ctest --preset host` green.

### Step 1 — Shared limits and predicates (`lib/dcc_types.h`)

Pure refactor plus additions; no behaviour change, no test change.

1. In `lib/dcc_types.h`, after the existing `DCC_PACKET_FIRST_BYTE` block, add:

   - `#define DCC_MIN_LOCO_ADDR 1`
   - `#define DCC_MAX_LOCO_ADDR 10239` (top of the 14-bit long-address space; first packet byte
     `0xE7`)
   - `#define HIGHEST_SHORT_ADDR 127` — **moved here** from
     `lib/PicoDCCTrack/pico_dcctrack.h:36`; delete it there. `pico_dcctrack.h` includes
     `dcc_types.h` at the top, so every existing user still compiles.
   - `#define INVALID_LOCO_ADDR 65535` — **moved here** from *both*
     `lib/PicoDCCLoco/pico_dccloco.h:15` and `lib/PicoDCCLoco/pico_dcclocos.h:20`; delete both
     copies (they are duplicated today and free to drift).
   - `#define DCC_MAX_THROTTLE_SPEED 126`
   - `#define DCC_SPEED_ESTOP 255` — internal sentinel, never a wire value.
   - `static_assert(DCC_SPEED_ESTOP > DCC_MAX_THROTTLE_SPEED, "estop sentinel collides with a legal speed");`

2. Add three `static inline` predicates in the same header (C- and C++-safe; `stdbool.h` is
   already included), each with a one-line comment naming the issue they close:

   ```c
   static inline bool dcc_is_valid_loco_address(int addr);   // 1..10239                (#12, #16)
   static inline bool dcc_is_valid_throttle_speed(int speed);// -1, or 0..126           (#11)
   static inline uint8_t dcc_speed_code(int speed);          // -1 -> DCC_SPEED_ESTOP, else (uint8_t)speed
   ```

   `dcc_speed_code()` is only meaningful for a speed that passed
   `dcc_is_valid_throttle_speed()`; say so in the comment.

3. Build both presets. `ctest --preset host` must be unchanged at 158.

### Step 2 — `PicoDCCLoco`: no throws, real validation, estop encoding

**`lib/PicoDCCLoco/pico_dccloco.h`**

- Delete `#define INVALID_LOCO_ADDR` (moved in step 1).
- No member changes. The copy constructor stays as it is — confirm it still lists exactly
  `address`, `speed`, `forward`, `cmd`.

**`lib/PicoDCCLoco/pico_dccloco.cpp`**

- Remove `#include <stdexcept>`; add `#include "../pico_diagnostic.h"`. No CMake change is
  needed: `PicoDiagnostic` is already linked into the firmware executable
  (`src/CMakeLists.txt:10`) and into `TEST_LIBS` for every suite that links `PicoDCCLoco`.
- `PicoDccLoco(PicoDccExPacket *packet)` — replace all three throws. Every member is assigned
  on every path (with the throws gone, an early return would otherwise leave them
  uninitialised):
  - not a throttle or function command → `address = INVALID_LOCO_ADDR`, `speed = 0`,
    `forward = true`, then `generateThrottleCommand()` (which yields `length = 0`);
    `LOG_WARNING(COMPONENT_DCCEX, "Loco not created: unsupported opcode")`.
  - `!dcc_is_valid_loco_address(packet->getCab())` → same inert state;
    `LOG_WARNING(COMPONENT_DCCEX, "Loco not created: address out of range")`.
  - throttle command with `!dcc_is_valid_throttle_speed(packet->getSpeed())` → keep the loco
    (the address is good) but fail safe to `speed = 0`;
    `LOG_WARNING(COMPONENT_DCCEX, "Loco speed out of range, using stop")`. This branch is
    unreachable once step 3 lands; it is defence in depth for direct callers.
  - throttle command, valid → `speed = dcc_speed_code(packet->getSpeed())`,
    `forward = packet->getDirection() == 1`.
  - **function command** → `speed = 0`, `forward = true` (do *not* read `param1`/`param2`;
    see D5).
- `PicoDccLoco(uint16_t address, uint8_t speed, bool forward)` — validate: an address failing
  `dcc_is_valid_loco_address()` becomes `INVALID_LOCO_ADDR`; a `speed` that is neither
  `<= DCC_MAX_THROTTLE_SPEED` nor `DCC_SPEED_ESTOP` becomes 0, with a `LOG_WARNING` in each
  case. The delegating `PicoDccLoco(uint16_t)` needs no change.
- `update(PicoDccExPacket *packet)` — throttle commands only:
  ```cpp
  if (!packet->isThrottleCommand()) return false;              // <F> is inert (D5)
  if (!dcc_is_valid_throttle_speed(packet->getSpeed())) {
      LOG_WARNING(COMPONENT_DCCEX, "Throttle speed out of range, ignored");
      return false;
  }
  return updateControl(packet->getDirection() == 1, dcc_speed_code(packet->getSpeed()));
  ```
- `updateControl(bool _forward, uint8_t _speed)` — reject before mutating:
  ```cpp
  if (_speed > DCC_MAX_THROTTLE_SPEED && _speed != DCC_SPEED_ESTOP) {
      LOG_WARNING(COMPONENT_DCCEX, "Throttle speed out of range, ignored");
      return false;
  }
  ```
  then the existing change-detection body unchanged.
- `generateThrottleCommand()` — after the four field initialisations:
  ```cpp
  if (!isValid()) { cmd.length = 0; cmd.repeats = 0; return; }
  if (address > HIGHEST_SHORT_ADDR)
      cmd.data[cmd.length++] = ((address >> 8) & 0x3F) | 0xC0;
  cmd.data[cmd.length++] = address & 0xff;
  if (speed == DCC_SPEED_ESTOP) {
      cmd.data[cmd.length++] = 0x41 | (forward ? 0x20 : 0x00);
      return;
  }
  // unchanged 128->28 step conversion
  ```
  Comment the estop byte with its derivation and the fact that it is the `<!>` broadcast's
  instruction byte plus the direction bit.
- `isValid()` — `return address >= DCC_MIN_LOCO_ADDR && address <= DCC_MAX_LOCO_ADDR;`

**`lib/PicoDCCLoco/pico_dcclocos.h` / `.cpp`**

- Delete the duplicate `#define INVALID_LOCO_ADDR`; remove `#include <stdexcept>`; add
  `#include "../pico_diagnostic.h"`.
- `void addLoco(...)` → `bool addLoco(PicoDccExPacket *packet, raw_dcc_cmd_t &cmd)`:
  ```cpp
  PicoDccLoco newLoco(packet);              // outside the lock: no shared state
  if (!newLoco.isValid()) {
      memset(&cmd, 0, sizeof(cmd));
      LOG_WARNING(COMPONENT_DCCEX, "Loco rejected: invalid throttle command");
      return false;
  }
  sem_acquire_blocking(&locos_lock);
  if (locos.size() >= MAX_LOCO) {           // size() read inside the lock (rule 5)
      sem_release(&locos_lock);
      memset(&cmd, 0, sizeof(cmd));
      LOG_WARNING(COMPONENT_DCCEX, "Loco rejected: collection full");
      return false;
  }
  locos.push_back(newLoco);
  cmd = newLoco.getThrottleCommand();
  sem_release(&locos_lock);
  return true;
  ```
- `getNextReminder()` — after the existing `isValid()` check, add the zero-length guard inside
  the same critical section, with a comment that `PicoDccTrack::sendCommand()` has no such
  guard:
  ```cpp
  if (!locos[nextIndex].isValid() || locos[nextIndex].getThrottleCommand().length == 0) { erase; release; return false; }
  ```

**Tests changed in this step**

`test/pico_dcc_loco_tests.cpp` — remove `#include <stdexcept>`; delete
`test_invalid_packet_opcode`, `test_invalid_packet_lowaddr`, `test_invalid_packet_highadd`,
`test_invalid_packet_lowspeed`, `test_invalid_packet_highspeed` (all five encode the crash as
correct behaviour) and their registrations. Add:

| Test | Asserts |
|---|---|
| `test_non_throttle_packet_yields_inert_loco` | `"s"` → `isValid()` false, `getThrottleCommand().length == 0`, no crash |
| `test_cab_zero_yields_inert_loco` | `"t 0 10 1"` → `isValid()` false, `length == 0` |
| `test_cab_above_14_bits_yields_inert_loco` | `"t 65535 10 1"` and `"t 10240 10 1"` → `isValid()` false, `length == 0` |
| `test_highest_legal_cab_is_encoded` | `"t 10239 0 1"` → `{0xE7, 0xFF, 0x71}`, length 3 |
| `test_out_of_range_speed_falls_back_to_stop` | `"t 3 200 1"` → `isValid()` true, `{0x03, 0x71}` |
| `test_estop_packet_encodes_emergency_stop` | `"t 3 -1 1"` → `{0x03, 0x61}` length 2 repeats 3; `"t 3 -1 0"` → `{0x03, 0x41}` |
| `test_estop_survives_repeated_reads` | after `"t 3 -1 1"`, two successive `getThrottleCommand()` calls both return `0x61` (this is what Core 1 reminders read) |
| `test_estop_long_address` | `PicoDccLoco(1000)` then `updateControl(true, DCC_SPEED_ESTOP)` → `{0xC3, 0xE8, 0x61}` |
| `test_function_packet_creates_loco_at_stop` | `"F 3 8 0"` → address 3, `{0x03, 0x71}` (**not** speed 8, **not** reverse) |
| `test_function_packet_does_not_change_speed` | loco at `updateControl(true, 20)`, then `update()` with `"F 3 8 1"` → returns false, bytes unchanged (`0x64`) |
| `test_update_control_rejects_out_of_range_speed` | `PicoDccLoco(3, 20, true)`; `updateControl(true, 200)` → false, bytes unchanged |
| `test_update_control_accepts_estop_sentinel` | `updateControl(true, DCC_SPEED_ESTOP)` → true, byte `0x61` |
| `test_out_of_range_address_emits_nothing` | `PicoDccLoco(20000)` → `isValid()` false, `length == 0` |
| `test_create_from_address_speed_direction` (**rewrite**) | was `PicoDccLoco(3, 128, false)` asserting 81 — a nonsense speed that happened to mask to 0. Use `PicoDccLoco(3, 126, false)` → `{0x03, 0x5F}` |
| `test_create_from_address_rejects_speed_above_max` | `PicoDccLoco(3, 200, true)` → `isValid()` true, `{0x03, 0x71}` |

`test/pico_dcc_locos_tests.cpp` — add:

| Test | Asserts |
|---|---|
| `test_add_loco_returns_true_for_valid_packet` | `"t 3 10 1"` → true, `getLocoCount() == 1` |
| `test_add_loco_rejects_cab_zero` | `"t 0 10 1"` → false, count 0, `cmd.length == 0` |
| `test_add_loco_rejects_address_above_14_bits` | `"t 65535 10 1"` → false, count 0 |
| `test_add_loco_rejects_non_throttle_packet` | `"s"` → false, count 0. **This is #2's crash path**; before the fix the binary aborts here |
| `test_add_loco_caps_at_max_loco` | add addresses 1..`MAX_LOCO` (all true), the next → false, count stays `MAX_LOCO` |
| `test_update_loco_rejects_out_of_range_speed` | loco from `"t 3 10 1"`, then `update()` with `"t 3 128 1"` → false, bytes still `0x72` |
| `test_update_loco_estop_and_reminder` | loco from `"t 3 20 1"`, update `"t 3 -1 1"` → `{0x03, 0x61}`; then `getNextReminder()` returns the same two bytes with `repeats == 0` |
| `test_update_loco` (**fix**) | `"t 3 128 1"` at line 48 is now rejected. Change the update to `"t 3 126 1"` and the expectation from `113` to `0x7F` |

`test/pico_dcc_wire_format_tests.cpp` — three of the four "current behaviour of open defects"
assertions flip here. Move each out of that section into the correct-behaviour sections above
it and rename:

| Now | Becomes |
|---|---|
| `test_ISSUE_11_speed_minus_one_currently_means_full_speed` (`data[1] == 0x7F`) | `test_speed_minus_one_is_an_emergency_stop`: `throttle_update_for("t 3 10 1", "t 3 -1 1")` → `length 2`, `data == {0x03, 0x61}`, and `assert_int_not_equal(data[1], throttle_for("t 3 126 1").data[1])` |
| `test_ISSUE_12_cab_zero_currently_emits_a_broadcast` (`data[0] == 0x00`) | `test_cab_zero_emits_nothing`: `throttle_for("t 0 126 1").length == 0`. **Keep** its `assert_true(packet.isValid())` line for now with a `// step 3 flips this` comment — it is still true until validation lands |
| `test_ISSUE_16_address_above_14_bits_emits_idle_address` (`data[0] == 0xFF`) | `test_address_above_14_bits_emits_nothing`: `throttle_for("t 65535 126 1").length == 0` |

Add `test_function_command_does_not_move_a_loco`:
`throttle_update_for("t 3 0 1", "F 3 8 1").data[1] == 0x71` (unchanged).
Leave `test_speed_zero_encodes_as_a_stop`, `test_speed_zero_reverse`,
`test_highest_legal_long_address` and everything else untouched — but **update the comment** in
`test_speed_zero_encodes_as_a_stop` to record the decision: speed 0 keeps value 3 (emergency
stop, ignore direction) in this PR, the new single-loco estop uses value 2, and the follow-up
issue owns changing speed 0 to a controlled stop.

Update the file's "Current behaviour of open defects" section header — only the two `ISSUE_15`
accessory tests remain there.

`test/pico_dcc_controller_tests.cpp` — no edits needed in this step. `<t 3 128 1>` still
creates a loco (speed falls back to 0), so the existing assertions hold.

### Step 3 — `PicoDCCEX`: the protocol gate and the `<l>` reply

**`lib/PicoDCCEX/pico_dccexpacket.cpp`**

- `validatePacket()` — split the combined `case 't': case 'F': case 'a':` block:
  ```cpp
  case ('t'):
      if (dcc_is_valid_loco_address(packet.addr) && dcc_is_valid_throttle_speed(packet.param1))
          valid_packet = true;
      else
          LOG_WARNING(COMPONENT_DCCEX, "Throttle rejected: cab or speed out of range");
      break;

  case ('F'):
      if (dcc_is_valid_loco_address(packet.addr))
          valid_packet = true;
      else
          LOG_WARNING(COMPONENT_DCCEX, "Function rejected: cab out of range");
      break;

  case ('a'):
      if (packet.addr != -1) valid_packet = true;      // unchanged
      break;
  ```
  Note in a comment that `packet.addr == -1` (the `sscanf`-failed sentinel) is covered because
  -1 is outside 1..10239 — the old check is subsumed, not lost.
- `getDccExCabUpdate()` — three changes, and nothing else:
  1. delete the dead `int8_t responseSpeed` and its two assignments (`-Wunused-but-set-variable`,
     named in #11);
  2. handle estop first: `if (getSpeed() < 0) { speed128 = 1; } else { /* existing 0..126
     mapping, unchanged */ }` — DCC-EX speedByte 1 is emergency stop, so
     `<t 3 -1 1>` → `<l 3 0 129 0>` and `<t 3 -1 0>` → `<l 3 0 1 0>`;
  3. leave the `speed128 > 1 → speed128 - 1` mapping for 0..126 exactly as it is (see D6/UART).
- `lib/PicoDCCEX/pico_dccexpacket.h` — `dccex_cab_update[16]` → `dccex_cab_update[24]`, with a
  comment giving the longest possible reply (`<l 10239 0 255 0>`, 17 chars + NUL). Truncation
  here has been emitting `<l 10239 0 253 ` with no closing `>` for every 4- and 5-digit address.

`decodePacket()` needs **no change** — its "always parse, validate in the consumer" comment
stays accurate; the consumer is now `validatePacket()` rather than three inconsistent places.
Delete the stale part of that comment that promises per-field error messages.

**Tests changed in this step**

`test/pico_dcc_packet_tests.cpp` — add:

| Test | Asserts |
|---|---|
| `test_throttle_cab_zero_is_rejected` | `"t 0 126 1"` → `isValid()` false |
| `test_throttle_cab_above_range_is_rejected` | `"t 10240 20 1"`, `"t 65535 126 1"` → false |
| `test_throttle_cab_range_boundaries_accepted` | `"t 1 0 1"` and `"t 10239 126 1"` → true |
| `test_throttle_speed_above_max_is_rejected` | `"t 3 127 1"`, `"t 3 128 1"`, `"t 3 256 1"` → false |
| `test_throttle_speed_minus_one_is_accepted` | `"t 3 -1 1"` → true, `getSpeed() == -1` |
| `test_throttle_speed_below_minus_one_is_rejected` | `"t 3 -2 1"` → false |
| `test_malformed_throttle_is_rejected` | `"t 3"` → false (pins the `sscanf` sentinel path) |
| `test_function_cab_range_is_validated` | `"F 0 1 1"` false, `"F 10240 1 1"` false, `"F 3 1 1"` true |
| `test_cab_update_reports_estop` | `"t 3 -1 1"` → `"<l 3 0 129 0>"`; `"t 3 -1 0"` → `"<l 3 0 1 0>"` |
| `test_cab_update_long_address_not_truncated` | `"t 10239 126 1"` → `"<l 10239 0 253 0>"` (17 chars, closing `>` present) |

`test/pico_dcc_wire_format_tests.cpp` — flip the one line left over from step 2:
`test_cab_zero_emits_nothing` now asserts `assert_false(packet.isValid())`. Add
`test_estop_response_reports_estop_not_full_speed` (`"t 3 -1 1"` → `"<l 3 0 129 0>"`) and
`test_cab_update_is_not_truncated_for_long_addresses` (`"t 10239 126 1"` →
`"<l 10239 0 253 0>"`) to the "Responses to the host" section.

`test/pico_dcc_controller_tests.cpp` — `<t 3 128 1>` is now rejected outright, so the three
uses must change to a legal speed or the locos are never created:

- line 252 (`test_command_queue_processing`) → `<t 3 126 1>`
- lines 292 and 296 (`test_emergency_stop`) → `<t 3 126 1>` and `<t 4 126 1>`

`<t 1 3 1 1>` (lines 599, 610, 638) and `<t 2 5 1 1>` still parse to cab 1/2 at speeds 3/5 and
stay valid — leave them, but add a comment at line 599 noting they are the legacy 4-field form
being misread, and that #2's sibling issue owns it.

### Step 4 — `PicoDCCController`: handle rejection, stop lying about `<F>`

**`lib/PicoDCCController/pico_dcccontroller.cpp`, `dccexLoop()`** (throttle/function block,
~line 158):

```cpp
if (!pico_locos->updateLocoThrottle(packet.getCab(), &packet, cmd)) {
    if (!pico_locos->addLoco(&packet, cmd)) {
        LOG_WARNING(COMPONENT_CONTROLLER, "Throttle command rejected");
    }
}
if (cmd.length > 0) { main_cmd_queue.push(cmd); }
if (packet.isThrottleCommand()) { DCCEX_RESPONSE(packet.getDccExCabUpdate()); }
```

No `try`/`catch` — there is nothing left to throw. The `<l>` reply is now sent for `<t>` only
(D5): the packet class has no way to report a function map or the loco's real speed, and
reporting the function number as a speed is worse than reporting nothing.

Leave the maintenance-mode branch, the emergency-stop branch, the accessory branch and the
repeat/interleave logic untouched.

**Tests changed in this step** — `test/pico_dcc_controller_tests.cpp`:

| Test | Asserts |
|---|---|
| `test_dccex_acknowledgments` (**fix**) | the `<F 3 144 1>` block now expects `uart_output_log.size() == 0`. Comment it with the reason |
| `test_ISSUE_2_rejected_throttle_does_not_abort` | write `<t 0 126 1>`, `<t 65535 126 1>`, `<t 3 999 1>`, `<t 3>` through `uart_test_write` + `dccexLoop()`; assert `getLocoCount() == 0`, no `<l ` in `uart_output_log`, `queued_commands.empty()`, and that the test returns (before the fix, `<t 3 999 1>` reaches `std::terminate` and aborts the binary) |
| `test_function_command_does_not_queue_a_speed_change` | `<t 3 0 1>` then `<F 3 8 1>`; drive `dccLoop()` and use the existing `unpack_sent_packet()` helper to assert every transmitted non-idle packet for cab 3 is `{0x03, 0x71, 0x72}`, i.e. no packet carrying speed 8 |

### Step 5 — Documentation (same PR)

See "Docs to update".

---

## Expected wire bytes (the contract this plan pins)

Rail bytes are shown as the packet payload plus the XOR checksum the station appends in
`PicoDccTrack::sendCommand()`; `cmd.data[]` holds everything before the checksum.

| Command | `cmd.data[]` | On the rails (with checksum) | Notes |
|---|---|---|---|
| `<t 3 20 1>` — short address, forward | `03 64` | `03 64 67` | 28-step value 8 = step 5. `repeats = 3` |
| `<t 1000 20 1>` — long address, forward | `C3 E8 64` | `C3 E8 64 4F` | `((1000>>8)&0x3F)\|0xC0 = 0xC3` |
| `<t 10239 0 1>` — top of range | `E7 FF 71` | `E7 FF 71 69` | unchanged from today; proves the mask did not shrink the range |
| `<t 3 -1 1>` — estop forward | `03 61` | `03 61 62` | `0x41 \| 0x20`; same instruction byte as `<!>` |
| `<t 3 -1 0>` — estop reverse | `03 41` | `03 41 42` | |
| `<!>` — broadcast estop | `00 41` | `00 41 41` | unchanged |
| `<t 0 126 1>` | *(nothing)* | *(nothing)* | rejected in `validatePacket()`; dropped by `PicoDccEx::processCommand`; no loco, no queue entry, **no UART reply**, one `LOG_WARNING` |
| `<t 65535 126 1>` | *(nothing)* | *(nothing)* | same |

Host replies: `<t 3 20 1>` → `<l 3 0 19 0>` *(unchanged mapping)*; `<t 3 -1 1>` →
`<l 3 0 129 0>`; `<t 10239 126 1>` → `<l 10239 0 253 0>`; `<F 3 8 1>` → *(no reply)*;
rejected commands → *(no reply)*.

---

## Verification

```bash
cmake --preset host && cmake --build --preset host && ctest --preset host
cmake --preset pico && cmake --build --preset pico
```

The **hardware build is required**: `lib/dcc_types.h` is a shared header included by every
component, and `HIGHEST_SHORT_ADDR`/`INVALID_LOCO_ADDR` move between headers. CI does not
cross-build, so this must be run locally. `scripts/Validate-DualMode.ps1` does both in one go.

Expect roughly 189 tests across the same 11 suites (packet +10, loco +10 net of 5 removed,
locos +7, controller +2, wire format +3). Take the exact number from `ctest` output — do not
copy the estimate into the docs.

Bench work, in isolation from any other change, with Paul's approval and track power confirmed
(see "How is it testable", above): single-loco estop behaviour, JMRI throttle sync across
estop, function presses and a long-address loco.

---

## Docs to update (same PR)

- **`CLAUDE.md`** — the test count on line 24; the "Transport facts" bullet on `<t cab speed dir>`
  gains the accepted ranges (cab 1..10239, speed 0..126 or -1 for estop) and a note that
  rejected commands are silent; add `<F>` to "Things that are in the tree but do not work"
  (accepted and used to create a locomotive, but no function packet is generated and no `<l>`
  is returned).
- **`docs/architecture.md`** — the accepted-opcode table rows for `<t>` and `<F>` (state the
  validated ranges, as the `<a>` row already does); the PicoDccLoco "Key Features: Address
  validation" line, which is currently aspirational and becomes true; the test count on line 276.
- **`docs/README.md`** — the test count on line 29; add a row to the current-state table for
  throttle/address validation; note under it that `<F>` is accepted but inert.
- **`docs/dccex-compliance-analysis.md`** — no change needed; line 51's "Full 0-126 speed range"
  becomes accurate rather than aspirational.

---

## Follow-up issues to file (not fixed here)

1. **Speed 0 emits an emergency stop.** `generateThrottleCommand()` maps speed 0 to 28-step
   value 3 (`0x71`/`0x51`) instead of value 0 (`0x60`/`0x40`), so every controlled stop is an
   emergency stop. Fixing it changes the most common command in the system and must be
   bench-tested alone. Pinned by `test_speed_zero_encodes_as_a_stop`.
2. **`getDccExCabUpdate()`'s 0..126 mapping disagrees with the packet encoder** (`s → s-1`
   where DCC-EX uses `s → s+1`). Named in #11; needs a JMRI bench test to settle.
3. **`<F>` produces no `<l>` reply and no function packet.** Real function support closes both.
4. **`findLoco()` returns a raw pointer into a vector that Core 0 can mutate.** The `MAX_LOCO`
   cap added here removes the reallocation case; the aliasing is still unsound.
5. **`-fno-exceptions` for the firmware build.** Now that `PicoDccLoco` no longer throws, the
   remaining source is `std::vector`; a fixed-capacity container would close it.

---

## Risks

- **The estop encoding is unproven on this layout.** `0x61`/`0x41` is what the standard and the
  existing `<!>` broadcast say, but #11 records that the throttle encoding was tuned empirically
  against JMRI and the reasoning was lost. If a decoder ignores value 2 but honours value 3,
  the estop will look like it did nothing. Bench test before merging; the fallback is
  `0x71`/`0x51` (value 3), one line in `generateThrottleCommand()`.
- **Estop is sticky.** Once a loco is estopped, Core 1 reminders re-send the estop forever until
  a new `<t>` arrives. If a throttle expects the station to forget an estop by itself, the loco
  will appear dead. This is intended DCC-EX behaviour, but it is new behaviour for this
  firmware and is worth watching for at the bench.
- **Suppressing the `<l>` reply to `<F>` is the riskiest JMRI-facing change here.** DCC++
  classic never replied to `<F>` and JMRI does not block on it, but if any client does, function
  presses will appear to hang. Cheap to revert independently of the rest.
- **Existing layouts using cab 0 or an address above 10239 will stop working.** That is the
  point of #12 and #16, but it is a hard rejection with no message on the wire (#4), so it will
  present to a user as "that loco does nothing" with only the LCD log to explain it.
- **A client that streams invalid commands fills the 30-entry diagnostic buffer**, pushing out
  older entries. Acceptable, but it means the log is not a reliable record during a flood.
- **`getNextReminder()`'s new zero-length guard erases the loco** rather than skipping it,
  matching the existing `isValid()` behaviour. If a valid loco ever produced a zero-length
  command, it would silently disappear from the collection. It cannot after this change — every
  valid address produces at least two bytes — but the coupling is worth knowing about.
