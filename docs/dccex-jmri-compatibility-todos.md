# DCC-EX JMRI Compatibility TODO List

**Date**: October 20, 2025  
**Purpose**: Track DCC-EX command implementations needed for JMRI integration  
**Priority**: High - JMRI is primary control software for model railroad automation

---

## Overview

JMRI (Java Model Railroad Interface) queries several DCC-EX commands during startup and operation. Currently, PicoDCC doesn't implement all query responses, causing unnecessary delays and potential compatibility issues.

**Current Impact**:
- ~9 second JMRI startup delay (3 timeouts × 3 seconds each)
- Queries for unsupported features (turnouts, automation, roster)
- Missing status information commands

**Goal**: Implement minimal responses to eliminate timeouts while maintaining honest capability reporting.

---

## Priority 1: Eliminate JMRI Startup Delays (Quick Wins)

### 1. Empty List Responses ⚠️ **HIGH PRIORITY**
**Impact**: Eliminates 9-second startup delay

| Command | Current Behavior | Required Behavior | Effort | Status |
|---------|------------------|-------------------|--------|--------|
| `<JT>` | Times out (3s) | Return `<jT>` (empty turnout list) | Low | 🔄 TODO |
| `<JA>` | Times out (3s) | Return `<jA>` (empty automation list) | Low | 🔄 TODO |
| `<JR>` | Times out (3s) | Return `<jR>` (empty roster list) | Low | 🔄 TODO |

**Implementation Notes**:
- Add parsing for `<JT>`, `<JA>`, `<JR>` in `PicoDCCEX::parseCommand()`
- Return lowercase response immediately (no processing needed)
- No data structures required - these are "feature not supported" responses
- **Estimated Time**: 1-2 hours (simple response addition)

**Files to Modify**:
- `lib/PicoDCCEX/pico_dccex.cpp` - Add command parsing
- `test/pico_dcc_dccex_tests.cpp` - Add test cases for empty list responses

---

## Priority 2: Basic System Information Commands

### 2. Status Query Commands ⚠️ **MEDIUM PRIORITY**
**Impact**: Provides JMRI with system information, enables monitoring

| Command | Current Behavior | Required Behavior | Effort | Status |
|---------|------------------|-------------------|--------|--------|
| `<s>` | Not implemented | Return version, track status, mode | Medium | 🔄 TODO |
| `<#>` | Not implemented | Return locomotive capacity (e.g., 50 locos) | Low | 🔄 TODO |

**`<s>` Response Format** (DCC-EX standard):
```
<iDCC-EX V-5.0.0 / PICODCC / BUILD Oct 20 2025>
<p1 MAIN> or <p0 MAIN>
<p1 PROG> or <p0 PROG>
<iM> if in Layout Maintenance Mode (new)
<u> if unsaved config changes (new)
```

**`<#>` Response Format**:
```
<# 50>    # Locomotive capacity
```

**Implementation Notes**:
- `<s>` requires version string, track power states from `PicoDCCController`
- `<s>` should include operation mode and unsaved changes indicators (PicoDCC extension)
- `<#>` can return static capacity value (50 locomotives is reasonable)
- **Estimated Time**: 3-4 hours (requires component coordination)

**Files to Modify**:
- `lib/PicoDCCEX/pico_dccex.cpp` - Add command parsing
- `lib/PicoDCCController/pico_dcc_controller.cpp` - Add status query methods
- `lib/PicoConfigStorage/pico_config_storage.h` - Add `hasUnsavedChanges()` accessor
- `test/pico_dcc_dccex_tests.cpp` - Add test cases for status queries

---

## Priority 3: Error Responses for Unsupported Features

### 3. Turnout/Sensor/Roster Modification Commands 🟡 **LOW PRIORITY**
**Impact**: Clarifies unsupported features, prevents JMRI confusion

| Command Pattern | Current Behavior | Required Behavior | Effort | Status |
|-----------------|------------------|-------------------|--------|--------|
| `<T id addr subaddr>` | Ignored? | Return `<X>` error | Low | 🔄 TODO |
| `<S id pin pullup>` | Ignored? | Return `<X>` error | Low | 🔄 TODO |
| `<+ cab func>` | Ignored? | Return `<X>` error | Low | 🔄 TODO |
| `<- cab>` | Implemented (releases cab) | Mirror `<- cab>` acknowledgment | Low | ✅ DONE |

**Implementation Notes**:
- Detect command patterns in `PicoDCCEX::parseCommand()`
- Return `<X>` error response (DCC-EX standard for unsupported operations)
- **Estimated Time**: 2-3 hours (pattern matching + tests)

**Files to Modify**:
- `lib/PicoDCCEX/pico_dccex.cpp` - Add command pattern detection
- `test/pico_dcc_dccex_tests.cpp` - Add test cases for error responses

---

## Priority 4: Current Monitoring API (Optional Enhancement)

### 4. Current Monitoring Command 🟢 **OPTIONAL**
**Impact**: Exposes existing hardware capability to JMRI

| Command | Current Behavior | Required Behavior | Effort | Status |
|---------|------------------|-------------------|--------|--------|
| `<c>` | Not implemented | Return main/prog track current readings | Medium | 🔄 TODO |

**`<c>` Response Format** (DCC-EX standard):
```
<c "CURRENT" 1234 567 3000 250>
# main_current(mA) prog_current(mA) main_limit(mA) prog_limit(mA)
```

**Implementation Notes**:
- Hardware already reads current via ADC (`PicoDCCTrack`)
- Need to add API to query current values from tracks
- Apply `adc_to_ma_conversion` calibration factor
- **Estimated Time**: 4-5 hours (API design + integration + tests)

**Files to Modify**:
- `lib/PicoDCCTrack/pico_dcctrack.h` - Add `getCurrentMilliamps()` method
- `lib/PicoDCCEX/pico_dccex.cpp` - Add command parsing
- `lib/PicoDCCController/pico_dcc_controller.cpp` - Query both tracks
- `test/pico_dcc_dccex_tests.cpp` - Add test cases

---

## Implementation Strategy

### Phase 1: Quick Wins (Eliminate JMRI Delay)
**Estimated Time**: 1-2 hours  
**Focus**: Empty list responses (`<JT>`, `<JA>`, `<JR>`)

**Steps**:
1. Add command parsing for `JT`, `JA`, `JR` patterns
2. Return lowercase empty list responses immediately
3. Add test cases for all three commands
4. Test with JMRI to verify startup delay eliminated

**Success Criteria**:
- JMRI startup time reduced by ~9 seconds
- No timeout warnings in JMRI console

### Phase 2: System Status (JMRI Monitoring)
**Estimated Time**: 3-4 hours  
**Focus**: Status query commands (`<s>`, `<#>`)

**Steps**:
1. Implement `<#>` with static capacity value
2. Design status query API for `PicoDCCController`
3. Implement `<s>` with version, track power, mode, unsaved flag
4. Add comprehensive test coverage
5. Test with JMRI system information panel

**Success Criteria**:
- JMRI displays correct version information
- JMRI shows track power status accurately
- Layout Maintenance Mode visible in status (PicoDCC extension)

### Phase 3: Error Handling (Feature Clarity)
**Estimated Time**: 2-3 hours  
**Focus**: Error responses for unsupported commands

**Steps**:
1. Catalog all turnout/sensor/roster command patterns
2. Add pattern detection in command parser
3. Return `<X>` errors for all unsupported operations
4. Add test coverage for error cases

**Success Criteria**:
- JMRI receives clear error responses
- No silent failures or hangs when using unsupported features

### Phase 4: Current Monitoring API (Optional)
**Estimated Time**: 4-5 hours  
**Focus**: Expose existing hardware monitoring

**Steps**:
1. Design `getCurrentMilliamps()` API for `PicoDCCTrack`
2. Implement command parsing for `<c>`
3. Query both tracks and format response
4. Add test coverage including calibration factor application

**Success Criteria**:
- JMRI displays real-time current readings
- Values match physical measurements (validate calibration)

---

## Testing Plan

### Unit Tests
- **Command Parsing**: Verify each new command is recognized
- **Response Format**: Validate DCC-EX protocol compliance
- **Error Cases**: Test unsupported command handling
- **State Queries**: Verify status information accuracy

### Integration Tests
- **JMRI Startup**: Measure startup time before/after changes
- **JMRI Monitoring**: Verify system information display
- **Error Handling**: Test unsupported feature workflows in JMRI

### Hardware Tests
- **Current Monitoring**: Validate ADC readings vs. physical measurements
- **Mode Indicators**: Verify Layout Maintenance Mode reporting
- **Track Power Status**: Confirm power state accuracy

---

## DCC-EX Protocol Compliance Notes

### Intentionally Unsupported Features
These features are **NOT** planned for implementation:

1. **Turnout Control**: PicoDCC focuses on locomotive control only
2. **Sensor/Occupancy Detection**: No hardware inputs for sensors
3. **Automation/Routes**: No route programming capability
4. **Roster Management**: JMRI handles roster, not command station
5. **Main Track CV Programming**: Safety concern (use programming track)

**Approach**: Return empty lists or error responses to clarify limitations.

### PicoDCC-Specific Extensions
Based on JMRI testing (October 20, 2025):
- Random text is **silently ignored** by JMRI
- `<U ...>` shows as **unrecognized command** in JMRI log (could be used for debug)
- `<X>` correctly interpreted as **operation failed** 
- Protocol extensions require **Java driver fork** (not worth the effort)

**Implemented Extensions**:
1. **`<D ACK LIMIT/MIN/MAX>` commands**: Runtime ACK parameter adjustment
   - Uses diagnostic namespace (`<D ...>`)
   - JMRI ignores diagnostic commands
   - No JMRI log pollution

2. **`<E>` command**: Save configuration (maintenance mode only)
   - Simple command, unlikely to conflict
   - Returns standard `<X>` error or custom `<e SAVED>` response

**Rejected Extensions** (originally planned, now removed):
- ~~`<iM>`/`<iN>` mode indicators~~ - Removed from `<s>` response
- ~~`<u>` unsaved changes indicator~~ - Removed from `<s>` response
- ~~ACK config display in `<s>`~~ - Removed from `<s>` response

**Design Decision**: 
- Mode and unsaved changes displayed **on LCD only**
- No DCC-EX protocol pollution
- Maintains clean JMRI compatibility
- No Java driver modifications required

**Rationale**: Keep JMRI logs clean, avoid confusion, maintain standard protocol compliance where possible.

---

## Estimated Total Effort

| Phase | Tasks | Estimated Time | Priority |
|-------|-------|----------------|----------|
| Phase 1 | Empty list responses | 1-2 hours | ⚠️ HIGH |
| Phase 2 | System status queries | 3-4 hours | 🟡 MEDIUM |
| Phase 3 | Error responses | 2-3 hours | 🟡 LOW |
| Phase 4 | Current monitoring | 4-5 hours | 🟢 OPTIONAL |
| **Total** | | **10-14 hours** | |

**Recommended Sequence**: Phase 1 → Phase 2 → [Pause for user feedback] → Phase 3 → Phase 4

---

## Success Metrics

1. **Startup Time**: JMRI startup delay reduced from ~12s to ~3s (9s improvement)
2. **Protocol Compliance**: All implemented commands follow DCC-EX standard format
3. **Test Coverage**: All new commands have unit test coverage
4. **JMRI Compatibility**: No errors/warnings in JMRI system console
5. **User Experience**: Clear feedback for unsupported features (no silent failures)

---

## References

- **DCC-EX Command Reference**: https://dcc-ex.com/reference/software/command-reference.html
- **JMRI DCC-EX Integration**: https://www.jmri.org/help/en/html/hardware/dccpp/index.shtml
- **PicoDCC DCC-EX Compliance Analysis**: `docs/dccex-compliance-analysis.md`

---

**Status**: 🔄 **Planning Complete** - Ready for Phase 1 implementation  
**Last Updated**: October 20, 2025  
**Next Action**: Implement Phase 1 (empty list responses) to eliminate JMRI startup delay
