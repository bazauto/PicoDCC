# DCC Service Mode Programming Implementation Plan

## Document Overview
This document outlines the implementation plan for DCC Service Mode (Programming Track) programming in PicoDCC. The goal is to enable CV (Configuration Variable) programming on the programming track, starting with locomotive address changes and expanding to full CV read/write/verify capabilities.

**Reference**: [DCC Wiki - Service Mode Programming](https://dccwiki.com/Service_Mode_Programming)

**Status**: Planning Phase  
**Last Updated**: October 18, 2025  
**Initial Goal**: Program locomotive address (CV1/CV17/CV18)

---

## Background

### What is Service Mode Programming?
Service Mode programming (also called "Programming on the Programming Track" or "Service Track Programming") is a method for reading and writing Configuration Variables (CVs) in DCC decoders. Unlike operations mode (main track programming), service mode:
- Uses lower voltage and current to safely program decoders
- Provides decoder acknowledgment via current pulse detection
- Is the primary method for changing decoder addresses
- Can read CV values (operations mode cannot reliably read)
- Is safer for decoder programming as there's no track power for motors

### Current Implementation Status
According to `docs/dccex-compliance-analysis.md`:
- ✅ Programming track hardware exists (separate from main track)
- ✅ Programming track power control implemented (`<0 PROG>`, `<1 PROG>`)
- ⚠️ CV method stubs exist in `PicoDccLoco` class
- ❌ DCC service mode packet generation not implemented
- ❌ ACK (acknowledgment) detection not implemented
- ❌ DCC-EX CV command parsing not implemented

---

## DCC Service Mode Programming Modes

There are three primary service mode programming methods defined by the NMRA DCC standards:

### 1. Direct Mode (Preferred - NMRA S-9.2.3)
- **Most common and reliable method**
- Directly addresses any CV (1-1024)
- Supports byte and bit manipulation
- Required by NMRA standards for all decoders manufactured after 1999

**Packet Format**:
```
Preamble + 0 + 0111CCAA + 0 + AAAAAAAA + 0 + DDDDDDDD + 0 + EEEEEEEE + 1
Where:
  CC = 11 for verify, 10 for write, 01 for bit manipulation
  AA + AAAAAAAA = 10-bit CV address (CV# - 1)
  DDDDDDDD = Data byte or bit value
  EEEEEEEE = Error detection byte (XOR of instruction bytes)
```

### 2. Register Mode (Legacy)
- **Older method**, primarily for compatibility
- Limited to 8 registers (maps to CVs 1-8)
- Some decoders may only support this mode

**Packet Format**:
```
Preamble + 0 + 0111CRRR + 0 + DDDDDDDD + 0 + EEEEEEEE + 1
Where:
  C = 1 for verify, 0 for write
  RRR = Register number (0-7)
  DDDDDDDD = Data byte
```

### 3. Paged Mode (Obsolete)
- **Not recommended** for new implementations
- Rarely used in modern decoders
- Can be omitted for initial implementation

---

## DCC-EX Protocol Commands for CV Programming

According to DCC-EX specification (from `docs/dccex-compliance-analysis.md`):

### Programming Track Commands (Service Mode)

| Command | Description | Priority |
|---------|-------------|----------|
| `<W cv value>` | Write CV value (byte) on prog track | **HIGH** |
| `<V cv value>` | Verify CV value (byte) on prog track | **HIGH** |
| `<R cv>` | Read CV value (byte) on prog track | **HIGH** |
| `<B cv bit value>` | Write CV bit on prog track | Medium |
| `<R cv bit>` | Read CV bit on prog track | Medium |

### Main Track Commands (Operations Mode - Future)
| Command | Description | Priority |
|---------|-------------|----------|
| `<w cab cv value>` | Write CV on main track for specific loco | Low |
| `<b cab cv bit value>` | Write CV bit on main track | Low |

**Note**: Main track CV programming (POM - Programming on Main) will be implemented in a future phase.

### DCC-EX Response Format
Successful CV operations should return:
```
<r cv value>           // Read result
<r cv bit value>       // Bit read result
```

Failed operations (no ACK) should return:
```
<r -1>                 // Read failed
```

---

## Hardware Requirements

### Current Hardware (Already Implemented)
From `src/pico_dcc.cpp` and `lib/PicoDCCTrack/pico_dcctrack.h`:
- ✅ Programming track signal pin (GPIO 20)
- ✅ Programming track power control (GPIO 21)
- ✅ Programming track current ADC (ADC1)
- ✅ Programming track short circuit LED (GPIO 19)
- ✅ Separate PIO state machine for prog track
- ✅ Increased preamble bits for prog track (20 vs 14 for main)

### ACK Detection Requirements
**ACK (Acknowledgment) Pulse Specifications** (NMRA S-9.2.3, from DCC Wiki Service Mode Programming):
- **Basic acknowledgment**: Decoder provides increased load (positive-delta) of **at least 60mA** for **6ms ± 1ms**
- Decoder may apply power to motor or similar device to create the load
- Command station must detect current increase above baseline
- Detection threshold typically 30-60mA above baseline
- **Must complete detection within 8ms window after packet end**
- Some decoders (e.g., ESU LokSound V5) alternate motor direction on each ACK to prevent vehicle movement

**Implementation Needs**:
1. **Baseline Current Measurement**: Measure idle track current before programming
2. **ACK Detection Window**: Sample current for 8ms after packet transmission
3. **Threshold Comparison**: Detect 30-60mA increase above baseline
4. **Timing Precision**: Microsecond-level timing control

**ADC Sampling Strategy**:
- Current implementation: 2000 samples for overcurrent protection
- For ACK detection: High-frequency sampling during 8ms window
- Suggested: 100-200 samples across 8ms = 12,500-25,000 Hz sample rate
- Pico ADC can handle this with DMA assistance

---

## Software Architecture

### Component Responsibilities

#### 1. PicoDccExPacket (Command Parsing)
**Location**: `lib/PicoDCCEX/pico_dccexpacket.cpp`

**New Methods Needed**:
```cpp
bool isProgramCommand();           // Detect CV programming commands
int getCVNumber();                 // Extract CV number from command
int getCVValue();                  // Extract CV value for write/verify
int getCVBit();                    // Extract bit number for bit operations
enum ProgramMode {                 // Programming mode enumeration
    PROG_MODE_WRITE_BYTE,
    PROG_MODE_VERIFY_BYTE,
    PROG_MODE_READ_BYTE,
    PROG_MODE_WRITE_BIT,
    PROG_MODE_READ_BIT
};
ProgramMode getProgramMode();      // Determine operation type
```

**Command Pattern Detection**:
```cpp
// Examples:
// <W 1 42>    -> Write CV1 = 42
// <V 29 6>    -> Verify CV29 = 6
// <R 17>      -> Read CV17
// <B 29 5 1>  -> Write CV29 bit 5 = 1
```

#### 2. PicoDccLoco (CV Logic - Existing Stubs)
**Location**: `lib/PicoDCCLoco/pico_dccloco.h` & `.cpp`

**Existing Methods to Implement**:
```cpp
bool verifyCV(int8_t cvNumber, int8_t expectedByte);
bool verifyCV(int8_t cvNumber, bool expectedBit);
int8_t readCVByte(int8_t cvNumber);
bool readCVBit(int8_t cvNumber, uint8_t bit);
void writeCVBytes(int8_t cvNumber, int8_t newByte);
void writeCVBit(int8_t cvNumber, bool newBit);
```

**New Helper Methods Needed**:
```cpp
raw_dcc_cmd_t generateDirectModeWrite(uint16_t cv, uint8_t value);
raw_dcc_cmd_t generateDirectModeVerify(uint16_t cv, uint8_t value);
raw_dcc_cmd_t generateDirectModeBitWrite(uint16_t cv, uint8_t bit, bool value);
raw_dcc_cmd_t generateDirectModeBitVerify(uint16_t cv, uint8_t bit, bool value);
uint8_t calculateErrorByte(uint8_t *data, uint8_t length);
```

**Design Decision**: Keep CV methods in `PicoDccLoco` or create new `PicoDccProgrammer` class?
- **Option A**: Keep in `PicoDccLoco` - simpler, CV operations are loco-specific
- **Option B**: New `PicoDccProgrammer` class - cleaner separation, dedicated state management
- **Recommendation**: **Option B** - Service mode programming has unique state/timing requirements

#### 3. PicoDccTrack (Packet Transmission & ACK Detection)
**Location**: `lib/PicoDCCTrack/pico_dcctrack.h` & `.cpp`

**New Methods Needed**:
```cpp
bool sendProgrammingPacket(raw_dcc_cmd_t cmd, bool expect_ack);
bool detectAck(uint32_t timeout_us = 8000);
float measureBaselineCurrent(uint32_t samples = 100);
bool waitForAck();
```

**ACK Detection Logic**:
```cpp
// Pseudo-code for ACK detection
float baseline = measureBaselineCurrent();
sendPacket(programming_packet);
sleep_us(100);  // Small delay after packet
for (int i = 0; i < 80; i++) {  // 8ms window, 100us samples
    float current = readCurrent();
    if (current > baseline + 0.030) {  // 30mA threshold
        return true;  // ACK detected
    }
    sleep_us(100);
}
return false;  // No ACK
```

**Timing Considerations**:
- Programming packets require minimum 20 preamble bits (already configured)
- Reset packets needed between operations: minimum 50 preamble bits
- ACK detection window: 6ms ± 2ms (must monitor for 8ms)
- Consider using hardware timer interrupts for precise timing

#### 4. PicoDccController (Command Orchestration)
**Location**: `lib/PicoDCCController/pico_dcccontroller.h` & `.cpp`

**New Logic in dccexLoop()**:
```cpp
if (packet->isProgramCommand()) {
    // Disable main track (safety)
    // Enable prog track if not already on
    // Execute programming operation
    // Return result via DCC-EX response
}
```

**Safety Considerations**:
- Ensure main track is powered off during programming
- Verify only one locomotive on prog track
- Add timeout for programming operations (10-30 seconds)
- Handle no-ACK scenarios gracefully

#### 5. New Component: PicoDccProgrammer (Recommended)
**Location**: `lib/PicoDccProgrammer/` (new directory)

**Purpose**: Encapsulate all service mode programming logic

**Class Structure**:
```cpp
class PicoDccProgrammer {
private:
    PicoDccTrack *prog_track;
    float baseline_current;
    uint32_t timeout_ms;
    
    bool sendAndVerifyAck(raw_dcc_cmd_t cmd);
    raw_dcc_cmd_t generateDirectModePacket(/* params */);
    uint8_t calculateErrorByte(uint8_t *data, uint8_t len);
    
public:
    PicoDccProgrammer(PicoDccTrack *prog_track);
    
    // Byte operations
    int readCV(uint16_t cv);                      // Returns -1 on failure
    bool writeCV(uint16_t cv, uint8_t value);     // Returns true if ACK
    bool verifyCV(uint16_t cv, uint8_t value);    // Returns true if ACK
    
    // Bit operations
    int readCVBit(uint16_t cv, uint8_t bit);      // Returns -1, 0, or 1
    bool writeCVBit(uint16_t cv, uint8_t bit, bool value);
    
    // Address programming helpers
    bool setShortAddress(uint8_t address);        // Programs CV1
    bool setLongAddress(uint16_t address);        // Programs CV17/CV18
    uint16_t readAddress();                        // Reads current address
};
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1)
**Goal**: Basic packet generation and ACK detection

**Tasks**:
1. ✅ Create `PicoDccProgrammer` class skeleton
2. ✅ Implement Direct Mode packet generation
   - Write byte packets
   - Verify byte packets
   - Error byte calculation
3. ✅ Implement ACK detection in `PicoDccTrack`
   - Baseline current measurement
   - High-speed ADC sampling during ACK window
   - Threshold detection logic
4. ✅ Add unit tests for packet generation
5. ✅ Add mock ACK detection for testing

**Validation**: Can generate valid Direct Mode packets and simulate ACK detection

---

### Phase 2: DCC-EX Command Integration (Week 2)
**Goal**: Parse DCC-EX CV commands and route to programmer

**Tasks**:
1. ✅ Add CV command parsing to `PicoDccExPacket`
   - `<W cv value>` - Write byte
   - `<V cv value>` - Verify byte
   - `<R cv>` - Read byte (via verify-scan method)
2. ✅ Connect parser to `PicoDccProgrammer`
3. ✅ Implement DCC-EX response generation
   - `<r cv value>` for successful read
   - `<r -1>` for failed read/no ACK
4. ✅ Add controller orchestration logic
5. ✅ Add integration tests

**Validation**: Can execute DCC-EX CV commands via serial interface

---

### Phase 3: CV Read Implementation (Week 3)
**Goal**: Implement CV reading via verify-scan method

**Background**: DCC has no direct "read" command. Reading is done by:
1. Loop through all 256 possible values (0-255)
2. Send verify packet for each value
3. When decoder ACKs, that's the current CV value

**Tasks**:
1. ✅ Implement `readCV()` with verify-scan loop
2. ✅ Optimize with binary search (optional speedup)
3. ✅ Add progress indication for long reads
4. ✅ Add timeout handling (max 30 seconds per CV)
5. ✅ Test with real decoder

**Validation**: Can successfully read CV values from decoder

---

### Phase 4: Address Programming (Week 4)
**Goal**: Achieve initial goal - program locomotive addresses

**Tasks**:
1. ✅ Implement `setShortAddress()` (CV1)
   - Write CV1 with new address (1-127)
   - Verify write was successful
2. ✅ Implement `setLongAddress()` (CV17/CV18)
   - Write CV17 (address high byte)
   - Write CV18 (address low byte)
   - Set CV29 bit 5 to enable long addressing
3. ✅ Implement `readAddress()` helper
   - Check CV29 bit 5 for long/short mode
   - Read appropriate CV(s)
4. ✅ Add high-level DCC-EX command
   - `<W 1 42>` to set short address 42
   - Document address range rules
5. ✅ Test with actual locomotives

**Validation**: Can change locomotive from address 3 to custom address and verify on main track

---

### Phase 5: Bit Operations (Week 5)
**Goal**: Complete service mode feature set

**Tasks**:
1. ✅ Implement Direct Mode bit manipulation packets
2. ✅ Implement `writeCVBit()` and `readCVBit()`
3. ✅ Add DCC-EX bit commands
   - `<B cv bit value>` - Write bit
   - `<R cv bit>` - Read bit (verify method)
4. ✅ Add bit operation tests
5. ✅ Document bit numbering (0-7, LSB first)

**Validation**: Can read/write individual CV bits

---

### Phase 6: Advanced Features (Future)
**Goal**: Enhanced programming capabilities

**Tasks**:
1. Register mode support (legacy decoders)
2. CV29 configuration helper functions
3. Decoder reset command
4. RailCom support (if hardware permits)
5. Operations mode programming (main track CV write)

**Validation**: Comprehensive CV programming capability

---

## Testing Strategy

### Unit Tests (Phase 1-2)
**Location**: `test/pico_dcc_programmer_tests.cpp` (new file)

**Test Cases**:
1. Direct Mode packet generation correctness
   - Write byte packet format
   - Verify byte packet format
   - Bit manipulation packet format
   - Error byte calculation
2. CV address encoding (10-bit split across two bytes)
3. ACK detection simulation
4. DCC-EX command parsing for CV commands
5. Response format validation

### Integration Tests (Phase 3-4)
**Location**: `test/pico_dcc_programmer_integration_tests.cpp` (new file)

**Test Cases**:
1. Full read/write/verify cycle
2. Address programming scenarios
3. No-ACK timeout handling
4. Multiple CV operations in sequence
5. Error handling and recovery

### Hardware Tests (Phase 4-5)
**Real Decoder Testing**:
1. Known-good decoder with default address
2. Read current address (should be 3)
3. Write new address (e.g., 42)
4. Verify write succeeded
5. Test locomotive on main track with new address
6. Read other CVs (e.g., CV29, CV7, CV8)

**Safety Tests**:
1. Ensure main track power off during programming
2. Verify current limiting works
3. Test with no decoder (no ACK scenario)
4. Test with multiple decoders (should fail safely)

---

## Safety Considerations

### Programming Track Safety
1. **Power Isolation**: Main track MUST be off during service mode programming
2. **Current Limiting**: Programming track limited to ~250mA max
3. **Single Decoder**: Only one decoder should be on prog track
4. **Timeout Protection**: All operations time out after 30 seconds max
5. **Short Circuit Detection**: Monitor for shorts, disable track immediately

### Decoder Safety
1. **Valid CV Ranges**: Check CV numbers (1-1024 valid)
2. **Valid Data Ranges**: Check data values (0-255)
3. **Critical CV Protection**: Warn before modifying CV29 (configuration)
4. **Reset Packets**: Send reset packets between operations

### User Experience
1. **Clear Error Messages**: Explain why operation failed
2. **Progress Indication**: Show progress for slow operations (CV read)
3. **Undo Capability**: Log previous CV values for rollback
4. **Documentation**: Comprehensive CV programming guide

---

## DCC Packet Specifications

**IMPORTANT**: The error byte (XOR checksum) is **automatically calculated and appended by `PicoDccTrack::sendCommand()`**. Component classes (e.g., `PicoDccProgrammer`) should NOT include the error byte in `raw_dcc_cmd_t.data[]`. The Track class handles this automatically by XORing all data bytes and incrementing the packet length when sending to the PIO state machine.

### Direct Mode Write Byte (Example: CV1 = 42)
```
Preamble (20 bits): 1111111111 1111111111
Packet Start Bit: 0
Instruction Byte 1: 0111CC00 = 01111000 (CV write, CV address high 2 bits = 00)
Data Separator: 0
Instruction Byte 2: 00000000 (CV address low 8 bits, CV1-1=0)
Data Separator: 0
Data Byte: 00101010 (42 decimal)
Data Separator: 0
Error Byte: 01010010 (XOR of instruction bytes) [AUTO-GENERATED BY TRACK]
Packet End Bit: 1
```

### Direct Mode Verify Byte (Example: CV1 = 42)
```
Preamble (20 bits): 1111111111 1111111111
Packet Start Bit: 0
Instruction Byte 1: 0111CC00 = 01111100 (CV verify, CV address high 2 bits = 00)
Data Separator: 0
Instruction Byte 2: 00000000 (CV address low 8 bits, CV1-1=0)
Data Separator: 0
Data Byte: 00101010 (42 decimal)
Data Separator: 0
Error Byte: 01010110 (XOR of instruction bytes) [AUTO-GENERATED BY TRACK]
Packet End Bit: 1
```

### Reset Packet (Between Operations)
```
Preamble (50+ bits): 11111111111111111111...
Packet Start Bit: 0
Address Byte: 00000000 (broadcast address)
Data Separator: 0
Instruction Byte: 00000000 (reset instruction)
Data Separator: 0
Error Byte: 00000000 (XOR) [AUTO-GENERATED BY TRACK]
Packet End Bit: 1
```

---

## Common CV Definitions

### Essential CVs for Address Programming
| CV | Name | Range | Description | Default |
|----|------|-------|-------------|---------|
| 1 | Primary Address | 1-127 | Short (2-digit) address | 3 |
| 17 | Extended Address High | 192-231 | Long address high byte (includes 0xC0) | 192 |
| 18 | Extended Address Low | 0-255 | Long address low byte | 0 |
| 29 | Configuration Data | 0-255 | Bit 5: 0=short addr, 1=long addr | 6 |

### Important Configuration CVs
| CV | Name | Description |
|----|------|-------------|
| 7 | Version | Manufacturer version number (read-only) |
| 8 | Manufacturer ID | NMRA manufacturer ID (read-only) |
| 29 | Configuration | Direction, speed steps, addressing mode |
| 49 | Long Address High | Some decoders use CV 49/50 instead of 17/18 |
| 50 | Long Address Low | Alternative long address storage |

### CV29 Bit Definitions
```
Bit 0: Direction (0=normal, 1=reversed)
Bit 1: Speed steps (0=14, 1=28/128)
Bit 2: DC operation enable
Bit 3: RailCom enable
Bit 4: Speed table enable
Bit 5: Address mode (0=short CV1, 1=long CV17/18)
Bit 6: Reserved
Bit 7: Accessory decoder mode
```

---

## Diagnostic Commands (Future Enhancement)

### Useful Non-Standard Commands
These are helpful for debugging but not part of NMRA standard:

```
<D cv>              // Dump CV value with extra diagnostics
<D RANGE start end> // Dump range of CVs
<D DECODER>         // Read decoder identification CVs (7, 8)
<D ADDRESS>         // Read and display current address
```

---

## Documentation Updates Required

### 1. Update `.github/copilot-instructions.md`
Add section on service mode programming:
```markdown
## Service Mode Programming
- Reference: https://dccwiki.com/Service_Mode_Programming
- DCC-EX commands: <W cv value>, <V cv value>, <R cv>
- ACK detection: 60mA pulse for 6ms, detect within 8ms
- Programming track: Separate from main, lower current, increased preamble
- Implementation: PicoDccProgrammer class handles all CV operations
```

### 2. Update `docs/architecture.md`
Add PicoDccProgrammer component to architecture diagram and descriptions

### 3. Update `docs/dccex-compliance-analysis.md`
Move CV programming from "NOT IMPLEMENTED" to "IMPLEMENTED" section

### 4. Create `docs/cv-programming-guide.md`
User-facing guide for CV programming via DCC-EX commands

---

## Open Questions

Before proceeding with implementation, please clarify:

### Hardware Questions
1. **ADC Sampling Rate**: What's the current ADC sample rate in the existing code? Can we achieve 12-25 kHz for ACK detection?
2. **Current Sensor Accuracy**: What's the precision of the current measurement on the programming track? Can it reliably detect 30mA changes?
3. **Programming Track Testing**: Do you have a known-good decoder for initial testing?

### Software Design Questions
1. **Component Structure**: Do you prefer keeping CV methods in `PicoDccLoco` or creating a dedicated `PicoDccProgrammer` class? (I recommend the latter)
2. **Error Handling**: Should failed CV operations return DCC-EX error format or custom diagnostic messages?
3. **Progress Indication**: Should CV read operations (which take ~2-5 seconds) show progress via serial output?

### Implementation Priority Questions
1. **Register Mode**: Should we implement legacy Register Mode, or focus solely on Direct Mode?
2. **Bit Operations**: Are bit-level CV operations (Phase 5) important for your use case, or can they wait?
3. **Main Track Programming**: Is operations mode programming (POM) needed soon, or can it be deferred to Phase 6?

---

## Risk Assessment

### High Risk Items
1. **ACK Detection Reliability**: Most critical component, hardware-dependent
   - **Mitigation**: Extensive testing with various decoders, adjustable thresholds
2. **Timing Precision**: ACK window is only 8ms
   - **Mitigation**: Use hardware timers, minimize interrupt latency
3. **Current Measurement Accuracy**: Need to detect small current changes
   - **Mitigation**: Calibration routine, multiple sample averaging

### Medium Risk Items
1. **Decoder Compatibility**: Different decoders may behave slightly differently
   - **Mitigation**: Test with multiple decoder brands
2. **Multi-byte CV Operations**: CV17/18 for long addresses must be coordinated
   - **Mitigation**: Atomic operations, verification after each write

### Low Risk Items
1. **Command Parsing**: Straightforward addition to existing parser
2. **Packet Generation**: Well-defined NMRA standard
3. **Error Reporting**: Can use existing DCC-EX response format

---

## Success Criteria

### Phase 4 Completion (Initial Goal)
- ✅ Can read current locomotive address from decoder
- ✅ Can write new short address (1-127) to CV1
- ✅ Can write new long address (128-10239) to CV17/CV18/CV29
- ✅ Locomotive responds to new address on main track
- ✅ Can verify programming success via CV read
- ✅ Proper error handling for no-ACK scenarios
- ✅ DCC-EX serial commands work correctly
- ✅ Documentation updated and complete

### Full Implementation (Phase 6)
- ✅ All NMRA Direct Mode operations supported
- ✅ CV read via verify-scan method
- ✅ Bit-level CV manipulation
- ✅ Comprehensive test coverage
- ✅ User guide documentation
- ✅ Compatible with common DCC control software (JMRI, etc.)

---

## Next Steps

1. **Review this document** - Confirm approach and priorities
2. **Answer open questions** - Clarify hardware capabilities and design preferences
3. **Phase 1 Implementation** - Start with `PicoDccProgrammer` class and packet generation
4. **Hardware Testing** - Validate ACK detection with real decoder
5. **Iterative Development** - Complete phases incrementally with testing

---

## References

- **NMRA Standards**:
  - S-9.2.3: Service Mode Programming
  - S-9.2.1: DCC Packet Format
  - RCN-212: Function Mapping
  
- **DCC-EX Documentation**:
  - [Command Summary Reference](https://dcc-ex.com/reference/software/command-summary-consolidated.html)
  - [CV Programming Guide](https://dcc-ex.com/reference/software/command-reference.html#programming-track-commands)

- **PicoDCC Documentation**:
  - `docs/architecture.md` - System architecture
  - `docs/dccex-compliance-analysis.md` - Current implementation status
  - `docs/safety-recommendations.md` - Safety considerations

- **External Resources**:
  - [DCC Wiki - Service Mode Programming](https://dccwiki.com/Service_Mode_Programming)
  - NMRA DCC Standards documents

---

*Document created: October 18, 2025*  
*Author: AI Coding Agent (with human oversight)*  
*Status: Draft for Review*
