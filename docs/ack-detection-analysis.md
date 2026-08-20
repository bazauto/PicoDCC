# ACK Detection Implementation Analysis

**Reviewed against `main`**: 2026-08-20 — **not implemented here.** `PicoDccTrack` contains no
ACK detection. The `PicoDccProgrammer` class this document recommends exists only on the
`origin/programming` branch, where reading the current accurately is the current blocker —
exactly the risk this analysis identifies. The tunable parameters (threshold, min/max pulse
duration, baseline, ADC-to-mA factor) *are* implemented and persistable on `main`; nothing
consumes them yet.

## Executive Summary

Based on your responses:
1. **ADC Sampling Rate**: Currently using polled `adc_read()` in main loop - needs dedicated high-speed sampling for ACK detection
2. **Architecture**: Will create `PicoDccProgrammer` class as recommended
3. **Implementation Scope**: Full implementation (all phases) is feasible and recommended

## Critical Challenge: ACK Detection Timing

### NMRA Requirements (S-9.2.3)
- **ACK Pulse**: 60mA current increase for 6ms ±1ms
- **Detection Window**: Must detect within 8ms after bit transmission
- **Baseline Current**: 5-20mA typical decoder idle current
- **Programming Current Limit**: 250mA sustained for >100ms triggers decoder reset

### Raspberry Pi Pico ADC Hardware Capabilities

**Hardware Specifications:**
- **ADC Resolution**: 12-bit (0-4095 range)
- **Reference Voltage**: 3.3V
- **Maximum Sample Rate**: 500 kSPS (500,000 samples/second)
- **Conversion Time**: ~2μs per sample (typical)
- **FIFO Buffer**: 4-sample deep FIFO with DMA support
- **Clock Source**: 48MHz ADC clock (default)

**Key SDK Functions:**
```c
uint16_t adc_read(void);                    // Single blocking read (~2μs)
void adc_fifo_setup(bool en, bool dreq_en, uint16_t dreq_thresh, bool err_in_fifo, bool byte_shift);
uint16_t adc_fifo_get(void);                // Read from FIFO
uint16_t adc_fifo_get_blocking(void);       // Blocking FIFO read
void adc_run(bool run);                     // Start/stop free-running mode
void adc_set_clkdiv(float clkdiv);          // Set sample rate (96MHz / (clkdiv + 1))
```

### Current Implementation Analysis

**Existing Code** (`pico_dcctrack.cpp` line 94):
```cpp
void PicoDccTrack::loop() {
    if (canReadCurrent()) {
        uint reading = adc_read();  // Polled, single sample
        
        // Overcurrent protection at 90% of 4096 (~3686)
        if (reading > (TRACK_POWER_ADC_RANGE / 100 * 90)) {
            setPower(false);
        }
        
        // Current averaging over 2000 samples
        if (current_cnt++ >= TRACK_POWER_CURRENT_SAMPLES) {
            average_current_reading = (float)current_sum / current_cnt;
            current_cnt = 0;
            current_sum = 0;
        }
    }
}
```

**Problems for ACK Detection:**
1. **Sample Rate Unknown**: Depends on main loop iteration speed (likely 1-10 kHz)
2. **Single Sample**: `adc_read()` is polled, not continuous
3. **No Timing Guarantee**: Main loop may be delayed by other processing
4. **Insufficient Resolution**: Need ~10-20 samples during 6ms ACK pulse for reliable detection

### Recommended Solution: Dedicated ACK Detection Window

**Strategy:**
When programming track sends a CV command packet, immediately enter a blocking ACK detection window that:
1. Records baseline current (before packet)
2. Waits for decoder processing time (~10ms)
3. Runs high-speed ADC sampling for 8ms detection window
4. Analyzes samples for 60mA ±15mA increase over baseline lasting 5-7ms

**Implementation Approach:**

```cpp
class PicoDccProgrammer {
private:
    PicoDccTrack *prog_track;
    
    // ACK detection configuration
    static constexpr uint32_t ACK_SAMPLE_RATE_KHZ = 25;  // 25 kHz sampling
    static constexpr uint32_t ACK_WINDOW_MS = 8;         // 8ms detection window
    static constexpr uint32_t ACK_MIN_DURATION_MS = 5;   // Minimum 5ms ACK pulse
    static constexpr uint32_t ACK_MAX_DURATION_MS = 7;   // Maximum 7ms ACK pulse
    static constexpr float ACK_CURRENT_THRESHOLD_MA = 60.0;  // 60mA minimum
    static constexpr float ACK_CURRENT_TOLERANCE_MA = 15.0;  // ±15mA tolerance
    
    // Current sensor calibration
    float baseline_current_ma = 0.0;
    float adc_to_ma_conversion = 0.0;  // Set during initialization
    
    bool detectACK() {
        // Step 1: Measure baseline current (average 10 samples)
        float baseline_adc = measureBaselineCurrent(10);
        
        // Step 2: Send CV packet and wait for decoder processing
        // (Packet transmission happens via normal queue)
        sleep_ms(10);  // Decoder processing time
        
        // Step 3: High-speed sampling during detection window
        const uint32_t total_samples = ACK_SAMPLE_RATE_KHZ * ACK_WINDOW_MS;  // 200 samples
        uint16_t samples[200];  // Stack buffer for samples
        
        // Configure ADC for free-running mode at 25 kHz
        adc_fifo_setup(true, false, 1, false, false);
        adc_set_clkdiv(48000000.0 / ACK_SAMPLE_RATE_KHZ / 1000.0 - 1);  // ~1919 for 25 kHz
        adc_run(true);
        
        // Collect samples (blocks Core 0 for 8ms - acceptable for CV programming)
        uint32_t start_time = time_us_32();
        for (uint32_t i = 0; i < total_samples; i++) {
            samples[i] = adc_fifo_get_blocking();
        }
        uint32_t end_time = time_us_32();
        
        adc_run(false);  // Stop free-running mode
        adc_fifo_drain();
        
        // Step 4: Analyze samples for ACK pulse
        return analyzeACKSamples(samples, total_samples, baseline_adc);
    }
    
    float measureBaselineCurrent(uint32_t num_samples) {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < num_samples; i++) {
            sum += adc_read();
            sleep_us(100);  // 10 kHz sampling for baseline
        }
        return (float)sum / num_samples;
    }
    
    bool analyzeACKSamples(uint16_t *samples, uint32_t count, float baseline_adc) {
        // Convert ADC threshold to counts
        float threshold_adc = baseline_adc + (ACK_CURRENT_THRESHOLD_MA / adc_to_ma_conversion);
        
        // Find contiguous region above threshold
        uint32_t ack_start = 0;
        uint32_t ack_duration_samples = 0;
        bool in_ack = false;
        
        for (uint32_t i = 0; i < count; i++) {
            if (samples[i] > threshold_adc) {
                if (!in_ack) {
                    ack_start = i;
                    in_ack = true;
                }
                ack_duration_samples++;
            } else if (in_ack) {
                // Check if this ACK pulse meets duration requirement
                float duration_ms = (float)ack_duration_samples / ACK_SAMPLE_RATE_KHZ;
                if (duration_ms >= ACK_MIN_DURATION_MS && duration_ms <= ACK_MAX_DURATION_MS) {
                    return true;  // Valid ACK detected
                }
                // Reset and keep searching
                in_ack = false;
                ack_duration_samples = 0;
            }
        }
        
        // Check final ACK region if still active
        if (in_ack) {
            float duration_ms = (float)ack_duration_samples / ACK_SAMPLE_RATE_KHZ;
            if (duration_ms >= ACK_MIN_DURATION_MS && duration_ms <= ACK_MAX_DURATION_MS) {
                return true;
            }
        }
        
        return false;  // No valid ACK found
    }
};
```

### Performance Analysis

**Timing Budget:**
- Baseline measurement: 10 samples × 100μs = 1ms
- Decoder processing delay: 10ms (standard)
- ACK detection window: 8ms at 25 kHz sampling
- **Total Core 0 blocking time**: ~19ms per CV operation

**Impact Assessment:**
- **Acceptable**: CV programming is infrequent (user-initiated only)
- **No DCC Impact**: Programming track is separate from main track (Core 1 still serving main track)
- **No Locomotive Impact**: Main track continues normal operation during programming
- **User Experience**: 19ms is imperceptible to user

**ADC Sample Rate Justification:**
- 25 kHz = 40μs per sample
- 6ms ACK pulse = 150 samples
- Provides excellent resolution for 5-7ms pulse detection
- Well within Pico's 500 kHz maximum capability

### Hardware Considerations

**Current Sensor Requirements:**
Your hardware needs to convert programming track current to ADC voltage:

**Typical Circuit** (verify against your PCB):
```
Programming Track → Current Sense Resistor (0.1Ω - 1.0Ω) → Amplifier → ADC Pin
```

**Calibration Required:**
```cpp
// Example: If 100mA = 1.65V ADC input (mid-scale)
// ADC reading at 100mA = 1.65V / 3.3V × 4096 = 2048
// Conversion factor: 100mA / 2048 counts = 0.0488 mA/count
adc_to_ma_conversion = 0.0488;  // Measure this empirically
```

**Calibration Procedure:**
1. Connect known resistive load to programming track (e.g., 33Ω = 100mA at 12V)
2. Measure ADC reading
3. Calculate conversion factor: `load_current_ma / adc_reading`
4. Store in EEPROM or as constant

### Alternative Approach: DMA-Based Sampling

If free-running mode causes issues, use DMA for background collection:

```cpp
// DMA setup (more complex, but non-blocking)
uint dma_chan = dma_claim_unused_channel(true);
dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
channel_config_set_read_increment(&cfg, false);
channel_config_set_write_increment(&cfg, true);
channel_config_set_dreq(&cfg, DREQ_ADC);

dma_channel_configure(
    dma_chan, &cfg,
    samples,           // Destination buffer
    &adc_hw->fifo,    // Source (ADC FIFO)
    total_samples,    // Transfer count
    true              // Start immediately
);

// Wait for completion
dma_channel_wait_for_finish_blocking(dma_chan);
```

**Benefit**: DMA allows Core 0 to continue processing while ADC samples in background.
**Drawback**: More complex setup, but prevents blocking if needed.

## Recommended Development Sequence

### Phase 1: ACK Detection Infrastructure (Week 1-2)
**Goal**: Validate hardware can detect ACK pulses

1. **Create `PicoDccProgrammer` class skeleton**
   - Constructor takes `PicoDccTrack *prog_track` reference
   - Initialize ADC calibration constants
   - Add unit test mocks for ADC free-running mode

2. **Implement `measureBaselineCurrent()`**
   - Average 10-20 samples to establish baseline
   - Store in member variable for comparison
   - Add test to verify averaging logic

3. **Implement `detectACK()` with free-running ADC**
   - Configure ADC for 25 kHz sampling
   - Collect 200 samples over 8ms window
   - Return raw sample buffer for analysis

4. **Implement `analyzeACKSamples()`**
   - Detect current rise above 60mA threshold
   - Verify pulse duration 5-7ms
   - Return boolean ACK detected/not detected

5. **Hardware Calibration**
   - Measure `adc_to_ma_conversion` factor with known load
   - Add calibration command to DCC-EX protocol (e.g., `<D PROG CAL 100>` = 100mA load connected)
   - Store calibration in constants

6. **Bench Testing**
   - Connect known decoder to programming track
   - Send manual CV29 verify command (e.g., `<V 29 6>`)
   - Observe ACK detection via serial debug output
   - Verify 60mA threshold works with your current sensor

### Phase 2: Direct Mode Packet Generation (Week 2-3)
**Goal**: Generate valid NMRA Direct Mode packets

1. **Implement `generateVerifyByteDirect(cv, value)`**
   - 10-bit CV address encoding
   - Instruction byte: `0111CCAA` format
   - Data byte: expected value
   - Error detection byte (XOR)
   - Return `raw_dcc_cmd_t`

2. **Implement `generateWriteByteDirect(cv, value)`**
   - Instruction byte: `0111CCAA` format
   - CV address and data bytes
   - Error byte calculation
   - Return `raw_dcc_cmd_t`

3. **Implement `generateReadByteDirect(cv)`**
   - Uses verify-scan technique (Phase 3)
   - Tries values 0-255 until ACK received
   - Returns read value or -1 on timeout

4. **Unit Tests**
   - Verify packet byte patterns match NMRA spec
   - Test all CV ranges (1-1024)
   - Test error byte calculation

### Phase 3: DCC-EX Command Integration (Week 3-4)
**Goal**: Parse DCC-EX CV commands and execute

1. **Extend `PicoDccExPacket::parse()`**
   - Add `<W cv value>` (write CV byte)
   - Add `<V cv value>` (verify CV byte)
   - Add `<R cv>` (read CV byte)
   - Add `<B cv bit value>` (write CV bit) - Phase 5

2. **Add `PicoDccController` orchestration**
   - Route CV commands to `PicoDccProgrammer`
   - Ensure programming track has power
   - Call appropriate programmer method
   - Generate DCC-EX response

3. **Implement DCC-EX responses**
   - Success: `<r cv value>` (e.g., `<r 1 3>` = CV1 verified as 3)
   - Failure: `<r -1>` (no ACK received)
   - Include in `dccex_communication.h` macros

4. **Integration Testing**
   - Send `<W 1 5>` via serial, verify CV1 written
   - Send `<V 1 5>` via serial, verify ACK received
   - Send `<R 1>` via serial, verify value read back

### Phase 4: Address Programming (Week 4-5) - YOUR GOAL
**Goal**: Change locomotive addresses

1. **Implement `setShortAddress(address)`**
   - Validate 1-127 range
   - Write CV1 = address
   - Verify write with ACK
   - Clear CV29 bit 5 (short address mode)
   - Return success/failure

2. **Implement `setLongAddress(address)`**
   - Validate 128-10239 range
   - Calculate CV17 (high byte) and CV18 (low byte)
   - Write CV17 and CV18
   - Set CV29 bit 5 (long address mode)
   - Verify all writes
   - Return success/failure

3. **Implement `readAddress()` helper**
   - Read CV29 bit 5 to determine mode
   - If short: return CV1
   - If long: return (CV17 << 8) | CV18
   - Return 0 on failure

4. **Add high-level DCC-EX commands**
   - `<W ADDR SHORT cab>` (e.g., `<W ADDR SHORT 12>`)
   - `<W ADDR LONG cab>` (e.g., `<W ADDR LONG 1234>`)
   - `<R ADDR>` (read current address)

5. **Your Test Case**
   - Place first locomotive on programming track
   - Send `<R ADDR>` - should return 3
   - Send `<W ADDR SHORT 10>`
   - Send `<R ADDR>` - should return 10
   - Move to main track, test throttle at address 10
   - Repeat for second locomotive with address 11

### Phase 5: Bit-Level Operations (Week 5-6)
**Goal**: Support bit manipulation for advanced CVs

1. **Implement `generateWriteBitDirect(cv, bit, value)`**
   - Instruction byte: `0111DCAA` (D=1 for bit write)
   - Data byte format: `111CDBBB` (C=verify, D=value, BBB=bit position)
   - Error byte
   - Return `raw_dcc_cmd_t`

2. **Implement `generateVerifyBitDirect(cv, bit, value)`**
   - Similar to write but with C=0 in data byte
   - Used for read-bit-by-verify

3. **Extend DCC-EX parser**
   - `<B cv bit value>` command
   - E.g., `<B 29 5 1>` sets CV29 bit 5 (long address mode)

4. **Use Cases**
   - CV29 configuration (direction, speed steps, etc.)
   - Decoder feature bits
   - Advanced lighting effects

### Phase 6: Legacy Mode Support (Week 6-7)
**Goal**: Support older decoders without Direct Mode

1. **Implement Physical Register Mode (NMRA S-9.2.3 Appendix A)**
   - Only supports CV1-8 (registers 1-8)
   - Simpler packet format
   - Required for very old decoders (pre-2002)

2. **Auto-detection Strategy**
   - Try Direct Mode first (preferred)
   - If no ACK, fallback to Register Mode
   - Report mode used in DCC-EX response

## Testing Strategy

### Unit Tests (CMocka)
- Packet generation correctness
- ACK detection logic with synthetic samples
- CV address encoding/decoding
- Error byte calculation
- Sample analysis edge cases (noise, partial pulses)

### Bench Tests (Real Hardware)
1. **ACK Detection Validation**
   - Connect known-good decoder
   - Verify 60mA pulse detection
   - Measure false-positive rate with noise

2. **CV Programming Validation**
   - Write CV1, read back, verify
   - Write CV29, read back, verify
   - Write long address CV17/18, verify

3. **Address Programming Validation**
   - Program address 3 → 10
   - Test on main track
   - Repeat for address 11

4. **Stress Testing**
   - Multiple CV writes in sequence
   - Verify no memory corruption
   - Test with different decoder brands

### Known Decoder Compatibility
**Recommended Test Decoders:**
- ESU LokSound V5 (excellent ACK, reverses motor direction during ACK)
- TCS (standard ACK implementation)
- Digitrax (widely compatible)
- NCE (common in N scale)

## Risk Mitigation

### Risk 1: ADC Noise
**Symptom**: False ACK detections from electrical noise
**Mitigation**:
- Require minimum pulse duration (5ms)
- Use hysteresis in threshold detection
- Add low-pass filter to current sensor hardware
- Average multiple samples during pulse

### Risk 2: Baseline Drift
**Symptom**: Decoder idle current changes, invalidates baseline
**Mitigation**:
- Re-measure baseline before each CV operation
- Use relative threshold (60mA increase, not absolute value)
- Add baseline drift detection (reject if >20% change)

### Risk 3: Decoder Non-Compliance
**Symptom**: Decoder sends 50mA ACK or 8ms pulse
**Mitigation**:
- Make threshold configurable (50-70mA range)
- Allow pulse duration 4-8ms (wider tolerance)
- Add diagnostic mode to report actual ACK characteristics

### Risk 4: Core 0 Blocking Impact
**Symptom**: 19ms blocking causes main track hiccups
**Mitigation**:
- Ensure Core 1 (main track PIO) is independent
- Test main track operation during CV programming
- If needed, switch to DMA-based sampling (non-blocking)

## Hardware Verification Checklist

Before implementation, verify your PCB design:

- [ ] Programming track current sensor circuit identified
- [ ] Current sense resistor value known (0.1Ω - 1.0Ω typical)
- [ ] Amplifier gain calculated (if present)
- [ ] ADC voltage range at 60mA calculated
- [ ] ADC voltage range at 250mA verified < 3.3V (avoid damage)
- [ ] Known-good decoder available for testing
- [ ] Programming track isolated from main track (no shared ground issues)
- [ ] 250mA programming track power supply confirmed

## Calibration Procedure

**One-time setup per hardware:**

1. Connect calibration resistor to programming track:
   - Example: 120Ω resistor at 12V = 100mA load
   
2. Enable programming track power:
   - Send `<1 PROG>` DCC-EX command
   
3. Read ADC value:
   - Add debug command: `<D PROG ADC>` returns raw ADC reading
   
4. Calculate conversion factor:
   ```
   adc_to_ma_conversion = 100.0mA / adc_reading
   ```
   Example: If adc_reading = 2048
   → adc_to_ma_conversion = 100.0 / 2048 = 0.0488 mA/count
   
5. Update constant in code:
   ```cpp
   static constexpr float ADC_TO_MA = 0.0488;  // Measured value
   ```

6. Verify with decoder:
   - Connect decoder, send `<V 29 6>`
   - Observe ACK current via `<D PROG ADC>` command
   - Should see ~2048 + (60mA / 0.0488) = ~3278 during ACK

## Documentation Updates

When implementation complete, update:

1. **`docs/architecture.md`**
   - Add PicoDccProgrammer component
   - Document ACK detection architecture
   - Update CV programming status

2. **`docs/dccex-compliance-analysis.md`**
   - Change CV programming from "STUBS ONLY" to "IMPLEMENTED"
   - List supported commands
   - Note any limitations

3. **`.github/copilot-instructions.md`**
   - Update service mode programming status
   - Add ACK detection patterns
   - Document calibration procedure

4. **User Documentation** (new file: `docs/cv-programming-guide.md`)
   - How to program addresses
   - Supported DCC-EX commands
   - Troubleshooting ACK detection
   - Calibration instructions

## Next Steps

**Immediate Actions:**
1. **Verify hardware**: Check your PCB schematic for current sensor circuit
2. **Measure calibration**: Connect 100mA load, read ADC, calculate conversion factor
3. **Create PicoDccProgrammer class**: Start with Phase 1 implementation
4. **Implement ACK detection**: Use free-running ADC at 25 kHz
5. **Bench test**: Verify ACK detection with known decoder before proceeding to Phase 2

**Development Timeline:**
- **Week 1-2**: Phase 1 (ACK detection) + calibration
- **Week 2-3**: Phase 2 (packet generation)
- **Week 3-4**: Phase 3 (DCC-EX integration)
- **Week 4-5**: **Phase 4 (address programming - YOUR GOAL)**
- **Week 5-6**: Phase 5 (bit operations)
- **Week 6-7**: Phase 6 (legacy modes)

This is a comprehensive but achievable implementation plan. The key technical challenge (ACK detection timing) is solved by using the Pico's free-running ADC mode at 25 kHz during a dedicated 8ms detection window.

Are you ready to proceed with Phase 1 (ACK detection infrastructure)?
