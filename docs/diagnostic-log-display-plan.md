# Diagnostic Log Display Feature - Implementation Plan

## Overview
Add an LCD screen to display diagnostic log entries from `pico_diagnostic.h`, enabling easier troubleshooting for DCC programming and non-volatile storage operations.

## Current State Analysis

### Existing Infrastructure ✅
1. **Diagnostic Logging System** (`lib/pico_diagnostic.h`)
   - 4 severity levels: INFO, WARNING, ERROR, CRITICAL
   - `diagnostic_msg_t` structure with timestamp, component, message
   - Currently silent (no output except CRITICAL in test mode)
   - 6 component identifiers defined
   - 17 LOG calls across codebase (5 in Track, 5 in Controller, 2 in DCCEX)

2. **LCD Display Infrastructure** (`lib/PicoDCCDisplay/`)
   - LVGL 8.3 graphics library fully integrated
   - LcdDriver working (ST7789T3, 320x240 SPI)
   - TouchDriver working (CST328, I2C, multi-touch capable)
   - LvglRenderer with diagnostic screen framework
   - 10Hz update rate (100ms interval)
   - Dependency injection architecture (testable)

3. **Touch Input System** ✅
   - CST328 controller fully functional
   - Touch callbacks already implemented in LvglRenderer
   - Multi-touch support (up to 5 points)
   - GPIO interrupts configured (GP10)
   - Touch event structure ready

4. **Display Architecture** ✅
   - Clean separation: Display → Renderer → Driver
   - IDisplayRenderer interface for testability
   - Mock renderer for unit tests
   - No TEST_BUILD conditionals in business logic
   - Controller reference already passed to display

## Dependencies Check

### ✅ READY - No Blockers Identified
1. **Hardware**
   - ✅ LCD operational (verified in current implementation)
   - ✅ Touch controller operational (Phase 4 buttons already work)
   - ✅ SPI and I2C buses configured
   - ✅ Sufficient GPIO pins available

2. **Software Architecture**
   - ✅ LVGL fully integrated (v8.3)
   - ✅ Display update loop established (10Hz)
   - ✅ Controller data access working
   - ✅ Test infrastructure in place (MockDisplayRenderer)

3. **Memory Considerations**
   - ⚠️ NEED TO CHECK: Flash memory usage (currently unknown)
   - ⚠️ NEED TO CHECK: RAM usage for log buffer (estimate: 2-4KB for 20-50 entries)
   - ✅ LVGL buffer already allocated (20 lines)

4. **Timing**
   - ✅ Display loop runs on Core 0 (no DCC timing interference)
   - ✅ 10Hz update rate sufficient for log display
   - ✅ No real-time constraints for diagnostic display

## Proposed Implementation

### Phase 1: Log Storage Infrastructure
**Goal**: Add circular buffer to store diagnostic messages in RAM

**Components to Modify**:
1. `lib/pico_diagnostic.h`
   - Add `diagnostic_log_buffer_t` circular buffer structure
   - Define `DIAG_LOG_BUFFER_SIZE` (suggest 20-50 entries)
   - Add functions:
     - `void diag_log_init(void)`
     - `void diag_log_add(diagnostic_msg_t msg)`
     - `uint8_t diag_log_get_count(void)`
     - `diagnostic_msg_t* diag_log_get_entry(uint8_t index)`
     - `void diag_log_clear(void)`
   - Modify `log_diagnostic()` to call `diag_log_add()`

2. `src/pico_dcc.cpp`
   - Add `diag_log_init()` call in `main()` initialization

**Test Coverage**:
- Create `test/pico_diagnostic_tests.cpp`
  - Test circular buffer wrap-around
  - Test entry retrieval
  - Test clear functionality
  - Test thread safety (if needed for multi-core)

**Estimated Effort**: 2-4 hours

---

### Phase 2: Log Display Screen (LVGL UI)
**Goal**: Create new LVGL screen to display log entries

**Components to Modify**:
1. `lib/PicoDCCDisplay/i_display_renderer.h`
   - Add virtual method: `void showLogScreen()`
   - Add virtual method: `void updateLogScreen()`

2. `lib/PicoDCCDisplay/lvgl_renderer.h/cpp`
   - Add LVGL objects:
     - `lv_obj_t* log_screen_`
     - `lv_obj_t* log_table_` (or list for scrollable entries)
     - `lv_obj_t* btn_clear_logs_`
     - `lv_obj_t* btn_back_to_main_`
   - Implement `showLogScreen()`:
     - Create table/list with columns: Time, Level, Component, Message
     - Color-code by severity (RED=CRITICAL, YELLOW=ERROR, etc.)
     - Add scrollbar for >10 entries
   - Implement `updateLogScreen()`:
     - Query diagnostic buffer
     - Update table/list contents
     - Auto-scroll to newest entry

3. `lib/PicoDCCDisplay/pico_dcc_display.h/cpp`
   - Add navigation state: `enum DisplayScreen { MAIN, LOG_VIEWER }`
   - Add method: `void showLogScreen()`
   - Add method: `void backToMainScreen()`

**Touch Button Integration**:
- Add "View Logs" button to main diagnostic screen
- Add "Clear" and "Back" buttons to log screen
- Wire button events to navigate between screens

**Test Coverage**:
- Extend `test/pico_dcc_display_tests.cpp`:
  - Test screen switching
  - Test log population
  - Test clear functionality

**Estimated Effort**: 4-6 hours

---

### Phase 3: Main Screen Integration
**Goal**: Add log status indicator to main diagnostic screen

**Components to Modify**:
1. `lib/PicoDCCDisplay/lvgl_renderer.h/cpp`
   - Add to main screen:
     - `lv_obj_t* log_status_label_` (shows count of unread entries)
     - `lv_obj_t* btn_view_logs_` (touch button to open log screen)
   - Update `updateDiagnosticScreen()`:
     - Update log count indicator
     - Flash indicator on new CRITICAL/ERROR messages
     - Change color based on highest severity (RED if CRITICAL exists, etc.)

**Visual Design**:
```
┌─────────────────────────────────────┐
│  PicoDCC Status          [LOGS: 5]  │ ← Click to view logs
├─────────────────────────────────────┤
│ Main Track:  ON   1250 mA           │
│ Prog Track:  OFF     0 mA           │
│ Packets:     12543   Locos: 3       │
├─────────────────────────────────────┤
│ [Main Pwr] [Prog Pwr] [Reset] [Cal] │
└─────────────────────────────────────┘
```

**Estimated Effort**: 2-3 hours

---

### Phase 4: Enhanced Features (Optional)
**Goal**: Advanced log viewing capabilities

**Possible Enhancements**:
1. **Filtering**
   - Filter by severity level
   - Filter by component
   - Show only CRITICAL/ERROR

2. **Export**
   - Output logs via UART for external capture
   - Save to flash (if programming feature adds flash write capability)

3. **Auto-Display**
   - Automatically switch to log screen on CRITICAL error
   - Pop-up notification overlay for critical messages

4. **Statistics**
   - Count errors by type
   - Time since last error
   - Most frequent error component

**Estimated Effort**: 4-8 hours (if implemented)

---

## Risk Assessment

### Low Risk ✅
1. **Display Integration**: Existing framework supports multiple screens
2. **Touch Input**: Already working for current buttons
3. **LVGL Rendering**: Proven stable in current implementation
4. **Testing**: Mock infrastructure already in place

### Medium Risk ⚠️
1. **Memory Usage**: Need to measure actual RAM impact
   - Mitigation: Start with small buffer (20 entries), test on hardware
   - Mitigation: Make buffer size configurable via `#define`

2. **Multi-Core Access**: Log buffer may be written from Core 1 (track errors)
   - Mitigation: Add semaphore protection to log buffer
   - Mitigation: Use atomic operations for buffer index

### Low Risk (Already Handled) ✅
1. **DCC Timing**: Display on Core 0, won't affect DCC packets ✅
2. **Protocol Compliance**: Logs don't use UART (DCC-EX channel) ✅
3. **Build Modes**: Can add TEST_BUILD mocks for log buffer ✅

---

## Implementation Sequence Recommendation

### Suggested Order:
1. **Phase 1 First** (Log Storage)
   - Establish core infrastructure
   - Test buffer mechanics without UI complexity
   - Verify memory usage is acceptable
   - Check multi-core safety

2. **Phase 2 Second** (Log Display)
   - Build UI on top of proven storage layer
   - Iterate on visual design easily
   - Test navigation flow

3. **Phase 3 Third** (Main Screen Integration)
   - Polish user experience
   - Add visual indicators

4. **Phase 4 Optional** (Future Enhancement)
   - Add if programming work requires it
   - Can be deferred to separate branch

### Estimated Total Time:
- **Minimum Viable**: Phases 1-3 = 8-13 hours
- **With Enhancements**: Phases 1-4 = 12-21 hours

---

## Dependencies on Programming Work

### ✅ INDEPENDENT - Can Do Now
This feature is **completely independent** of the DCC programming branch work:

1. **No CV Programming Required**: Log viewer just displays existing messages
2. **No Flash Write Required**: Log buffer is RAM-only (volatile)
3. **No New DCC Packets**: Uses existing diagnostic infrastructure
4. **No New Hardware**: LCD and touch already working

### Synergies with Programming Work
When merged together, this feature will **enhance** programming work:

1. **CV Read/Write Errors**: Will show up in log viewer
2. **Programming Track Issues**: Easier to diagnose with log history
3. **Timing Problems**: Visual indication of DCC violations
4. **Storage Issues**: NV storage errors visible on screen

### Recommendation
✅ **PROCEED NOW** - This feature can be developed in parallel with programming work and merged later. No blocking dependencies identified.

---

## Testing Strategy

### Unit Tests (TEST_BUILD=ON)
1. `test/pico_diagnostic_tests.cpp`:
   - Log buffer operations
   - Circular buffer wrap
   - Thread safety

2. `test/pico_dcc_display_tests.cpp`:
   - Screen switching
   - Log rendering
   - Button interactions

### Integration Tests
1. Generate test logs at startup
2. Verify display shows correct entries
3. Test clear functionality
4. Test scrolling with >10 entries
5. Verify color coding by severity

### Hardware Validation
1. Flash to Pico
2. Trigger real errors (short circuit, etc.)
3. Verify logs appear on screen
4. Test touch navigation
5. Measure RAM usage via debug output

---

## Next Steps

1. **Review this plan** - Confirm approach is acceptable
2. **Start Phase 1** - Implement log storage infrastructure
3. **Test on hardware** - Verify memory usage acceptable
4. **Proceed to Phase 2** - Build UI layer
5. **Integrate with main** - Add to diagnostic screen

## Questions to Answer Before Starting

1. ✅ **Buffer Size**: 20 entries × 64 bytes = ~1.3KB acceptable? (ANSWER: Likely yes, but measure)
2. ✅ **Persistence**: Keep logs on power cycle? (ANSWER: No, RAM-only for now)
3. ✅ **Multi-Core**: Protect buffer with semaphore? (ANSWER: Yes, if Core 1 writes logs)
4. ✅ **Auto-Display**: Switch to logs on CRITICAL? (ANSWER: Phase 4 optional)
5. ✅ **UART Output**: Also output to serial? (ANSWER: Phase 4 optional)

---

## Conclusion

✅ **READY TO PROCEED** - No blocking dependencies identified. This feature can be implemented immediately and will provide valuable troubleshooting capability for both current operations and future programming work.

The existing LCD, touch, and diagnostic infrastructure provides a solid foundation. The main work is adding the log buffer and LVGL UI screens, both of which are straightforward given the current architecture.

Estimated timeline: **1-2 days of focused development** for Phases 1-3 (minimum viable feature).
