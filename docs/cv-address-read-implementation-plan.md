# CV Address Read Implementation Plan

**Date**: October 21, 2025  
**Status**: 🚀 READY TO IMPLEMENT  
**Purpose**: Read locomotive address from decoder (non-destructive programming track operation)

---

## 🎯 Implementation Strategy

**Why Address Read First?**
- ✅ **Non-destructive** - doesn't modify decoder state
- ✅ **Validates all infrastructure** - ACK detection, programming track, Direct Mode packets
- ✅ **Tests hardware** - ADC current monitoring, baseline measurement, pulse detection
- ✅ **Simple DCC-EX command** - `<R 1>` (read CV1 - short address)
- ✅ **Clear success criteria** - known decoder address can be verified

**Deferred Until Later**:
- ❌ Address writing (CV1, CV17/18, CV29) - destructive, needs verified read first
- ❌ General CV read/write - address is most common use case
- ❌ Service mode verify/bit operations - read is sufficient for address discovery

---

## 📋 Implementation Phases

### Phase 1: ACK Detection Infrastructure (Week 1)

**Goal**: Detect ACK pulses (60mA for 6ms within 8ms window)

#### 1.1 Create PicoDccProgrammer Component
**Files**: 
- `lib/PicoDccProgrammer/pico_dcc_programmer.h`
- `lib/PicoDccProgrammer/pico_dcc_programmer.cpp`
- `lib/PicoDccProgrammer/CMakeLists.txt`

**Class Design**:
```cpp
class PicoDccProgrammer {
private:
    PicoDccTrack *prog_track;
    PicoConfigStorage *config_storage;
    
    // ACK detection configuration (from NV storage)
    uint16_t ack_limit_ma = 60;        // ACK threshold (50-100mA typical)
    uint16_t ack_min_duration_us = 4500; // Min ACK pulse (4.5ms typical)
    uint16_t ack_max_duration_us = 8000; // Max ACK window (8ms spec limit)
    
    // Current monitoring
    float baseline_current_ma = 0.0;
    
    // Internal helpers
    bool measureBaselineCurrent();
    bool detectACK(uint32_t timeout_ms);
    
public:
    PicoDccProgrammer(PicoDccTrack *prog_track, PicoConfigStorage *config);
    
    // CV read operations
    int16_t readCV(uint16_t cv_number);  // Returns CV value or -1 on error
    
    // Configuration
    void loadConfig();  // Load ACK parameters from NV storage
    void setACKThreshold(uint16_t limit_ma);
    void setACKMinDuration(uint16_t duration_us);
    void setACKMaxDuration(uint16_t duration_us);
    
    // Diagnostics
    float getBaselineCurrent() const { return baseline_current_ma; }
};
```

#### 1.2 Baseline Current Measurement
**Purpose**: Measure decoder idle current before ACK detection

**Algorithm**:
```cpp
bool PicoDccProgrammer::measureBaselineCurrent() {
    // 1. Ensure programming track powered
    if (!prog_track->isPowered()) {
        LOG_ERROR("PROG", "Cannot measure baseline - track not powered");
        return false;
    }
    
    // 2. Send reset packets (20-bit preamble + reset instruction)
    // Allow decoder to settle (100ms minimum)
    sleep_ms(100);
    
    // 3. Sample current over 50ms (typical idle current: 20-40mA)
    const int num_samples = 50;
    uint32_t total_current = 0;
    
    for (int i = 0; i < num_samples; i++) {
        total_current += prog_track->getCurrentMilliamps();
        sleep_ms(1);
    }
    
    // 4. Calculate average
    baseline_current_ma = (float)total_current / num_samples;
    
    // 5. Validate range (10-100mA typical)
    if (baseline_current_ma < 10.0 || baseline_current_ma > 100.0) {
        LOG_WARNING("PROG", "Baseline current unusual: %.1f mA", baseline_current_ma);
    }
    
    LOG_INFO("PROG", "Baseline current: %.1f mA", baseline_current_ma);
    return true;
}
```

#### 1.3 ACK Pulse Detection
**Purpose**: Detect 60mA pulse for 6ms within 8ms window

**Algorithm**:
```cpp
bool PicoDccProgrammer::detectACK(uint32_t timeout_ms) {
    uint32_t start_time = millis();
    uint32_t ack_start = 0;
    bool ack_in_progress = false;
    float threshold = baseline_current_ma + ack_limit_ma;
    
    while (millis() - start_time < timeout_ms) {
        float current_ma = prog_track->getCurrentMilliamps();
        uint32_t now = micros();
        
        // Detect ACK pulse start (current exceeds threshold)
        if (!ack_in_progress && current_ma >= threshold) {
            ack_in_progress = true;
            ack_start = now;
            LOG_DEBUG("PROG", "ACK pulse start: %.1f mA", current_ma);
        }
        
        // Detect ACK pulse end (current drops below threshold)
        if (ack_in_progress && current_ma < threshold) {
            uint32_t ack_duration = now - ack_start;
            
            // Validate ACK duration (4.5ms - 8ms typical)
            if (ack_duration >= ack_min_duration_us && ack_duration <= ack_max_duration_us) {
                LOG_INFO("PROG", "ACK detected: %u µs, %.1f mA", ack_duration, current_ma);
                return true;  // Valid ACK
            } else {
                LOG_WARNING("PROG", "ACK rejected: %u µs (expected %u-%u µs)", 
                           ack_duration, ack_min_duration_us, ack_max_duration_us);
                ack_in_progress = false;  // Reset for next pulse
            }
        }
        
        // Timeout ACK detection window
        if (ack_in_progress && (now - ack_start) > ack_max_duration_us) {
            LOG_WARNING("PROG", "ACK timeout: pulse too long");
            ack_in_progress = false;
        }
        
        sleep_us(100);  // High-speed sampling (10kHz)
    }
    
    LOG_DEBUG("PROG", "No ACK detected within %u ms", timeout_ms);
    return false;  // No ACK detected
}
```

**Key Requirements**:
- **High-speed ADC sampling**: 10kHz minimum (100µs per sample)
- **Timing precision**: Microsecond-level pulse duration measurement
- **Current threshold**: Baseline + configurable limit (default 60mA)
- **Duration window**: 4.5ms - 8ms (NMRA S-9.2.3 spec)

---

### Phase 2: Direct Mode Packet Generation (Week 2)

**Goal**: Generate NMRA Direct Mode CV read packets

#### 2.1 CV Read Packet Format (NMRA S-9.2.3)

**Byte 1**: Long preamble (20 bits, not 14)  
**Byte 2**: Address byte (0b01110110 for broadcast/direct mode)  
**Byte 3**: Instruction byte (0b1110CCAA)
  - `1110` = Direct Mode CV access
  - `CC` = CV address bits 9-8 (for CVs 513-1024)
  - `AA` = Instruction type (01 = Verify, 10 = Write, 11 = Read)
**Byte 4**: CV address low byte (bits 7-0)  
**Byte 5**: Data byte (for write/verify) OR 0x00 (for read)  
**Byte 6**: Error detection byte (XOR of bytes 2-5)

**Example - Read CV1** (short address):
```
Preamble: 20 bits of '1'
Byte 1: 0  (start bit)
Byte 2: 01110110 (0x76 - direct mode address)
Byte 3: 11100011 (0xE3 - read CV, bits 9-8 = 00, instruction = 11)
Byte 4: 00000000 (0x00 - CV1 address low byte)
Byte 5: 00000000 (0x00 - not used for read)
Byte 6: 11010101 (0xD5 - error byte: 0x76 XOR 0xE3 XOR 0x00 XOR 0x00)
```

#### 2.2 Implementation

**Helper Function**:
```cpp
raw_dcc_cmd_t PicoDccProgrammer::generateCVReadPacket(uint16_t cv_number) {
    raw_dcc_cmd_t packet;
    packet.is_prog = true;  // Programming track uses 20-bit preamble
    
    // NMRA CV numbering: CV1 = address 0, CV2 = address 1, etc.
    uint16_t cv_addr = cv_number - 1;
    
    // Byte 1: Direct mode address (broadcast)
    packet.data[0] = 0x76;
    
    // Byte 2: Instruction byte (1110CCAA)
    // CC = CV bits 9-8, AA = 11 (read operation)
    uint8_t cv_high_bits = (cv_addr >> 8) & 0x03;
    packet.data[1] = 0xE0 | (cv_high_bits << 2) | 0x03;
    
    // Byte 3: CV address low byte
    packet.data[2] = cv_addr & 0xFF;
    
    // Byte 4: Data byte (not used for read, set to 0x00)
    packet.data[3] = 0x00;
    
    // Byte 5: Error detection byte (XOR of all previous bytes)
    packet.data[4] = packet.data[0] ^ packet.data[1] ^ packet.data[2] ^ packet.data[3];
    
    packet.size = 5;
    packet.repeat = 0;  // Single transmission (ACK detection follows)
    
    return packet;
}
```

---

### Phase 3: CV Read Implementation (Week 3)

**Goal**: Complete end-to-end CV read operation

#### 3.1 Main Read Function

```cpp
int16_t PicoDccProgrammer::readCV(uint16_t cv_number) {
    // 1. Validate CV number (1-1024)
    if (cv_number < 1 || cv_number > 1024) {
        LOG_ERROR("PROG", "Invalid CV number: %u (valid: 1-1024)", cv_number);
        return -1;
    }
    
    // 2. Ensure programming track powered
    if (!prog_track->isPowered()) {
        LOG_ERROR("PROG", "Programming track not powered");
        return -1;
    }
    
    // 3. Measure baseline current
    if (!measureBaselineCurrent()) {
        LOG_ERROR("PROG", "Failed to measure baseline current");
        return -1;
    }
    
    // 4. Generate CV read packet
    raw_dcc_cmd_t read_packet = generateCVReadPacket(cv_number);
    
    // 5. Try each byte value (0-255) until ACK received
    for (uint16_t byte_value = 0; byte_value <= 255; byte_value++) {
        // Generate verify packet with current byte value
        raw_dcc_cmd_t verify_packet = generateCVVerifyPacket(cv_number, byte_value);
        
        // Send verify packet (6-8 times per NMRA spec)
        for (int retry = 0; retry < 8; retry++) {
            prog_track->sendPacket(&verify_packet);
            sleep_ms(10);  // Allow decoder to process
        }
        
        // Check for ACK pulse
        if (detectACK(20)) {  // 20ms timeout per NMRA spec
            LOG_INFO("PROG", "CV%u = %u (ACK detected)", cv_number, byte_value);
            return (int16_t)byte_value;
        }
    }
    
    LOG_ERROR("PROG", "CV%u read failed: No ACK for any value", cv_number);
    return -1;  // No ACK detected for any byte value
}
```

**Note**: This is the "brute force" method (256 iterations). Optimized methods (bit manipulation) can be added later.

#### 3.2 CV Verify Packet Generation

```cpp
raw_dcc_cmd_t PicoDccProgrammer::generateCVVerifyPacket(uint16_t cv_number, uint8_t byte_value) {
    raw_dcc_cmd_t packet;
    packet.is_prog = true;
    
    uint16_t cv_addr = cv_number - 1;
    
    // Byte 1: Direct mode address
    packet.data[0] = 0x76;
    
    // Byte 2: Instruction byte (1110CCAA)
    // CC = CV bits 9-8, AA = 01 (verify operation)
    uint8_t cv_high_bits = (cv_addr >> 8) & 0x03;
    packet.data[1] = 0xE0 | (cv_high_bits << 2) | 0x01;
    
    // Byte 3: CV address low byte
    packet.data[2] = cv_addr & 0xFF;
    
    // Byte 4: Data byte (value to verify)
    packet.data[3] = byte_value;
    
    // Byte 5: Error detection byte
    packet.data[4] = packet.data[0] ^ packet.data[1] ^ packet.data[2] ^ packet.data[3];
    
    packet.size = 5;
    packet.repeat = 0;
    
    return packet;
}
```

---

### Phase 4: DCC-EX Integration (Week 4)

**Goal**: Add `<R cv>` command support to DCC-EX parser

#### 4.1 Command Format

**DCC-EX Command**: `<R cv>`  
**Example**: `<R 1>` (read CV1 - short address)  
**Response**: `<r cv value>` or `<r cv -1>` (error)

#### 4.2 PicoDccEx Parser Extension

**Add to `pico_dccex.cpp`**:
```cpp
bool PicoDccEx::processCommand(pico_dccex_packet* packet) {
    // ... existing command parsing ...
    
    // CV Read: <R cv>
    if (buffer[0] == 'R' && buffer[1] == ' ') {
        int cv_number = 0;
        if (sscanf(buffer + 2, "%d", &cv_number) == 1) {
            packet->type = DCCEX_CV_READ;
            packet->data.cv_read.cv_number = cv_number;
            return true;
        }
    }
    
    return false;
}
```

#### 4.3 PicoDccController Integration

**Add to `pico_dcccontroller.cpp`**:
```cpp
void PicoDccController::dccexLoop() {
    pico_dccex_packet packet;
    
    if (dccex->processCommand(&packet)) {
        switch (packet.type) {
            // ... existing cases ...
            
            case DCCEX_CV_READ: {
                int16_t cv_value = programmer->readCV(packet.data.cv_read.cv_number);
                
                if (cv_value >= 0) {
                    // Success - send value
                    printf("<r %d %d>\n", packet.data.cv_read.cv_number, cv_value);
                } else {
                    // Error - send -1
                    printf("<r %d -1>\n", packet.data.cv_read.cv_number);
                }
                break;
            }
        }
    }
}
```

---

## 🧪 Testing Strategy

### Unit Tests

**Test Suite**: `test/pico_dcc_programmer_tests.cpp`

1. **Baseline Current Measurement**:
   - Test valid current range (10-100mA)
   - Test out-of-range warnings
   - Test track power check

2. **ACK Detection**:
   - Test valid ACK pulse (60mA for 6ms)
   - Test pulse too short (reject)
   - Test pulse too long (reject)
   - Test current below threshold (no ACK)
   - Test timeout (no pulse detected)

3. **Packet Generation**:
   - Test CV1 read packet (address 0)
   - Test CV17 read packet (long address high byte)
   - Test CV29 read packet (configuration byte)
   - Test CV verify packet generation
   - Test error byte calculation

4. **CV Read Logic**:
   - Test successful read (ACK on byte 42)
   - Test failed read (no ACK for any byte)
   - Test invalid CV number (< 1 or > 1024)
   - Test unpowered track error

### Hardware Testing

**Prerequisites**:
- Programming track powered
- Known decoder with known address (e.g., CV1 = 3)
- Serial terminal connected (115200 baud)

**Test Procedure**:
1. Power on programming track: `<1 PROG>`
2. Read CV1: `<R 1>`
3. Expected response: `<r 1 3>` (if decoder address is 3)
4. Verify diagnostic logs show:
   - Baseline current measurement
   - ACK pulse detection
   - CV value confirmation

**Success Criteria**:
- ✅ Correct CV value returned
- ✅ ACK detection timing accurate (6ms ±10%)
- ✅ Baseline current stable (±5mA)
- ✅ No false ACK detections

---

## 📁 File Structure

```
lib/PicoDccProgrammer/
├── CMakeLists.txt
├── pico_dcc_programmer.h
└── pico_dcc_programmer.cpp

test/
└── pico_dcc_programmer_tests.cpp

docs/
└── cv-address-read-implementation-plan.md (this file)
```

---

## 🔧 Configuration Storage Integration

**ACK Detection Parameters** (from NV storage):
- `ack_limit_ma`: ACK threshold (50-100mA typical, default 60mA)
- `ack_min_duration_us`: Min ACK pulse (4000-5000µs typical, default 4500µs)
- `ack_max_duration_us`: Max ACK window (7000-9000µs typical, default 8000µs)

**Loading from NV Storage**:
```cpp
void PicoDccProgrammer::loadConfig() {
    if (config_storage) {
        ack_limit_ma = config_storage->getACKLimit();
        ack_min_duration_us = config_storage->getACKMinDuration();
        ack_max_duration_us = config_storage->getACKMaxDuration();
        
        LOG_INFO("PROG", "ACK config loaded: limit=%umA, min=%uµs, max=%uµs",
                ack_limit_ma, ack_min_duration_us, ack_max_duration_us);
    }
}
```

**DCC-EX Runtime Adjustment** (already implemented):
- `<D ACK LIMIT 60>` - Set ACK threshold to 60mA
- `<D ACK MIN 4500>` - Set ACK min duration to 4500µs
- `<D ACK MAX 8000>` - Set ACK max duration to 8000µs

---

## 📊 Success Metrics

**Phase 1 Complete**:
- ✅ Baseline current measured accurately
- ✅ ACK pulses detected (60mA for 6ms)
- ✅ No false positives/negatives
- ✅ Unit tests passing (10+ tests)

**Phase 2 Complete**:
- ✅ CV read packets generated correctly
- ✅ CV verify packets generated correctly
- ✅ Error bytes calculated correctly
- ✅ Unit tests passing (5+ tests)

**Phase 3 Complete**:
- ✅ CV read returns correct value
- ✅ Handles read failures gracefully
- ✅ Validates inputs (CV number range)
- ✅ Unit tests passing (5+ tests)

**Phase 4 Complete**:
- ✅ `<R cv>` command works via serial
- ✅ Response format correct (`<r cv value>`)
- ✅ Integration with PicoDccController
- ✅ Hardware test successful (known decoder)

---

## 🚀 Next Steps After Address Read

1. **Optimize Read Algorithm** (optional):
   - Implement bit manipulation method (8 reads instead of 256)
   - Faster CV reading (important for CV17/18 long address)

2. **Expand CV Read Support**:
   - Read CV17/18 (long address)
   - Read CV29 (configuration byte)
   - General CV read (1-1024)

3. **Add CV Write Support**:
   - Write CV1 (short address programming)
   - Write CV17/18 (long address programming)
   - Write CV29 (configuration changes)

4. **POM (Programming on Main)** (future):
   - Write CVs without programming track
   - Useful for function mapping, speed tables

---

## 📝 Implementation Checklist

### Phase 1: ACK Detection Infrastructure
- [ ] Create `lib/PicoDccProgrammer/` directory
- [ ] Implement `PicoDccProgrammer` class
- [ ] Implement `measureBaselineCurrent()`
- [ ] Implement `detectACK()`
- [ ] Add unit tests for ACK detection
- [ ] Test with mock hardware
- [ ] Update `CMakeLists.txt`

### Phase 2: Direct Mode Packet Generation
- [ ] Implement `generateCVReadPacket()`
- [ ] Implement `generateCVVerifyPacket()`
- [ ] Add unit tests for packet generation
- [ ] Verify error byte calculation
- [ ] Test CV1, CV17, CV29 packets

### Phase 3: CV Read Implementation
- [ ] Implement `readCV()` main function
- [ ] Add error handling (invalid CV, no power)
- [ ] Add unit tests for CV read logic
- [ ] Test with mock ACK detection
- [ ] Add diagnostic logging

### Phase 4: DCC-EX Integration
- [ ] Add `<R cv>` command parsing
- [ ] Integrate with `PicoDccController`
- [ ] Add `<r cv value>` response format
- [ ] Update DCC-EX compliance documentation
- [ ] Hardware test with known decoder

---

## 🎉 Completion Criteria

**CV Address Read is COMPLETE when**:
1. ✅ `<R 1>` command returns correct short address
2. ✅ ACK detection works reliably (no false positives/negatives)
3. ✅ All unit tests pass (30+ tests total)
4. ✅ Hardware testing successful with real decoder
5. ✅ Documentation updated (architecture, DCC-EX compliance)
6. ✅ Code coverage > 70% for PicoDccProgrammer
7. ✅ No memory leaks or timing issues

**Then proceed to**: CV write operations or additional CV reads (CV17/18 for long address)

---

**Document Version**: 1.0  
**Last Updated**: October 21, 2025  
**Author**: GitHub Copilot  
**Status**: Ready for implementation
