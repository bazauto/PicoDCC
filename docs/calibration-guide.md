# PicoDCC Calibration Guide

## Overview

This guide explains how to calibrate your PicoDCC hardware for accurate CV programming ACK detection. Calibration is a one-time process that measures your hardware's current sensor characteristics and stores the values in non-volatile flash memory.

## Why Calibration is Needed

CV programming requires detecting a 60mA current pulse (ACK) from decoders on the programming track. Your hardware's current sensor circuit converts track current to an ADC voltage reading. The relationship between current and ADC value varies based on:

- Current sense resistor value (typically 0.1Ω - 1.0Ω)
- Amplifier gain (if present)
- Voltage divider ratios
- Component tolerances

Calibration measures this relationship and stores the conversion factor for runtime use.

## Prerequisites

Before calibration, you need:

1. **Calibration Load**: A known resistive load that draws ~100mA from the programming track
   - Example: 120Ω resistor at 12V = 100mA
   - Or a known-good decoder configured to draw consistent current
   
2. **Serial Terminal**: Connected to PicoDCC USB port at 115200 baud

3. **Power Supply**: Your normal DCC power supply (12-18V)

4. **Multimeter** (optional): To verify actual load current

## Calibration Workflow

### Step 1: Connect Calibration Load

Connect your 100mA calibration load to the programming track terminals.

**Example using resistor:**
```
Programming Track (+) ----[ 120Ω Resistor ]---- Programming Track (-)
```

**Calculating your resistor value:**
```
R = V / I
R = 12V / 0.100A = 120Ω  (for 12V supply)
R = 15V / 0.100A = 150Ω  (for 15V supply)
```

**💡 Tip**: Use a power resistor rated for at least 2W to avoid overheating.

### Step 2: Start Calibration Mode

Send the calibration start command via serial terminal:

```
<D CAL START>
```

**Expected Response:**
```
<D CAL START OK>
<D CAL MSG Connect 100mA calibration load to programming track>
<D CAL MSG Enable programming track power: <1 PROG>>
<D CAL MSG Read ADC value: <D CAL ADC>>
<D CAL MSG Set calibration: <D CAL SET 100.0 <adc_value>>>
<D CAL MSG Save calibration: <D CAL SAVE>>
```

### Step 3: Enable Programming Track Power

```
<1 PROG>
```

**Expected Response:**
```
<p1 PROG>
```

The programming track is now powered and your calibration load should be drawing current.

**⚠️ Warning**: The load will heat up. Don't leave power on for extended periods.

### Step 4: Read ADC Value

```
<D CAL ADC>
```

**Expected Response:**
```
<D CAL ADC 1 VALUE 2048>
```

This shows:
- ADC channel 1 (programming track current sensor)
- Current ADC reading: 2048 counts (example)

**💡 Note**: The value will depend on your hardware. Typical range: 500-3000 counts for 100mA.

### Step 5: Set Calibration Factor

Using the ADC value from Step 4, calculate and set the conversion factor:

```
<D CAL SET 100.0 2048>
```

Replace `2048` with your actual ADC reading.

**Expected Response:**
```
<D CAL SET OK ADC_MA=0.0488 (100.0mA @ 2048 counts)>
```

This calculates and sets the conversion factor:
```
conversion = 100.0 mA / 2048 counts = 0.0488 mA/count
```

### Step 6: Save Calibration to Flash

```
<D CAL SAVE>
```

**Expected Response:**
```
<D CONFIG SAVING>
<D CONFIG SAVE OK>
```

**⏱️ Note**: Flash write takes ~410ms and briefly pauses track operations. This is normal.

### Step 7: Verify Calibration

```
<D CONFIG GET ADC_MA>
```

**Expected Response:**
```
<D CONFIG ADC_MA 0.0488>
```

This confirms your calibration factor is stored in flash memory.

### Step 8: Power Down and Remove Load

```
<0 PROG>
```

**Expected Response:**
```
<p0 PROG>
```

Remove the calibration load. **Calibration is complete!**

## Verifying Calibration with a Decoder

To verify your calibration works with real decoders:

### Step 1: Place Decoder on Programming Track

Connect a known-good decoder (locomotive) to the programming track.

### Step 2: Enable Programming Track

```
<1 PROG>
```

### Step 3: Read Current ADC Value

```
<D CAL ADC>
```

**Expected Response:**
```
<D CAL ADC 1 VALUE 2048>
```

This should show the decoder's idle current (~5-20mA typical).

**Example calculation:**
```
Current = ADC × Conversion
Current = 2048 × 0.0488 mA/count = 100mA
```

### Step 4: Send a CV Verify Command (Future Feature)

Once CV programming is implemented:
```
<V 29 6>
```

This verifies CV29 has value 6. During ACK, you should see current jump by 60mA:
```
Idle:  2048 counts =  100mA
ACK:   3277 counts =  160mA  (100mA + 60mA ACK pulse)
```

## Configuration Management Commands

### View All Configuration

```
<D CONFIG GET ALL>
```

**Response:**
```
<D CONFIG ALL ADC_MA=0.0488 ACK_THRESH=60.0 ACK_MIN=5.0 ACK_MAX=7.0 
 BASELINE=10.0 MAIN_LIMIT=3000 PROG_LIMIT=250>
```

### View Individual Parameters

```
<D CONFIG GET ADC_MA>       → ADC conversion factor
<D CONFIG GET ACK_THRESH>   → ACK detection threshold (mA)
<D CONFIG GET ACK_MIN>      → Minimum ACK duration (ms)
<D CONFIG GET ACK_MAX>      → Maximum ACK duration (ms)
<D CONFIG GET BASELINE>     → Baseline current (mA)
<D CONFIG GET MAIN_LIMIT>   → Main track current limit (mA)
<D CONFIG GET PROG_LIMIT>   → Prog track current limit (mA)
```

### Set Individual Parameters

```
<D CONFIG SET ACK_THRESH 55.0>     → Adjust ACK threshold for finicky decoders
<D CONFIG SET ACK_MIN 4.5>         → Allow shorter ACK pulses
<D CONFIG SET ACK_MAX 7.5>         → Allow longer ACK pulses
<D CONFIG SET MAIN_LIMIT 2500>     → Set main track current limit to 2.5A
```

**⚠️ Important**: Changes are only in RAM until saved!

### Save Configuration to Flash

```
<D CONFIG SAVE>
```

**⏱️ Note**: Takes ~410ms, briefly pauses both tracks.

### Reset to Factory Defaults

```
<D CONFIG RESET>
```

This resets all parameters to:
```
ADC_MA      = 0.0488  (must recalibrate!)
ACK_THRESH  = 60.0 mA
ACK_MIN     = 5.0 ms
ACK_MAX     = 7.0 ms
BASELINE    = 10.0 mA
MAIN_LIMIT  = 3000 mA
PROG_LIMIT  = 250 mA
```

**⚠️ Warning**: You'll need to recalibrate after reset!

### Export Configuration (Backup)

```
<D CONFIG EXPORT>
```

**Response:**
```
<D CONFIG ALL ADC_MA=0.0512 ACK_THRESH=55.0 ACK_MIN=4.5 ACK_MAX=7.5 
 BASELINE=12.0 MAIN_LIMIT=2500 PROG_LIMIT=300>
```

Save this output to restore configuration after firmware updates or resets.

## Troubleshooting

### Problem: ADC reading is 0

**Cause**: Current sensor circuit not connected or power off

**Solution**:
- Verify programming track power is enabled: `<1 PROG>`
- Check calibration load is connected
- Verify PCB current sensor circuit wiring

### Problem: ADC reading is 4095 (maximum)

**Cause**: Current sensor circuit saturated or shorted

**Solution**:
- Check for short circuit on programming track
- Verify load resistor value (should be >100Ω)
- Check current sensor amplifier circuit

### Problem: Calibration factor seems wrong

**Symptoms**: ACK detection fails or false positives

**Solution**:
- Verify actual load current with multimeter
- Use correct load current value in `<D CAL SET>`
- Example: If resistor actually draws 95mA, use `<D CAL SET 95.0 2048>`

### Problem: Configuration lost after firmware update

**Cause**: Firmware grew into last 4KB flash sector

**Solution**:
- Check firmware size: should be < 2044KB (2MB - 4KB)
- Re-calibrate using this guide
- Or: export config before update, import after

### Problem: "SAVE FAILED" error

**Cause**: Flash write failed or verification mismatch

**Solution**:
- Try again (flash write occasionally fails)
- If persistent, may indicate flash hardware issue
- Check power supply stability during save

## Advanced: Manual ADC Reading

For hardware verification, you can read ADC directly:

```
<D CAL ADC 0>   → Read ADC channel 0
<D CAL ADC 1>   → Read ADC channel 1 (programming track)
<D CAL ADC 2>   → Read ADC channel 2 (main track, if equipped)
<D CAL ADC 3>   → Read ADC channel 3
```

## Technical Details

### Flash Storage

Configuration is stored in the last 4KB sector of 2MB flash:
- **Address**: 0x101FF000 (offset 0x1FF000)
- **Size**: 4096 bytes (4KB sector)
- **Write endurance**: ~10,000 cycles (decades of use)
- **Write time**: ~410ms (both cores paused)

### Configuration Structure

```c
typedef struct {
    uint32_t magic;              // 0x50444343 ("PDCC")
    uint32_t version;            // Structure version
    float adc_to_ma_conversion;  // Calibration factor
    float ack_threshold_ma;      // ACK threshold
    float ack_min_duration_ms;   // Min ACK duration
    float ack_max_duration_ms;   // Max ACK duration
    float baseline_current_ma;   // Baseline current
    uint16_t main_track_current_limit_ma;
    uint16_t prog_track_current_limit_ma;
    uint8_t reserved[3960];      // Future expansion
    uint32_t checksum;           // CRC32 validation
} pico_config_t;
```

### CRC32 Validation

Configuration integrity is protected by CRC32 checksum (polynomial 0xEDB88320). Corrupted config automatically falls back to factory defaults.

## Summary

1. Connect 100mA calibration load to programming track
2. Send `<D CAL START>` to begin calibration
3. Enable programming track: `<1 PROG>`
4. Read ADC value: `<D CAL ADC>`
5. Set calibration: `<D CAL SET 100.0 <adc_value>>`
6. Save to flash: `<D CAL SAVE>`
7. Verify: `<D CONFIG GET ADC_MA>`
8. Power down: `<0 PROG>`

Calibration survives power cycles and firmware updates (if firmware < 2044KB).

For questions or issues, refer to `docs/non-volatile-storage-options.md` for technical details.
