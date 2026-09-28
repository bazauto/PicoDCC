# CV Programming Integration - `<R>` Command

## Overview
Implemented the `<R>` DCC-EX command for reading decoder addresses on the programming track. This provides immediate hardware testing capability for the CV programming infrastructure.

## Implementation Summary

### Command: `<R>` - Read Decoder Address
**Purpose**: Automatically detects and reads decoder address (short or long)

**Response Format**:
- `<r address>` - Success (returns short address 1-127 or long address 128-10239)
- `<r -1>` - Failure (no valid address found or track not powered)

**Behavior**:
1. Verifies programming track is powered
2. Attempts to read short address (CV1)
3. If short address invalid, attempts to read long address (CV17/18)
4. Returns first valid address found
5. Logs all operations to diagnostic system

### Files Modified

#### 1. **lib/PicoDCCEX/pico_dccexpacket.h**
- Added `isReadAddressCommand()` method
- Recognizes 'R' opcode as valid command

#### 2. **lib/PicoDCCEX/pico_dccexpacket.cpp**
- Added validation for 'R' command (no parameters required)

#### 3. **lib/PicoDCCController/pico_dcccontroller.h**
- Included `PicoDCCProgrammer` header
- Added `PicoDccProgrammer programmer` member variable
- Added `handleReadAddressCommand()` method declaration

#### 4. **lib/PicoDCCController/pico_dcccontroller.cpp**
- Initialized `programmer` in constructor with programming track and config storage
- Added command routing in `dccexLoop()` to handle `<R>` commands
- Implemented `handleReadAddressCommand()`:
  - Checks programming track power
  - Attempts short address read first (most common)
  - Falls back to long address read if needed
  - Returns DCC-EX protocol response
  - Comprehensive diagnostic logging

#### 5. **lib/PicoDCCController/CMakeLists.txt**
- Added `PicoDCCProgrammer` to link libraries (both test and hardware modes)

#### 6. **lib/PicoDCCProgrammer/CMakeLists.txt**
- Changed hardware mode from INTERFACE to STATIC library
- Added parent directory to include paths for `pico_diagnostic.h`
- Fixed PUBLIC linkage for `pico_stdlib` and `hardware_adc`

#### 7. **lib/pico_diagnostic.h**
- Added `COMPONENT_PROGRAMMER` identifier for consistent logging

## Test Status
- ✅ **26/26 programmer tests passing** (100%)
- ✅ **Hardware build successful** (.uf2 generated)
- ✅ **Dual-mode build working** (TEST_BUILD on/off)

## Usage Example

```
// Power on programming track
<1 PROG>

// Place decoder on programming track

// Read decoder address
<R>

// Expected responses:
<r 3>      // Short address 3 found
<r 1234>   // Long address 1234 found
<r -1>     // No valid address found
```

## Diagnostic Logging
The command generates comprehensive logs visible via LCD:

**Success Path**:
```
[12345] INFO PROGRAMMER: Starting decoder address read...
[12678] INFO PROGRAMMER: Short address read successfully
```

**Long Address Path**:
```
[12345] INFO PROGRAMMER: Starting decoder address read...
[13012] INFO PROGRAMMER: Long address read successfully
```

**Failure Path**:
```
[12345] INFO PROGRAMMER: Starting decoder address read...
[13456] ERROR PROGRAMMER: Failed to read decoder address
```

**Track Not Powered**:
```
[12345] WARNING PROGRAMMER: Read address failed: programming track not powered
```

## Hardware Testing Checklist

### Prerequisites
- ✅ Programming track isolated from main track
- ⚠️ Current sense circuit connected and working (ACK detection critical)
- ⚠️ Decoder placed on programming track
- ⚠️ USB connected for serial monitoring
- ⚠️ Power supply adequate for programming track

### Test Steps
1. Flash `.uf2` file to Pico
2. Connect serial monitor (115200 baud)
3. Send `<1 PROG>` to power programming track
4. Send `<R>` to read address
5. Verify response: `<r address>` or `<r -1>`
6. Check diagnostic logs on LCD for detailed status

### Expected Outcomes
- **Short Address Decoder**: Should return `<r 1>` to `<r 127>`
- **Long Address Decoder**: Should return `<r 128>` to `<r 10239>`
- **No Decoder**: Should return `<r -1>` with timeout
- **Track Not Powered**: Should return `<r -1>` immediately

### Troubleshooting
If reading fails:
1. Check baseline current (should be 10-100mA when powered)
2. Verify ACK pulse timing (decoder should pulse ~6ms)
3. Adjust ACK threshold if needed: `<D ACK LIMIT 60>` (default)
4. Check ACK timing windows: `<D ACK MIN 4500>` `<D ACK MAX 8000>` (µs)
5. Verify packet transmission (8 repetitions per NMRA standard)

## Architecture Integration

### Component Relationships
```
PicoDccController (Core 0)
    ├── PicoDccProgrammer
    │   ├── PicoDccTrack (prog_track)
    │   └── PicoConfigStorage (ACK parameters)
    ├── PicoDccEx (DCC-EX protocol parser)
    └── PicoDccLocos (locomotive collection)
```

### Data Flow
```
UART → PicoDccEx → PicoDccController → PicoDccProgrammer
                                              ↓
                                         PicoDccTrack (programming)
                                              ↓
                                         Decoder (ACK pulse)
                                              ↓
                                         ADC (current sense)
                                              ↓
                                         PicoDccProgrammer (ACK detection)
                                              ↓
                                         Response → UART
```

## Future Enhancements

### Phase 2 - Full CV Read/Write
- `<R cv>` - Read any CV (1-1024)
- `<W cv value>` - Write CV byte
- `<V cv value>` - Verify CV byte
- `<B cv bit value>` - Read/write CV bit

### Phase 3 - Advanced Features
- Bit manipulation for faster reads (8 operations vs 256)
- Service mode reset packet
- CV29 configuration analysis
- POM (Programming on Main) support

## Notes
- Current implementation uses brute-force CV read (0-255 verify operations)
- ACK detection requires properly tuned current sense circuit
- Programming track operations are blocking (no concurrent main track operations)
- All CV operations use NMRA S-9.2.3 Direct Mode format
- Decoder must comply with NMRA standards (60mA pulse, 6ms duration)

## References
- NMRA S-9.2.3: Direct Mode CV Access Specification
- DCC Wiki: [Service Mode Programming](https://dccwiki.com/Service_Mode_Programming)
- Project Docs: `docs/service-mode-programming-plan.md`
