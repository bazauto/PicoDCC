# PicoDCC Critical Review — February 2026

**Scope**: All library code, all 156 tests (10 suites), all 23 docs, build system, copilot-instructions.md  
**Platform**: RP2350 (Pico 2), dual-core Cortex-M33, 4MB flash, 520KB RAM

---

## Summary

| Area | Grade | Notes |
|------|-------|-------|
| Architecture & Design | A- | Dual-core split, safety systems, and display DI are strong |
| Code Quality | B | Solid core logic; thread-safety and validation bugs exist |
| Test Infrastructure | B+ | Good coverage overall; critical gaps in DCC-EX parser and some false passes |
| Documentation | C | Excellent planning docs, but most frozen at Oct 2025; widespread inaccuracies |

**Total tests**: 156 registered (150 with actual assertions, 6 empty stubs)  
**Test suites**: 10 (Controller 15, DCCEX 3, Loco 22, Locos 11, Packet 25, Programmer 30, Track 21, Config 11, Display 9, Diagnostic 9)

---

## THE GOOD

### Architecture
- **Dual-core design** is clean: Core 0 handles command parsing + UI, Core 1 handles real-time DCC packet transmission
- **Reminder generation on Core 1** is self-regulating (hardware-paced), eliminating queue overflow by design
- **Layout Maintenance Mode** with verify-not-force safety pattern and LCD-only entry (physical presence required)
- **Display architecture** with `IDisplayRenderer` interface and dependency injection enables real unit testing without LVGL
- **PIO-based DCC generation** with 3 independent health checks (PC tracking, transmission counters, activity timeout)
- **Config storage** with CRC32 validation, linker script reservation, hybrid runtime/persistent architecture
- **Diagnostic logging** with circular buffer, per-core safe access, and LCD display integration

### Code
- DCC packet generation follows NMRA standards correctly (preamble lengths, function groups F0-F28, accessory encoding, broadcast emergency stop)
- Queue metrics with high-water mark and wait time tracking
- PicoDccProgrammer ACK detection with hysteresis and configurable parameters
- Version header auto-generated from git hash at build time
- Custom linker script preserves config across firmware updates

### Testing
- 156 tests with CMocka is solid for an embedded project
- PIO mock captures and reassembles DCC packets for verification
- UART output log capture enables protocol response testing
- CTest integration with VS Code test explorer

---

## THE BAD — Bugs to Fix

### P0 — Critical

#### 1. Config validation rejects its own defaults
- **File**: `lib/PicoConfigStorage/pico_config_storage.cpp` line 185
- **Bug**: `adc_to_ma_conversion > 1.0f` rejects values above 1.0, but `DEFAULT_ADC_TO_MA` is 15.0f
- **Impact**: Any config saved with the default conversion factor fails validation on reload, falling back to defaults every boot
- **Fix**: Change upper bound to ~100.0f or remove it (CRC already guards against corruption)
- [ ] Fixed

#### 2. Hardcoded PIO State Machine 0
- **File**: `lib/PicoDCCTrack/pico_dcctrack.cpp` lines 181-186
- **Bug**: `pio_sm_put_blocking((PIO)pio, 0, ...)` uses hardcoded SM 0 instead of `pio_sm` member
- **Impact**: Works only because SM 0 happens to be free; if anything else claims SM 0 first, DCC output silently goes to wrong state machine
- **Fix**: Replace `0` with `pio_sm`
- [ ] Fixed

#### 3. `findLoco()` returns dangling pointer
- **File**: `lib/PicoDCCLoco/pico_dcclocos.cpp` lines 36-52
- **Bug**: Acquires semaphore, gets pointer into vector, releases semaphore, returns pointer. If Core 1 erases elements via `getNextReminder()`, the pointer from Core 0 is dangling
- **Fix**: Return by value, or use RAII lock guard for caller scope, or use indices
- [ ] Fixed

### P1 — High

#### 4. Missing `volatile` on cross-core booleans
- **File**: `lib/PicoDCCTrack/pico_dcctrack.h` lines 83-85
- **Bug**: `power_on`, `tripped`, `send_idle_packets` written by one core, read by the other, but not `volatile` or atomic. Compiler may cache stale values
- **Impact**: On RP2350 with separate L1 caches per core, this is a real concern
- **Fix**: Mark as `volatile bool` or use `std::atomic<bool>`
- [ ] Fixed

#### 5. Core 1 heartbeat false positive at boot
- **File**: `lib/PicoDCCController/pico_dcccontroller.cpp` lines 78-84
- **Bug**: Both `core1_heartbeat` and `last_core1_heartbeat_value` init to 0. First 50ms check sees them equal → `emergencyPowerCutoff()` fires before Core 1 starts
- **Impact**: `sleep_ms(100)` in `main()` may paper over this, but it's a race
- **Fix**: Initialize `last_core1_heartbeat_value` to -1 (or UINT32_MAX), or add a `core1_started` flag
- [ ] Fixed

#### 6. Diagnostic logger race condition
- **File**: `lib/PicoDiagnostic/pico_diagnostic.cpp` line 204
- **Bug**: `static diagnostic_msg_t msg` populated before semaphore is acquired. Core 0 and Core 1 calling `log_diagnostic()` simultaneously corrupt each other's message
- **Fix**: Use `get_core_num()` to index into per-core static buffers
- [ ] Fixed

#### 7. Integer division precision loss for overcurrent threshold
- **File**: `lib/PicoDCCTrack/pico_dcctrack.cpp` line 107
- **Bug**: `TRACK_POWER_ADC_RANGE / 100 * 90` → `4096 / 100 * 90` = `40 * 90` = 3600 (87.9%), not 3686 (90%)
- **Fix**: `(TRACK_POWER_ADC_RANGE * 90) / 100` or `TRACK_POWER_ADC_RANGE * 9 / 10`
- [ ] Fixed

#### 8. Programmer `readCV()` timing in hardware mode
- **File**: `lib/PicoDCCProgrammer/pico_dcc_programmer.cpp` line 358
- **Bug**: `sleep_us(5000)` is commented out in hardware mode — `detectACK()` called immediately after queueing packet, before it's transmitted
- **Impact**: ACK detection may start before decoder receives the command
- **Fix**: Uncomment or implement proper transmission-complete signaling
- [ ] Fixed

#### 9. SPI CS pin conflict in LCD driver
- **File**: `lib/PicoDCCDisplay/lcd_driver.cpp` lines 76-77
- **Bug**: `gpio_set_function(LCD_PIN_CS, GPIO_FUNC_SPI)` assigns CS to SPI hardware, but `writeCommand()`/`writeData()` use `gpio_put()` for manual CS control. Once assigned to SPI function, `gpio_put()` may have no effect
- **Fix**: Either use hardware CS exclusively or don't assign `GPIO_FUNC_SPI` to the CS pin (use `GPIO_FUNC_SIO` for software control)
- [ ] Fixed

### P2 — Medium

#### 10. `void *pio` loses type safety
- **File**: `lib/PicoDCCTrack/pico_dcctrack.h` line 71
- **Issue**: PIO handle stored as `void*`; every use requires `(PIO)pio` cast
- **Fix**: Use Pico SDK's `PIO` type directly
- [ ] Fixed

#### 11. Null reference check (undefined behavior)
- **File**: `lib/PicoDCCEX/pico_dccexpacket.cpp` lines 19-21
- **Issue**: `if (&packetData == nullptr)` — a reference cannot be null in valid C++; compilers optimize this away
- **Fix**: Remove the check or change parameter to pointer
- [ ] Fixed

#### 12. `typedef unsigned int uint` redefinition
- **File**: `lib/PicoDCCTrack/pico_dcctrack.h` line 58
- **Issue**: `uint` may already be defined on some platforms; fragile
- **Fix**: Use `unsigned int` directly or `uint32_t`
- [ ] Fixed

#### 13. CV method stubs declared but never defined
- **File**: `lib/PicoDCCLoco/pico_dccloco.h` lines 45-49
- **Issue**: `verifyCV()`, `readCVByte()`, `readCVBit()`, `writeCVBytes()`, `writeCVBit()` declared but never defined. Linking fails if called
- **Fix**: Remove declarations or add stub implementations returning error codes
- [ ] Fixed

#### 14. Duplicate `INVALID_LOCO_ADDR` define
- **Files**: `lib/PicoDCCLoco/pico_dccloco.h` line 16, `lib/PicoDCCLoco/pico_dcclocos.h` line 24
- **Fix**: Define once in `dcc_types.h`
- [ ] Fixed

#### 15. `HIGHEST_SHORT_ADDR` defined in Track, used in Loco
- **Issue**: Creates unnecessary cross-component dependency (Loco → Track header)
- **Fix**: Move to `dcc_types.h`
- [ ] Fixed

#### 16. `std::invalid_argument` on bare metal
- **File**: `lib/PicoDCCLoco/pico_dccloco.cpp` lines 11-12
- **Issue**: `PICO_CXX_ENABLE_EXCEPTIONS 1` makes this work, but each `throw` site adds ~2-4KB of exception tables. Packets are pre-validated before reaching this constructor
- **Fix**: Replace with error codes or `assert()` for precondition violations
- [ ] Fixed

#### 17. No watchdog timer
- **Issue**: If `main()` loop hangs (not Core 1, which has the heartbeat), there's no recovery. RP2350 has a hardware watchdog
- **Fix**: Enable `hardware_watchdog`, feed in main loop
- [ ] Fixed

#### 18. No config migration path
- **File**: `lib/PicoConfigStorage/pico_config_storage.cpp`
- **Issue**: `CONFIG_VERSION` is checked strictly (`!= CONFIG_VERSION → reject`). Adding new fields to `pico_config_t` causes all existing configs to be rejected and reset
- **Fix**: Add version upgrade handler (read old version, copy fields, set defaults for new fields)
- [ ] Fixed

### P3 — Low

#### 19. Typo collection
- `DCCEX_RECIVING` → RECEIVING (`lib/PicoDCCEX/pico_dccex.h`)
- `isAccesoryCommand` → isAccessoryCommand (multiple files)
- `"Packate"` → Packet (`lib/PicoDCCController/pico_dcccontroller.cpp` line 51)
- `"Bradcast"` → Broadcast (`lib/PicoDCCEX/pico_dccexpacket.cpp` line 230)
- `"Initiallising"` → Initializing (`lib/PicoDCCLoco/pico_dccloco.cpp`)
- [ ] Fixed

#### 20. CMake target name mismatch (Programmer)
- **File**: `lib/PicoDCCProgrammer/CMakeLists.txt`
- **Issue**: Test mode creates `PicoDccProgrammer`, hardware mode creates `PicoDCCProgrammer` — different casing
- [ ] Fixed

#### 21. Dead code in DCC-EX processor
- **File**: `lib/PicoDCCEX/pico_dccex.cpp` lines 78-95
- **Issue**: Three branches after `if (currentPacket->isValid())` (version/power/else) all do exactly the same thing
- [ ] Fixed

---

## THE UGLY — Test Issues

### Test Bugs

#### T1. Wrong variable name in locos test
- **File**: `test/pico_dcc_locos_tests.cpp` line 93
- **Bug**: `PicoDccExPacket packet1((char *)buffer)` should be `buffer1` — both locos get address 3 instead of distinct addresses
- [ ] Fixed

#### T2. Exception tests silently pass when exception is NOT thrown
- **File**: `test/pico_dcc_loco_tests.cpp` lines 48-85
- **Bug**: Six try/catch tests lack `fail()` after the constructor. If constructor stops throwing, tests pass instead of catching the regression
- [ ] Fixed

#### T3. Six empty programmer tests inflate count
- **File**: `test/pico_dcc_programmer_tests.cpp`
- **Tests with zero assertions**:
  - `test_generate_cv_read_packet_cv1` (line 282)
  - `test_generate_cv_read_packet_cv17` (line 297)
  - `test_generate_cv_read_packet_cv513` (line 306)
  - `test_generate_cv_verify_packet_cv1_value_3` (line 317)
  - `test_read_short_address_success` (line 355)
  - `test_read_long_address_success` (line 368)
- **Real test count**: 150 (not 156)
- [ ] Fixed (implement or remove)

#### T4. PIO mock state leaks between tests
- **File**: `test/mocks.cpp` lines 179-195
- **Bug**: `static` locals in `pio_sm_put_blocking` can't be reset by setup/teardown. Odd-count PIO writes corrupt next test
- [ ] Fixed

#### T5. Mock semaphores are complete no-ops
- **File**: `test/mocks.cpp` lines 140-143
- **Impact**: Thread-safety bugs (e.g., `findLoco()` dangling pointer) cannot be caught by any test. Test named `test_thread_safe_loco_count` is single-threaded with no-op locks
- [ ] Fixed (at minimum, add counting to detect missing acquire/release pairs)

#### T6. `mock_time_ms` initial value inconsistency
- `test_globals.cpp` initializes to 1000
- `pico_dcc_display_tests.cpp` initializes to 0
- `pico_diagnostic_tests.cpp` initializes to 0
- Tests that forget to reset inherit previous test's time
- [ ] Fixed

### Missing Test Coverage

#### T7. DCC-EX parser has only 3 tests (critical gap)
- Only tests: constructor, reset, empty command
- **Missing**: state machine transitions, malformed input, buffer overflow, partial commands, unknown opcodes
- [ ] Added

#### T8. No error-path tests for Controller
- Missing: malformed UART input, power-on then immediate e-stop, idempotent power commands, forget non-existent cab
- [ ] Added

#### T9. No boundary-value tests for Loco speed steps
- Missing: speed 0 (stop), speed 1 (e-stop per NMRA), speed 126, 127, 128
- [ ] Added

#### T10. `test_current_averaging` only tests initial state
- **File**: `test/pico_dcc_track_tests.cpp` line 488
- Never exercises the averaging algorithm
- [ ] Fixed

#### T11. Display test uses fake pointer
- **File**: `test/pico_dcc_display_tests.cpp` line 108
- `reinterpret_cast<PicoDccController*>(0x1000)` — dangling pointer that will segfault if display logic ever dereferences it
- [ ] Fixed

#### T12. Controller test boilerplate duplication
- Every controller test repeats 15+ lines of `track_settings_t` initialization (~200 lines total)
- Should extract into fixture/helper
- [ ] Fixed

---

## WHAT'S MISSING

### Code

| Item | Description | Priority |
|------|-------------|----------|
| Watchdog | RP2350 hardware watchdog not enabled; no recovery if main loop hangs | P2 |
| Display failure logging | `display.init()` failure prints to stdio but doesn't log via `LOG_CRITICAL` | P3 |
| Config version migration | Strict version check means adding fields resets all user configs | P2 |
| UART input sanitization | DCC-EX parser uses fixed buffers with no overflow protection | P2 |
| `HIGHEST_SHORT_ADDR` location | Defined in Track, should be in `dcc_types.h` | P3 |
| `raw_dcc_cmd_t` alignment | Cross-core struct missing `__attribute__((aligned(8)))` | P2 |
| Config setter validation | `setACKThreshold` etc. accept any float with no bounds checking | P2 |
| `ack_max_duration_ms` validation | Same range as min (1-10ms); should allow higher and check max > min | P2 |

### Documentation

| Item | Description | Priority |
|------|-------------|----------|
| Test counts wrong everywhere | architecture.md: 124, copilot-instructions.md: 113, README: 64, actual: 156 | P1 |
| RP2040 vs RP2350 confusion | gpio-pinout-reference.md, non-volatile-storage-options.md, lcd-integration.md all reference wrong MCU | P1 |
| Wrong SPI pins in lcd-integration.md | GP4/GP5 shown instead of GP6/GP7 (code has GP6=SCK, GP7=MOSI) | P1 |
| Planning docs marked TODO for done features | cv-address-read-plan, service-mode-plan, README checkboxes all unchecked | P2 |
| No user-facing command reference | Supported DCC-EX commands scattered across 5+ docs | P2 |
| No changelog | No record of what changed when | P3 |
| Doc redundancy | 4 CV docs, 3 coverage docs, 3 config docs — consolidate each cluster | P2 |
| AI conversational artifacts | "Would you like me to...", "Are you ready?", "Happy coding! 🚂" in 3+ docs | P3 |
| copilot-instructions.md accuracy | Says 113 tests, Windows-centric paths/commands, component counts wrong | P1 |
| No Linux build instructions | All build docs are PowerShell/Windows; workspace is Linux | P2 |
| architecture.md overcurrent | Says "70% of ADC range" but code uses 90% | P2 |
| architecture.md Mermaid diagram | Missing PicoDccProgrammer and PicoConfigStorage components | P2 |
| "Future Enhancements" lists done features | CV Programming and F13-F28 listed as planned in architecture.md | P3 |

---

## Document-by-Document Status

| Document | Status | Key Issue |
|----------|--------|-----------|
| architecture.md | STALE | Wrong test counts, wrong overcurrent %, missing components in diagram |
| ack-detection-analysis.md | STALE | Written as planning; feature is implemented; contains AI conversational prompts |
| calibration-guide.md | MOSTLY CURRENT | References "Future Feature" for CV verify which is now implemented |
| coverage-quick-start.md | STALE | Oct 2025 numbers, Windows-only, missing programmer coverage |
| coverage-scripts-overview.md | STALE | "Last updated: October 20, 2025", wrong test counts |
| cv-address-read-implementation-plan.md | STALE | Says "READY TO IMPLEMENT" — fully implemented with 30 tests |
| cv-programming-integration.md | CURRENT | Minor: says 26 tests, now 30 |
| dccex-compliance-analysis.md | STALE | Says "No CV programming capability" — FALSE; wrong flash address (0x101FF) |
| dccex-jmri-compatibility-todos.md | PARTIALLY STALE | "Last Updated: October 20, 2025" |
| diagnostic-log-display-quick-reference.md | MOSTLY CURRENT | Component table has codes not in header; references nonexistent docs |
| firmware-update-config-preservation.md | CONTRADICTORY | Marked "IMPLEMENTED" but contains "Phase 2: Future" for completed work |
| gpio-pinout-reference.md | STALE | Says "RP2040"; pin assignments are correct |
| hardware-test-plan-nv-storage.md | CONTRADICTORY | Test 13 updated for linker script but body says "Config NOT Preserved" |
| hardware-test-quick-reference.md | PARTIALLY STALE | Wrong `<s>` response format |
| implementation-complete-config-storage.md | STALE | Reads as snapshot; linker script "not enforced" — it IS |
| lcd-integration.md | STALE | **Wrong SPI pin assignments**; wrong RAM size; says "no persistence" — FALSE |
| linker-script-implementation-summary.md | CURRENT | Minor: firmware size may be outdated |
| non-volatile-storage-options.md | STALE | Says "RP2040 has 2MB flash" — project uses RP2350 with 4MB |
| README.md | STALE | "Last Updated: October 19, 2025"; all checkboxes unchecked; says 64 tests |
| safety-recommendations.md | CURRENT | Test count outdated but safety content is accurate |
| service-mode-programming-plan.md | STALE | Says "Planning Phase" — Phases 1-4 implemented |
| test-coverage-report.md | STALE | Oct 2025 snapshot; missing programmer; should be auto-generated |
| vscode-test-integration.md | STALE | Wrong test count; Windows-only; "October 20, 2025" |

---

## Recommended Action Plan

### Phase 1 — Fix Bugs (safety/correctness)
1. Fix config validation upper bound (`adc_to_ma_conversion > 1.0f` → `> 100.0f`)
2. Fix hardcoded PIO SM 0 → `pio_sm`
3. Fix `findLoco()` to return by value
4. Add `volatile` to cross-core booleans
5. Fix heartbeat initial state
6. Fix diagnostic logger per-core buffer
7. Fix integer division order for overcurrent
8. Fix programmer `readCV()` hardware-mode timing

### Phase 2 — Fix Tests (reliability/coverage)
1. Fix `buffer` → `buffer1` bug in locos test
2. Add `fail()` to exception tests
3. Implement or remove 6 empty programmer tests
4. Add DCC-EX parser tests (critical gap)
5. Fix PIO mock state leak

### Phase 3 — Documentation Cleanup
1. Fix all test counts (or remove hardcoded numbers)
2. Fix RP2040 → RP2350 references
3. Fix SPI pin assignments in lcd-integration.md
4. Update copilot-instructions.md for Linux and current state
5. Mark planning docs as archived/historical
6. Consolidate redundant doc clusters
7. Create user-facing DCC-EX command reference

### Phase 4 — Improvements
1. Enable hardware watchdog
2. Add config version migration
3. Clean up typos
4. Change `void *pio` to `PIO` type
5. Remove dead code and stubs
