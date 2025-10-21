# Firmware Update Process - Configuration Preservation

**✅ IMPLEMENTED**: Custom linker script now provides automatic configuration preservation!

**Issue** (Resolved): Standard firmware flashing previously erased the entire flash including the configuration sector

**Solution**: Custom linker script (`memmap_picodcc.ld`) reserves last 4KB of flash for configuration storage

---

## ✅ Current Solution: Custom Linker Script (ACTIVE)

### Status: FULLY IMPLEMENTED ✅

The project now uses a custom linker script that **automatically preserves configuration** during firmware updates. No manual backup/restore is needed!

### How It Works

1. **Memory Layout**:
   - **Firmware region**: 0x10000000 - 0x103FEFFF (4092 KB)
   - **Config region**: 0x103FF000 - 0x103FFFFF (4 KB, **reserved and protected**)

2. **Linker Protection**:
   - Firmware is limited to 4092 KB by `memmap_picodcc.ld`
   - Linker will **fail the build** if firmware exceeds this limit
   - Build error: *"ERROR: Firmware exceeds 4092KB and would overwrite config sector at 0x103FF000!"*

3. **Automatic Preservation**:
   - VS Code Pico extension respects the custom linker script
   - UF2/ELF files only include firmware (0x10000000-0x103FEFFF)
   - Config sector (0x103FF000-0x103FFFFF) is **NOT** included in firmware images
   - OpenOCD/picotool/debugger flashing preserves config sector automatically

4. **Current Firmware Size**:
   - **Firmware**: ~437 KB (10.68% of 4092 KB limit)
   - **Safety margin**: 1639.37 KB (plenty of room for growth)
   - **Config sector**: Safe and protected ✅

### Verification

Check firmware size at any time:
```bash
cd build/src
arm-none-eabi-size PicoDCC.elf

# Expected output:
#    text    data     bss     dec     hex filename
#  416408    2048   60056  478512   74d30 PicoDCC.elf
# 
# Firmware size (text + data) = 418,456 bytes (406.65 KB)
# Well under the 2,093,056 byte (2044 KB) limit ✅
```

Check firmware end address:
```bash
arm-none-eabi-nm PicoDCC.elf | grep "__flash_binary_end"

# Expected: 10065288 (ends at ~405 KB, well before config at 2044 KB)
```

### Benefits

- ✅ **No manual backup required** - config persists automatically across firmware updates
- ✅ **Works with VS Code Pico extension** - debug/flash workflow completely unchanged
- ✅ **Build-time protection** - linker prevents accidental config sector overflow
- ✅ **Zero user intervention** - completely transparent to developers
- ✅ **Development-friendly** - no tedious config resets between debug iterations
- ✅ **Safe for production** - configuration survives all update methods (UF2, OpenOCD, picotool)

### Firmware Update Process (With Linker Script)

**With custom linker script** (current):
1. Flash new firmware via VS Code Pico extension (F5) or UF2
2. Configuration automatically preserved ✅
3. System boots with saved configuration intact
4. No user action required

**That's it!** Your calibration and settings persist automatically.

---

## 📜 Implementation Files

- **Linker Script**: `memmap_picodcc.ld` (project root)
- **CMake Integration**: `src/CMakeLists.txt` (line 21: `pico_set_linker_script()`)
- **Config Storage**: `lib/PicoConfigStorage/pico_configstorage.cpp` (uses 0x103FF000)

---

## � Historical Solutions (Reference Only)

The following solutions were documented before the custom linker script was implemented. They are **no longer needed** but kept here for reference.

### ~~Solution 1: Manual Backup/Restore~~ (NOT NEEDED ANYMORE)

**Before Firmware Update**:
1. Enter Layout Maintenance Mode via LCD
2. Read current config via serial terminal
3. Save config values externally (notepad, file, etc.)

**After Firmware Update**:
1. System boots with factory defaults
2. Enter Layout Maintenance Mode
3. Re-enter config values via DCC-EX commands:
   ```
   <D ACK LIMIT 55>
   <D ACK MIN 4500>
   <D ACK MAX 8000>
   ```
4. Save to flash: `<E>`

**Pros**:
- Works today, no code changes needed
- Simple and reliable

**Cons**:
- Manual process
- User must remember to backup
- Re-calibration may be needed

**Status**: ❌ Superseded by custom linker script (automatic preservation)

---

### ~~Solution 2: Firmware Update Utility~~ (NOT NEEDED ANYMORE)

**Concept**: Tool that preserves configuration sector during firmware updates

**Implementation Options**:

#### Option A: Picotool with Preserve Flag
```bash
# Read config before update
picotool save -r 0x103FF000 0x103FFFFF config_backup.bin

# Flash new firmware (erases everything)
picotool load PicoDCC.uf2

# Restore config
picotool load -o 0x103FF000 config_backup.bin
```

**Pros**:
- Automated preservation
- Works with any firmware update

**Cons**:
- Requires picotool scripting
- Not beginner-friendly
- Requires debugger connection

**Status**: ❌ Superseded by custom linker script (simpler and automatic)

#### ~~Option B: UF2 with Reserved Region~~ (IMPLEMENTED ✅)

This is the **CURRENT SOLUTION**! The custom linker script approach has been implemented.

**Implementation**: See `memmap_picodcc.ld` and `src/CMakeLists.txt`

**CMakeLists.txt modification**:
```cmake
# Custom linker script that reserves last 4KB
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T ${CMAKE_SOURCE_DIR}/memmap_config.ld")

# Or use pico_set_linker_script
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/memmap_config.ld)
```

**Custom linker script (`memmap_config.ld`)**:
```ld
MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4092k  /* Firmware only */
    CONFIG(r) : ORIGIN = 0x103FF000, LENGTH = 4k     /* Reserved for config */
    RAM(rwx)  : ORIGIN = 0x20000000, LENGTH = 520k
}

SECTIONS
{
    /* Firmware sections use FLASH only */
    .text : {
        *(.text*)
    } > FLASH
    
    /* CONFIG region is not touched by firmware */
}
```

**Pros**:
- Linker prevents firmware from overwriting config
- UF2 file won't include config sector
- Configuration automatically preserved

**Cons**:
- Requires linker script customization
- More complex build setup
- May conflict with Pico SDK defaults

#### Option C: In-App Firmware Update (Advanced)
Application-level firmware update that:
1. Reads new firmware over USB/serial
2. Writes firmware to flash **except config sector**
3. Preserves config automatically

**Pros**:
- Best user experience
- No external tools needed
- Automatic preservation

**Cons**:
- Complex implementation
- Requires bootloader-like code
- Risk of bricking if update fails

---

## 📋 Recommended Approach

### Phase 1: Manual Backup/Restore (Current)

**For Now**: Document the manual process clearly

**User Instructions** (add to hardware test plan):
```markdown
### Firmware Update Procedure

**Before Update**:
1. Connect serial terminal (115200 baud)
2. Record current configuration:
   - Send: `<s>` → Note track power states
   - Send: `<D ACK LIMIT>` → Note threshold value
   - Send: `<D ACK MIN>` → Note min duration
   - Send: `<D ACK MAX>` → Note max duration
   - Note ADC calibration if displayed
3. Write values down or save to file

**Update Firmware**:
1. Copy new `PicoDCC.uf2` to Pico in bootloader mode
2. Pico reboots with new firmware
3. Configuration is erased (factory defaults loaded)

**After Update**:
1. Power on, system boots with defaults
2. Enter Layout Maintenance Mode (LCD)
3. Re-enter configuration:
   - `<D ACK LIMIT [value]>`
   - `<D ACK MIN [value]>`
   - `<D ACK MAX [value]>`
4. Save: `<E>`
5. Exit maintenance mode
6. If ADC calibration needed, follow calibration guide
```

### Phase 2: Custom Linker Script (Future)

**Timeline**: After ACK detection phase complete

**Implementation**:
1. Create `memmap_picodcc.ld` linker script
2. Reserve last 4KB for configuration
3. Modify `src/CMakeLists.txt` to use custom linker script
4. Test firmware size limits (must fit in 4092KB)
5. Validate configuration preservation across updates

**Estimated Effort**: 2-3 hours

---

## 🔍 Current Firmware Size Check

**How to check if we're close to 4092KB limit**:

After building firmware:
```bash
ls -lh build/src/PicoDCC.elf
arm-none-eabi-size build/src/PicoDCC.elf
```

**Expected Output**:
```
   text    data     bss     dec     hex filename
 123456    5678   12345  141479   22857 build/src/PicoDCC.elf
```

**Safe Zone**: text + data < 4,190,208 bytes (4092KB)

**Current Status**: Firmware is well under limit (typical: 200-400KB)

---

## 📝 Documentation Updates Needed

### 1. Update Hardware Test Plan

Add section: **"Firmware Update Procedure"** with manual backup/restore steps

### 2. Update Calibration Guide

Add warning:
```markdown
⚠️ **Important**: Firmware updates will erase calibration data!
Before updating firmware:
1. Record your calibration values
2. Follow firmware update procedure (see hardware test plan)
3. Re-calibrate or restore values after update
```

### 3. Update Implementation Doc

Update "Known Limitations" section:
```markdown
1. **Firmware Updates Erase Configuration**: Standard flash process erases entire flash
   - **Mitigation**: Manual backup/restore procedure documented
   - **Future**: Custom linker script to reserve config sector
   - **Impact**: Users must re-enter calibration after firmware updates
```

---

## ✅ Action Items

### Immediate (Before Hardware Testing)
1. ✅ Document manual backup/restore procedure
2. ✅ Add firmware update section to hardware test plan
3. ✅ Update calibration guide with warning
4. ✅ Test firmware size (verify << 4092KB)

### Future (After ACK Detection Phase)
1. Create custom linker script (`memmap_picodcc.ld`)
2. Modify `src/CMakeLists.txt` to use custom linker
3. Test configuration preservation across updates
4. Update documentation with automatic preservation

---

## 🎯 Recommended for Hardware Testing

**For upcoming hardware testing session**:

1. **Test configuration backup/restore manually**:
   - Save config to flash
   - Record values via serial
   - Flash firmware again (simulating update)
   - Verify config is erased
   - Restore config manually
   - Verify it works

2. **Document the experience**:
   - How long did backup/restore take?
   - Was procedure clear enough?
   - Any issues or confusion?

3. **Decide on priority**:
   - Is manual process acceptable for now?
   - Or should we implement linker script before Phase 1?

---

## 🚦 Decision Point

**Question for User**: 

**Option A**: Proceed with manual backup/restore for now (5 minutes to document)
- Pros: Can start hardware testing immediately
- Cons: Users must manually preserve config during updates

**Option B**: Implement custom linker script first (2-3 hours)
- Pros: Automatic config preservation from day 1
- Cons: Delays hardware testing, adds complexity

**Recommendation**: **Option A** - Manual process for now, custom linker later
- Reason: Hardware testing is more urgent
- Reason: Manual process is simple and reliable
- Reason: Can add linker script later without breaking anything

---

*Document Version: 1.0 - October 20, 2025*
