# Hardware Testing Quick Reference Card

## 🎯 What's Ready to Test

**All configuration storage and Layout Maintenance Mode features are ready for hardware testing!**

---

## ✅ Features Implemented

1. **Flash Storage** - Configuration persists across power cycles (last 4KB of flash)
2. **Runtime Config** - Adjustable ACK parameters via `<D ACK ...>` commands
3. **Persistent Config** - Calibration and limits saved via `<E>` command
4. **Layout Maintenance Mode** - Safe state for flash writes (LCD-only entry)
5. **Safety Lockouts** - Main track power disabled during maintenance mode
6. **Unsaved Changes** - Tracking and warnings before exit
7. **CRC32 Validation** - Detects flash corruption, restores factory defaults

---

## 🧪 13 Hardware Tests

| # | Test Name | What It Validates |
|---|-----------|-------------------|
| 1 | Factory defaults | Flash read on virgin device |
| 2 | Runtime config | `<D ACK>` commands in NORMAL mode |
| 3 | Save rejection | `<E>` blocked outside maintenance mode |
| 4 | Mode entry | LCD UI safety checks |
| 5 | Power lockout | Main track disabled in maintenance |
| 6 | Flash save | 410ms write with DCC pause |
| 7 | Persistence | Config survives power cycle |
| 8 | Exit warning | Prevents accidental data loss |
| 9 | CRC32 check | Corruption detection |
| 10 | Exit behavior | Main track stays OFF |
| 11 | Prog track | Unaffected by maintenance mode |
| 12 | Endurance | Multiple flash writes |
| 13 | Firmware update | Manual backup/restore procedure |

**Full test procedures**: See `docs/hardware-test-plan-nv-storage.md`

---

## 🔌 Test Equipment

- Raspberry Pi Pico 2 with PicoDCC hardware
- LCD touchscreen (Waveshare WAV-27579)
- Serial terminal (115200 baud, 8N1)
- Optional: Oscilloscope (verify 410ms timing)

---

## 📝 Key Commands for Testing

### Status Query
```
<s>                     System status (mode, power, unsaved changes)
```
**Response Examples**:
- `<p0 MAIN> <p1 PROG> <iN>` - Normal mode, main OFF, prog ON
- `<p0 MAIN> <p1 PROG> <iM>` - Maintenance mode
- `<p0 MAIN> <p1 PROG> <iN> <u>` - Normal mode with unsaved changes

### Runtime Config (NORMAL mode)
```
<D ACK LIMIT 55>        Set ACK threshold to 55mA (runtime only)
<D ACK MIN 4500>        Set ACK min duration to 4500µs
<D ACK MAX 8000>        Set ACK max duration to 8000µs
```

### Save to Flash (MAINTENANCE mode only)
```
<E>                     Save runtime config to flash
```
**Response**:
- `<e SAVED>` - Success (in maintenance mode)
- `<X>` - Error (not in maintenance mode)

### Track Power Control
```
<0>                     Power OFF both tracks
<1>                     Power ON both tracks
<0 MAIN>                Power OFF main track
<1 MAIN>                Power ON main track (blocked in maintenance)
<0 PROG>                Power OFF programming track
<1 PROG>                Power ON programming track
```

---

## 🖥️ LCD Navigation

### Enter Maintenance Mode
1. Main screen → Tap "Settings"
2. Settings screen → Tap "Layout Maintenance Mode"
3. Modal: Confirm "Yes" (after verifying locos stopped & main power OFF)
4. Maintenance screen appears

### Exit Maintenance Mode
1. Maintenance screen → Tap "Exit Maintenance"
2. If unsaved changes, modal warns: "Unsaved changes will be lost"
3. Confirm exit or cancel to save first

### Save Configuration
1. In maintenance mode screen
2. Tap "Save to Flash" button
3. Brief pause (~410ms) with "Saving..." indicator
4. Unsaved indicator changes from orange to green

---

## 🚨 Safety Checklist

Before entering maintenance mode:
- [ ] All locomotives stopped (user responsibility)
- [ ] Main track power OFF (system verified)
- [ ] No operations in progress
- [ ] **Configuration backed up** (see Firmware Update section)

During maintenance mode:
- [ ] Main track power lockout active
- [ ] Programming track continues normally
- [ ] Runtime config still adjustable
- [ ] Flash save allowed

After exiting maintenance mode:
- [ ] Main track stays OFF (user must re-enable)
- [ ] Normal operations resume
- [ ] Saved config persists across power cycles

---

## 🔄 Firmware Update (CRITICAL)

### ⚠️ Configuration is NOT Preserved During Updates

**Default behavior**: Flashing firmware erases entire flash including config sector

### Before Update (5 minutes)
1. **Backup configuration**:
   ```
   <s>                    # Note mode, power, unsaved flag
   <D ACK LIMIT>          # Note threshold (if implemented)
   <D ACK MIN>            # Note min duration (if implemented)
   <D ACK MAX>            # Note max duration (if implemented)
   ```
2. **Write down values** (paper or file)
3. **Note any custom calibration** (ADC conversion factor)

### Flash Firmware
- **UF2 Method**: Hold BOOTSEL, copy `PicoDCC.uf2` to USB drive
- **Picotool**: `picotool load -x build/pico/src/PicoDCC.elf`
- **OpenOCD**: Standard flash via debugger

### After Update (5 minutes)
1. System boots with factory defaults
2. Enter Layout Maintenance Mode (LCD)
3. Restore configuration:
   ```
   <D ACK LIMIT [value]>
   <D ACK MIN [value]>
   <D ACK MAX [value]>
   ```
4. Save: `<E>` → expect `<e SAVED>`
5. Exit mode (LCD), re-enable main track if needed

**Documentation**: `docs/firmware-update-config-preservation.md`

---

## ⏱️ Expected Timing

| Operation | Duration | Notes |
|-----------|----------|-------|
| Flash erase | ~400ms | Sector erase |
| Flash program | ~10ms | 16 pages × 0.6ms |
| **Total flash write** | **~410ms** | **DCC packets pause** |
| Mode entry | <100ms | UI transition |
| Mode exit | <100ms | UI transition |
| CRC32 validation | <10ms | On boot |

**Critical**: During 410ms flash write, DCC signals stop. Decoders switch to DC mode and go **FULL SPEED** immediately (not coast). Main track power lockout prevents this.

---

## 🔍 Diagnostic Log Monitoring

**Via LCD**: Main screen → "View Logs" button

**Key messages to watch for**:
```
INFO SYSTEM: Configuration loaded from flash
INFO SYSTEM: CRC32 validation passed
INFO SYSTEM: Entering Layout Maintenance Mode
WARNING SYSTEM: Main track power blocked - maintenance mode active
INFO SYSTEM: Saving configuration to flash...
INFO SYSTEM: Configuration saved successfully
WARNING SYSTEM: Configuration CRC32 validation failed
INFO SYSTEM: Using factory defaults
```

---

## ✅ Pass/Fail Criteria

### Must Pass (Critical)
- ✅ Flash writes complete without errors
- ✅ Configuration persists across power cycles
- ✅ CRC32 detects corruption and restores defaults
- ✅ Main track power lockout enforced
- ✅ 410ms blocking occurs during save
- ✅ Unsaved changes tracked correctly

### Should Pass (Important)
- ✅ LCD UI provides clear feedback
- ✅ Modal dialogs prevent accidental actions
- ✅ Programming track unaffected
- ✅ Multiple flash writes succeed (stress test)
- ✅ Firmware update backup/restore works

### If Any Fail
1. Document failure details in test report
2. Check diagnostic logs for error messages
3. Verify hardware connections (LCD, power)
4. Check firmware build (TEST_BUILD=OFF)
5. Report issue with logs and context

---

## 📊 Quick Test Sequence (25 Minutes)

**Fast validation of core features**:

1. **Boot Test** (2 min)
   - Power on, check factory defaults
   - `<s>` → verify normal mode

2. **Runtime Config** (3 min)
   - `<D ACK LIMIT 55>` → verify acknowledgment
   - `<s>` → verify unsaved flag (`<u>`)

3. **Mode Entry** (3 min)
   - LCD: Settings → Maintenance Mode
   - `<s>` → verify `<iM>` flag

4. **Save Test** (3 min)
   - LCD: Tap "Save to Flash"
   - Observe 410ms pause
   - `<s>` → verify no unsaved flag

5. **Persistence Test** (5 min)
   - Exit maintenance mode
   - Power cycle
   - `<s>` → verify config persisted

6. **Safety Test** (2 min)
   - Exit maintenance, main track OFF
   - `<1 MAIN>` in NORMAL → verify allowed
   - Enter maintenance, `<1 MAIN>` → verify blocked (`<X>`)

7. **Lockout Test** (2 min)
   - In maintenance mode
   - `<1 MAIN>` → verify `<X>` error
   - Exit, `<1 MAIN>` → verify allowed

8. **Firmware Update** (5 min) - Optional
   - Backup config: `<s>`, `<D ACK LIMIT>`
   - Flash firmware (re-flash same version OK)
   - Verify factory defaults restore
   - Restore config manually
   - Verify persistence

**Total**: ~25 minutes for core validation (20 min without firmware update test)

---

## 📄 Full Documentation

- **Test Plan**: `docs/hardware-test-plan-nv-storage.md` (13 detailed tests)
- **Firmware Update**: `docs/firmware-update-config-preservation.md` (backup/restore procedures)
- **Calibration Guide**: `docs/calibration-guide.md` (ADC calibration workflow)
- **Implementation Details**: `docs/implementation-complete-config-storage.md`
- **Architecture**: `docs/architecture.md` (system overview)

---

## 🎉 Success Indicators

When all tests pass:
- Configuration storage infrastructure is **production-ready**
- Layout Maintenance Mode safety system is **validated**
- Ready to proceed to **Phase 1: ACK Detection** (CV programming)

---

*Quick Reference v1.0 - October 20, 2025*
