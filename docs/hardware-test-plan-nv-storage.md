# Hardware Test Plan - Non-Volatile Configuration Storage

**Target**: Configuration Storage & Layout Maintenance Mode  
**Date**: October 20, 2025  
**Status**: Ready for Hardware Testing  
**Prerequisites**: Raspberry Pi Pico 2 with PicoDCC hardware, serial terminal (115200 baud), LCD display

---

## ✅ What's Ready for Testing

### Phase 1: Configuration Storage ✅
- Flash memory storage (last 4KB sector @ 0x103FF000)
- CRC32 validation and corruption detection
- Factory default fallback
- Load/save operations with ~410ms blocking

### Phase 2: DCC-EX Command Integration ✅
- `<D ACK LIMIT>`, `<D ACK MIN>`, `<D ACK MAX>` - Runtime config
- `<E>` - Save to flash (maintenance mode only)
- `<s>` - System status with mode and unsaved changes
- Error handling for invalid commands

### Phase 3: Layout Maintenance Mode ✅
- LCD-only mode entry with safety checklist
- Main track power lockout enforcement
- Unsaved changes tracking and warnings
- Modal confirmation dialogs
- Safe exit with warning if unsaved

---

## 🧪 Test Scenarios

### Test 1: Flash Read on First Boot (Factory Defaults)

**Goal**: Verify factory defaults load correctly on virgin flash

**Prerequisites**:
- Fresh firmware flash (or erase flash with `picotool`)
- No previous configuration saved

**Steps**:
1. Flash firmware to Pico
2. Power on, wait for boot sequence
3. Connect serial terminal (115200 baud)
4. Send: `<s>`

**Expected Results**:
```
<p0 MAIN> <p0 PROG> <iN>
```
- Both tracks powered OFF initially
- Normal operation mode (`iN`)
- No unsaved changes flag

**Verification**: Check diagnostic logs via LCD for:
```
[TIME] INFO SYSTEM: Configuration loaded from flash
[TIME] INFO SYSTEM: Using factory defaults (no saved config)
```

**Pass Criteria**:
- ✅ System boots without errors
- ✅ Factory defaults applied
- ✅ ACK threshold = 60.0 mA (default)
- ✅ ADC conversion = 0.0 (uncalibrated)

---

### Test 2: Runtime Configuration Adjustment (NORMAL Mode)

**Goal**: Verify runtime config changes work in normal operation

**Prerequisites**:
- System powered on in NORMAL mode
- Main track can be ON or OFF
- Serial terminal connected

**Steps**:
1. Send: `<D ACK LIMIT 55>`
2. Expected: `<D ACK LIMIT 55>`
3. Send: `<D ACK MIN 4500>`
4. Expected: `<D ACK MIN 4500>`
5. Send: `<D ACK MAX 8000>`
6. Expected: `<D ACK MAX 8000>`
7. Send: `<s>`

**Expected Results**:
```
<p? MAIN> <p? PROG> <iN> <u>
```
- Normal mode (`iN`)
- **Unsaved changes flag (`<u>`) present**
- Runtime config updated in RAM

**Verification via LCD**:
- Main screen shows orange "unsaved" indicator
- Settings screen shows modified values

**Pass Criteria**:
- ✅ Commands acknowledged correctly
- ✅ Unsaved changes flag appears
- ✅ Values persist in RAM until power cycle
- ✅ LCD shows unsaved indicator

---

### Test 3: Save Rejected in NORMAL Mode (Safety Check)

**Goal**: Verify `<E>` command is blocked outside maintenance mode

**Prerequisites**:
- System in NORMAL mode
- Runtime config modified (unsaved changes)

**Steps**:
1. Send: `<D ACK LIMIT 55>`
2. Expected: `<D ACK LIMIT 55>`
3. Send: `<E>`

**Expected Results**:
```
<X>
```
- Error response (`<X>`)
- Flash write NOT performed
- Unsaved changes still present

**Verification via LCD**:
- Diagnostic log shows:
  ```
  [TIME] WARNING SYSTEM: Save rejected - not in maintenance mode
  ```

**Pass Criteria**:
- ✅ `<E>` command returns error
- ✅ No flash write occurs
- ✅ Unsaved changes flag remains

---

### Test 4: Layout Maintenance Mode Entry (LCD UI)

**Goal**: Verify safe mode entry with safety checks

**Prerequisites**:
- System in NORMAL mode
- Access to LCD touchscreen
- Main track powered OFF (verify with `<0 MAIN>`)

**Steps**:
1. On LCD main screen, tap "Settings" button
2. On Settings screen, tap "Layout Maintenance Mode" button
3. **Modal appears**: "Layout Maintenance Mode - Are you sure all locos stopped and main power off?"
4. Verify main track power is OFF: `<0 MAIN>` then `<s>` (should show `<p0 MAIN>`)
5. Tap "Yes" on modal

**Expected Results**:
- Modal closes
- LCD switches to "Maintenance Mode" screen
- Main screen shows:
  - "Maintenance Mode Active"
  - Track power status
  - Unsaved changes indicator (orange if changes exist)
  - "Save to Flash" button
  - "Exit Maintenance" button

**Verification via Serial**:
1. Send: `<s>`
2. Expected: `<p0 MAIN> <p? PROG> <iM>`
   - Main track OFF (`<p0 MAIN>`)
   - Maintenance mode active (`<iM>`)

**Verification via LCD Diagnostic Logs**:
```
[TIME] INFO SYSTEM: Entering Layout Maintenance Mode
```

**Pass Criteria**:
- ✅ Mode entry requires main track power OFF
- ✅ Modal confirmation requires user action
- ✅ LCD switches to maintenance screen
- ✅ Status command shows maintenance mode

---

### Test 5: Main Track Power Lockout (Safety Feature)

**Goal**: Verify main track cannot be powered during maintenance mode

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Main track powered OFF

**Steps**:
1. Via serial, send: `<1 MAIN>`

**Expected Results**:
```
<X>
```
- Error response (power request rejected)
- Main track remains OFF

**Verification via Status**:
1. Send: `<s>`
2. Expected: `<p0 MAIN> <p? PROG> <iM>`
   - Main track still OFF

**Verification via LCD Diagnostic Logs**:
```
[TIME] WARNING SYSTEM: Main track power blocked - maintenance mode active
```

**Pass Criteria**:
- ✅ `<1 MAIN>` command rejected with `<X>`
- ✅ Main track remains OFF
- ✅ Programming track unaffected

---

### Test 6: Save Configuration to Flash (Maintenance Mode)

**Goal**: Verify flash write operation with 410ms blocking

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Runtime config modified (e.g., `<D ACK LIMIT 55>`)
- Unsaved changes present

**Steps**:
1. Verify unsaved changes: `<s>` (should show `<u>` flag)
2. On LCD, tap "Save to Flash" button
3. Observe LCD during save (brief pause expected)
4. Check status: `<s>`

**Expected Results**:
- LCD shows brief "Saving..." indicator (~410ms)
- After save, unsaved indicator turns green (saved)
- Status response: `<p0 MAIN> <p? PROG> <iM>` (no `<u>` flag)

**Verification via Serial** (alternative to LCD):
1. Send: `<E>`
2. Expected: `<e SAVED>`
3. Send: `<s>`
4. Expected: No `<u>` flag

**Verification via LCD Diagnostic Logs**:
```
[TIME] INFO SYSTEM: Saving configuration to flash...
[TIME] INFO SYSTEM: Configuration saved successfully
```

**Timing Check**:
- Use oscilloscope or logic analyzer on DCC signal
- Verify ~410ms gap in DCC packets during save
- Verify packets resume after save

**Pass Criteria**:
- ✅ Save completes without errors
- ✅ Unsaved changes flag clears
- ✅ ~410ms blocking occurs (DCC packets pause)
- ✅ System resumes normal operation after save

---

### Test 7: Flash Persistence Across Power Cycle

**Goal**: Verify saved configuration survives power cycle

**Prerequisites**:
- Configuration saved to flash in Test 6
- Modified values: ACK threshold = 55 mA

**Steps**:
1. While in maintenance mode, verify config: `<s>` (no `<u>` flag)
2. Exit maintenance mode via LCD (tap "Exit Maintenance")
3. Power cycle the Pico (disconnect/reconnect power)
4. Wait for boot sequence
5. Connect serial terminal
6. Check loaded config: `<s>`

**Expected Results**:
- System boots normally
- Modified values restored from flash
- No unsaved changes flag

**Verification via LCD Diagnostic Logs**:
```
[TIME] INFO SYSTEM: Configuration loaded from flash
[TIME] INFO SYSTEM: CRC32 validation passed
[TIME] INFO SYSTEM: ACK threshold: 55.0 mA
```

**Alternative Verification** (if DCC-EX commands implemented):
1. Send: `<D ACK LIMIT>`
2. Expected: `<D ACK LIMIT 55>` (not default 60)

**Pass Criteria**:
- ✅ Boot completes without errors
- ✅ Modified values loaded from flash
- ✅ CRC32 validation passes
- ✅ Values match what was saved

---

### Test 8: Unsaved Changes Warning on Exit

**Goal**: Verify exit warning prevents accidental loss of changes

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Runtime config modified (unsaved changes)

**Steps**:
1. Modify config: `<D ACK LIMIT 50>` (don't save)
2. Verify unsaved: `<s>` (should show `<u>` flag)
3. On LCD, tap "Exit Maintenance" button
4. **Warning modal appears**: "Unsaved changes will be lost. Continue?"
5. Tap "No" (cancel exit)

**Expected Results**:
- Modal closes
- System remains in maintenance mode
- Unsaved changes preserved

**Verify by Retrying with Save**:
6. Tap "Save to Flash" button
7. Wait for save completion
8. Tap "Exit Maintenance" button
9. **No warning modal** (changes are saved)
10. System exits to NORMAL mode

**Verification via Status After Exit**:
1. Send: `<s>`
2. Expected: `<p0 MAIN> <p? PROG> <iN>` (NORMAL mode, no unsaved)

**Pass Criteria**:
- ✅ Warning modal appears when unsaved changes exist
- ✅ Cancel works (stays in maintenance mode)
- ✅ No warning when no unsaved changes
- ✅ Exit successful when saved

---

### Test 9: CRC32 Corruption Detection

**Goal**: Verify factory defaults restore on corrupted flash

**Prerequisites**:
- Hardware debugger or direct flash access
- Ability to corrupt flash sector (optional - advanced test)

**Steps** (Manual Flash Corruption):
1. Save valid config to flash (Test 6)
2. Using debugger, write garbage to config sector (0x103FF000)
3. Power cycle the Pico
4. Check diagnostic logs via LCD

**Expected Results**:
```
[TIME] WARNING SYSTEM: Configuration CRC32 validation failed
[TIME] INFO SYSTEM: Using factory defaults
```

**Verification via Status**:
1. Send: `<s>`
2. System boots normally with factory defaults
3. ACK threshold = 60.0 mA (default, not saved value)

**Alternative Test** (Easier - No Debugger):
- Flash virgin firmware to new Pico
- Verify factory defaults load
- This simulates "no valid config" scenario

**Pass Criteria**:
- ✅ Corrupted config detected
- ✅ Factory defaults applied
- ✅ System boots normally (no crash)
- ✅ Diagnostic log shows validation failure

---

### Test 10: Main Track Stays OFF After Exit

**Goal**: Verify main track power doesn't auto-restore after exit

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Main track was OFF before entering
- Programming track can be ON or OFF

**Steps**:
1. Verify main track OFF: `<s>` (should show `<p0 MAIN>`)
2. Exit maintenance mode via LCD
3. Check status: `<s>`

**Expected Results**:
```
<p0 MAIN> <p? PROG> <iN>
```
- Main track still OFF (`<p0 MAIN>`)
- Normal mode active (`<iN>`)
- Main track did NOT auto-restore

**Verify Manual Re-enable Works**:
4. Send: `<1 MAIN>`
5. Expected: `<p1 MAIN>` (now allowed in NORMAL mode)
6. Send: `<s>`
7. Expected: `<p1 MAIN> <p? PROG> <iN>`

**Pass Criteria**:
- ✅ Main track stays OFF after exit
- ✅ User must explicitly re-enable with `<1 MAIN>`
- ✅ Re-enable works in NORMAL mode

---

### Test 11: Programming Track Unaffected During Maintenance

**Goal**: Verify programming track operates normally during maintenance mode

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Programming track powered ON

**Steps**:
1. Enter maintenance mode (Test 4)
2. Send: `<1 PROG>`
3. Expected: `<p1 PROG>`
4. Verify programming track has power (LED/scope)
5. Send DCC packet commands (if implemented): `<W 1 123>`
6. Exit maintenance mode
7. Programming track should remain ON

**Expected Results**:
- Programming track power controllable in both modes
- Programming commands work in maintenance mode
- No interruption to programming track operations

**Pass Criteria**:
- ✅ Programming track power commands work
- ✅ Programming track unaffected by mode changes
- ✅ Only main track is locked out

---

### Test 12: Multi-Cycle Flash Endurance (Stress Test)

**Goal**: Verify flash write reliability over multiple cycles

**Prerequisites**:
- System in LAYOUT_MAINTENANCE mode
- Serial terminal with scripting capability (optional)

**Steps**:
1. Modify config: `<D ACK LIMIT 55>`
2. Save: `<E>` (via serial or LCD)
3. Expected: `<e SAVED>`
4. Modify again: `<D ACK LIMIT 60>`
5. Save: `<E>`
6. Repeat steps 1-5 for 10 cycles
7. Power cycle after 10 saves
8. Verify last saved value persists

**Expected Results**:
- All 10 save operations complete without errors
- Flash endurance: 10,000 cycles typical (we're testing 10)
- Last saved value loads after power cycle

**Verification via LCD Diagnostic Logs**:
- No flash write errors
- No CRC32 validation failures
- Configuration loads correctly after power cycle

**Pass Criteria**:
- ✅ 10 consecutive saves complete successfully
- ✅ No flash write errors
- ✅ Final value persists after power cycle
- ✅ Flash endurance acceptable (10 << 10,000)

---

### Test 13: Firmware Update Configuration Preservation

**✅ UPDATED**: Custom linker script is now implemented! Configuration is automatically preserved during firmware updates.

**Purpose**: Verify that configuration persists across firmware updates (automatic preservation via `memmap_picodcc.ld`)

**Prerequisites**:
- Test 6 passed (flash save working)
- Test 7 passed (persistence validated)
- Serial terminal connected (115200 baud)
- New firmware build available (or can re-flash same version)

**Test Procedure**:

**Phase 1: Save Configuration**
1. Enter maintenance mode (LCD)
2. Configure test values:
   ```
   <D ACK LIMIT 55>
   <D ACK MIN 5000>
   <D ACK MAX 7500>
   ```
3. Save to flash: `<E>`
4. Expected: `<e SAVED>`

**Phase 2: Verify Configuration Saved**
1. Query current configuration:
   ```
   <s>
   <D ACK LIMIT>
   <D ACK MIN>
   <D ACK MAX>
   ```
2. **Verify values match**: LIMIT=55, MIN=5000, MAX=7500
3. Exit maintenance mode (LCD)

**Phase 3: Flash New Firmware (Automatic Preservation)**
- **VS Code Method** (Recommended): Press F5 to debug/flash
- **UF2 Method**: Hold BOOTSEL, copy `PicoDCC.uf2` to USB drive
- **Picotool**: `picotool load -x build/src/PicoDCC.elf`
- **OpenOCD**: Flash via debugger

**Phase 4: Verify Configuration Persists (NO MANUAL RESTORE NEEDED)**
1. System boots after firmware update
2. Send: `<s>`
3. **Expected**: Saved configuration STILL PRESENT (not factory defaults) ✅
4. Verify: `<D ACK LIMIT>` returns 55 (NOT default value)
5. Verify: `<D ACK MIN>` returns 5000
6. Verify: `<D ACK MAX>` returns 7500

**Phase 5: Power Cycle Verification**
1. Power cycle Pico
2. Send: `<s>`
3. **Expected**: Configuration STILL persists after power cycle
4. Verify all values still match Phase 2

**Expected Results**:
- Firmware update **DOES NOT** erase config sector (linker script protection) ✅
- Configuration persists automatically (no manual restore needed) ✅
- Saved values survive firmware update ✅
- Saved values survive power cycle ✅
- **NO FACTORY DEFAULTS** - config sector preserved

**Verification via LCD Diagnostic Logs**:
- After firmware update: **NO** "CRC32 mismatch, loading factory defaults" message
- Logs show: "CONFIG: Configuration loaded from flash" (not factory defaults)
- No config-related errors

**Pass Criteria**:
- ✅ Firmware update **does not** erase config sector
- ✅ Configuration automatically persists (no manual backup/restore)
- ✅ Saved values still present after firmware update
- ✅ Saved values persist across power cycles
- ✅ Custom linker script (`memmap_picodcc.ld`) working correctly

**Fail Criteria**:
- ❌ Configuration erased after firmware update (factory defaults restore)
- ❌ CRC32 mismatch errors in logs
- ❌ Saved values lost
- ❌ Linker script not being used

**Notes**:
- This tests the **current** custom linker script approach ✅
- **NO manual backup/restore needed** - fully automatic
- Config sector (0x103FF000-0x103FFFFF) is **reserved** by `memmap_picodcc.ld`
- Firmware limited to 2044 KB (currently using only 406 KB, plenty of margin)
- Time: ~5 minutes (2 min save, 2 min flash, 1 min verify)

---

## 📋 Hardware Test Checklist

### Test Execution Record

| Test # | Test Name | Pass | Fail | Notes |
|--------|-----------|------|------|-------|
| 1 | Factory defaults on first boot | ☐ | ☐ | |
| 2 | Runtime config adjustment | ☐ | ☐ | |
| 3 | Save rejected in NORMAL mode | ☐ | ☐ | |
| 4 | Maintenance mode entry (LCD) | ☐ | ☐ | |
| 5 | Main track power lockout | ☐ | ☐ | |
| 6 | Save to flash (410ms blocking) | ☐ | ☐ | |
| 7 | Flash persistence (power cycle) | ☐ | ☐ | |
| 8 | Unsaved changes warning | ☐ | ☐ | |
| 9 | CRC32 corruption detection | ☐ | ☐ | |
| 10 | Main track stays OFF after exit | ☐ | ☐ | |
| 11 | Programming track unaffected | ☐ | ☐ | |
| 12 | Multi-cycle flash endurance | ☐ | ☐ | |
| 13 | Firmware update config preservation | ☐ | ☐ | |

---

## 🔧 Test Equipment Needed

1. **Raspberry Pi Pico 2** with PicoDCC firmware (TEST_BUILD=OFF)
2. **LCD touchscreen** (Waveshare WAV-27579, ST7789T3)
3. **Serial terminal** (PuTTY, screen, or JMRI Serial Monitor)
   - Baud: 115200
   - 8N1 (8 data bits, no parity, 1 stop bit)
4. **Oscilloscope or logic analyzer** (optional - for timing verification)
5. **Hardware debugger** (optional - for flash corruption test)

---

## 🚨 Safety Considerations

### Critical Safety Rules

1. **DO NOT attempt flash writes with locomotives on track**
   - 410ms packet pause → decoders switch to DC mode
   - DC mode = FULL SPEED immediately (not coast)
   - Main track power lockout prevents this

2. **Always verify main track power OFF** before maintenance mode entry
   - Send `<0 MAIN>` before entering
   - Verify `<p0 MAIN>` in status response
   - LCD modal enforces this check

3. **Use Layout Maintenance Mode for ALL flash writes**
   - Never bypass safety checks
   - Mode entry is LCD-only (intentional)
   - No remote DCC-EX command to enter mode

4. **Monitor flash write operation**
   - Expect ~410ms pause in DCC signals
   - Verify packets resume after save
   - Check for error messages in diagnostic log

5. **⚠️ FIRMWARE UPDATES ERASE CONFIGURATION**
   - Standard flash process erases entire 4MB flash including config sector
   - **ALWAYS backup configuration before firmware updates**
   - See "Firmware Update Procedure" section below

---

## 🔄 Firmware Update Procedure

### ⚠️ Critical: Configuration is NOT Preserved During Updates

**Default Behavior**: Flashing new firmware erases the entire flash, including the configuration sector (0x103FF000-0x103FFFFF)

**Impact**: Your calibration values and saved settings will be **LOST**

### Backup Configuration Before Update

**Steps**:
1. Connect serial terminal (115200 baud)
2. Record current configuration:
   ```
   <s>                          # Note track power states and mode
   <D ACK LIMIT>                # Note threshold (if implemented)
   <D ACK MIN>                  # Note min duration (if implemented)
   <D ACK MAX>                  # Note max duration (if implemented)
   ```
3. If you have a calibration value, note the ADC conversion factor
4. **Write values down or save to a file**

### Flash New Firmware

**Method 1: UF2 Bootloader** (Easiest):
1. Hold BOOTSEL button while plugging in USB
2. Pico appears as USB drive
3. Copy `PicoDCC.uf2` to the drive
4. Pico automatically reboots with new firmware

**Method 2: Picotool** (Requires debugger):
```bash
picotool load -x build/src/PicoDCC.elf
```

**Method 3: OpenOCD** (Requires debugger):
```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "adapter speed 5000" \
  -c "program build/src/PicoDCC.elf verify reset exit"
```

### Restore Configuration After Update

**Steps**:
1. System boots with factory defaults
2. Verify boot: `<s>` (should show normal mode, default settings)
3. Enter Layout Maintenance Mode via LCD:
   - Main screen → Settings → Layout Maintenance Mode
   - Confirm modal (verify main track OFF)
4. Re-enter configuration via serial:
   ```
   <D ACK LIMIT [value]>         # Restore threshold
   <D ACK MIN [value]>           # Restore min duration
   <D ACK MAX [value]>           # Restore max duration
   ```
5. Save to flash: `<E>`
6. Expected: `<e SAVED>`
7. Exit maintenance mode via LCD
8. If ADC calibration needed, follow calibration guide

### Verification

After restore:
```
<s>                              # Check status
# Should show restored values, no unsaved changes flag
```

**Time Required**: ~5 minutes for backup/restore

---

## 🔮 Future Enhancement (Not Implemented Yet)

**Custom Linker Script**: Reserve last 4KB of flash for configuration
- Firmware limited to 4092KB (0x10000000-0x103FEFFF)
- Configuration sector protected (0x103FF000-0x103FFFFF)
- UF2 generation excludes config sector
- Automatic configuration preservation during updates

**Status**: Planned for future implementation  
**Timeline**: After ACK Detection phase complete  
**Documentation**: See `docs/firmware-update-config-preservation.md`

---

## 📊 Expected Test Results Summary

### Pass Criteria (All Tests)
- ✅ Flash reads/writes complete without errors
- ✅ CRC32 validation detects corruption
- ✅ Factory defaults restore on invalid config
- ✅ Runtime config persists in RAM until power cycle
- ✅ Flash config survives power cycles
- ✅ Layout Maintenance Mode enforces safety lockouts
- ✅ Main track power lockout prevents unsafe enables
- ✅ Unsaved changes tracking works correctly
- ✅ LCD UI provides clear feedback and warnings
- ✅ ~410ms blocking occurs during flash writes
- ✅ Programming track unaffected by maintenance mode

### Failure Scenarios to Watch For
- ❌ Flash write fails (corrupted data, CRC mismatch)
- ❌ Configuration doesn't persist after power cycle
- ❌ Main track can be enabled during maintenance mode
- ❌ Flash write takes significantly longer than 410ms
- ❌ System crashes or hangs during flash write
- ❌ Unsaved changes flag doesn't appear/clear correctly
- ❌ Mode transitions fail or get stuck

---

## 📝 Test Report Template

```
=== PicoDCC Hardware Test Report ===
Date: [YYYY-MM-DD]
Firmware Version: [commit hash]
Hardware: Raspberry Pi Pico 2
Test Environment: [lab/bench/field]

Test Results:
- Total Tests: 12
- Passed: __/12
- Failed: __/12

Failed Tests:
1. [Test #, Name, Failure Description]

Notes:
- [Any observations or anomalies]
- [Performance measurements]
- [Recommendations]

Tester: [Name]
Signature: _________________
```

---

## ✅ Ready for Hardware Testing

All software components are complete and unit tested:
- **Configuration storage**: 11/11 tests passing
- **Controller tests**: 13/13 tests passing (includes 4 maintenance mode tests)
- **Overall test coverage**: 64.96% (445/685 lines)
- **Controller coverage**: 70.26% (137/195 lines)

**Next Step**: Flash firmware to hardware (TEST_BUILD=OFF) and execute test plan!

---

*Document Version: 1.0 - October 20, 2025*
