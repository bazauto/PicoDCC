# LCD Touch Calibration Guide

## Overview
The PicoDCC touch screen calibration system allows you to calibrate the CST328 capacitive touch controller to accurately map raw touch coordinates to display positions. This is essential because the touch panel orientation and scaling may not perfectly align with the display.

## When to Calibrate
- **First time setup**: After assembling the hardware
- **After hardware changes**: If you replace the touch panel or LCD
- **If touch accuracy degrades**: Occasional recalibration may be needed
- **When adding new screens**: Different UI layouts may benefit from fresh calibration

## Calibration Methods

### Serial Command (Only Method)
Touch screen calibration is triggered via serial command only.

1. **Connect to Serial Console**
   - Baud rate: 115200
   - Data bits: 8, Parity: None, Stop bits: 1
   - Use PuTTY, screen, minicom, or similar terminal

2. **Send Calibration Command**
   ```
   <C>
   ```
   You should see:
   ```
   <CAL OK - Starting calibration...>
   ```

3. **Calibration Screen Appears**
   - The LCD automatically switches to calibration mode
   - You'll see a red crosshair and instructions

4. **Follow the Instructions**
   The calibration system will guide you through touching 9 points:
   - 3×3 grid covering the entire screen
   - Each point sampled 3 times for accuracy
   - Total: 27 touches required
   
   The LCD will show:
   ```
   Touch + at (X,Y)
   Sample N/3
   ```
   
   Touch the CENTER of the red crosshair each time.

5. **Collect the Output**
   After completing all touches, the serial console will output:
   - Raw calibration data for all 9 points
   - **Ready-to-use C++ code** for the calibration

6. **Apply the Calibration**
   - Copy the generated code from the serial output
   - Paste it into `lib/PicoDCCDisplay/pico_dcc_display.cpp`
   - Replace the coordinate transformation logic in `touchCallback()`
   - Rebuild and flash the firmware

## Important Notes

- **Touch calibration is serial-only**: The CALIBRATE button on the LCD is reserved for programming track parameter calibration (CV read/write settings, ACK detection, etc.)
- **Serial access required**: You must have access to the serial console to calibrate the touch screen
- **One-time setup**: Once calibrated, the settings are saved in the compiled firmware until you recalibrate

## Calibration Screen Details

### Grid Points
The calibration uses a 3×3 grid at these display coordinates:

| Position | X coordinate | Y coordinate |
|----------|--------------|--------------|
| Top-Left | 40 | 30 |
| Top-Center | 160 | 30 |
| Top-Right | 280 | 30 |
| Mid-Left | 40 | 120 |
| Mid-Center | 160 | 120 |
| Mid-Right | 280 | 120 |
| Bottom-Left | 40 | 210 |
| Bottom-Center | 160 | 210 |
| Bottom-Right | 280 | 210 |

### Sampling
- **3 samples per point**: Reduces the impact of jittery readings
- **200ms debounce**: Prevents accidental double-touches
- **Raw coordinates averaged**: Improves accuracy

### Output Format
The serial output provides:

1. **Calibration Point Data** - Raw measurements:
   ```
   Point 1: Display( 40, 30) -> Raw(1547, 3370)
   Point 2: Display(160, 30) -> Raw(1542, 2102)
   ...
   ```

2. **Piecewise Linear Calibration Code** - Ready to paste:
   ```cpp
   // X-axis calibration (from middle row Y=120):
   // Left   (X= 40): Raw Y = 3377
   // Center (X=160): Raw Y = 2100
   // Right  (X=280): Raw Y =  810
   
   // Y-axis calibration (from middle column X=160):
   // Top    (Y= 30): Raw X = 1542
   // Middle (Y=120): Raw X = 1798
   // Bottom (Y=210): Raw X = 1809
   
   int raw_y = points[0].y;
   int raw_x = points[0].x;
   int scaled_x, scaled_y;
   
   // Map raw Y to display X:
   if (raw_y <= 2100) {
       scaled_x = 40 + ((raw_y - 3377) * (160 - 40)) / (2100 - 3377);
   } else if (raw_y <= 810) {
       scaled_x = 160 + ((raw_y - 2100) * (280 - 160)) / (810 - 2100);
   } else {
       scaled_x = 280 + ((raw_y - 810) * (320 - 280)) / (4095 - 810);
   }
   
   // Map raw X to display Y:
   if (raw_x <= 1798) {
       scaled_y = 30 + ((raw_x - 1542) * (120 - 30)) / (1798 - 1542);
   } else if (raw_x <= 1809) {
       scaled_y = 120 + ((raw_x - 1798) * (210 - 120)) / (1809 - 1798);
   } else {
       scaled_y = 210 + ((raw_x - 1809) * (240 - 210)) / (4095 - 1809);
   }
   
   // Clamp to screen bounds:
   if (scaled_x < 0) scaled_x = 0;
   if (scaled_x > 320) scaled_x = 320;
   if (scaled_y < 0) scaled_y = 0;
   if (scaled_y > 240) scaled_y = 240;
   
   data->point.x = scaled_x;
   data->point.y = scaled_y;
   ```

## Piecewise Linear Interpolation

The calibration uses **piecewise linear interpolation** instead of a simple linear mapping because:

1. **Touch panels are non-linear**: The CST328 may have non-uniform sensitivity
2. **Display rotation effects**: The 270° rotation creates complex transformations
3. **Hardware variations**: Each panel may have different characteristics

The piecewise approach:
- Divides the screen into segments (e.g., left-center-right for X-axis)
- Uses linear interpolation within each segment
- Provides better accuracy across the entire screen

## Tips for Accurate Calibration

### Before Starting
- Clean the LCD surface (fingerprints affect accuracy)
- Ensure good lighting so you can see the crosshairs clearly
- Have the serial console visible to follow instructions

### During Calibration
- **Touch firmly but not hard**: Just enough to register
- **Touch the center of the crosshair**: Not the edges
- **Hold steady**: Wait for the beep/confirmation before lifting
- **Wait for "Next point..."**: Don't rush to the next touch

### After Calibration
- **Test all UI elements**: Verify buttons activate correctly
- **Check edge accuracy**: Touch near screen edges
- **Compare with previous calibration**: If wildly different, recalibrate

## Troubleshooting

### Calibration Won't Start
- **Check serial command**: Must be exactly `<CAL>` with angle brackets
- **Try button method**: Press CALIBRATE button on LCD
- **Check LCD init**: Ensure LCD boot sequence completed successfully

### Touch Not Detected During Calibration
- **Check INT pin**: Should pulse on touch (GPIO 10)
- **Verify I2C**: Touch controller at address 0x1A
- **Check reset**: Touch controller needs proper reset on boot

### Generated Code Looks Wrong
- **Extreme values**: If raw coordinates are all near 0 or 4095, touch controller may not be working
- **Reversed axes**: If X and Y seem swapped, this is normal (270° rotation)
- **Non-monotonic**: If raw values don't increase/decrease smoothly across the grid, recalibrate

### Buttons Still Misaligned After Calibration
- **Verify code placement**: Ensure you replaced the correct section in `touchCallback()`
- **Check rebuild**: Make sure you rebuilt and flashed the firmware
- **Re-run calibration**: First attempt may not be perfect
- **Check for typos**: Code must be pasted exactly as generated

## Future Enhancements

### Non-Volatile Storage (Planned)
Once `PicoConfigStorage` is implemented, calibration parameters will be saved to flash memory:
- No need to recompile after calibration
- Calibration survives power cycles
- Multiple calibration profiles possible

### Per-Screen Calibration (Future)
When multiple UI screens are implemented:
- Each screen may have its own calibration
- Calibration tied to UI layout
- Automatic calibration switching

## Technical Details

### Component Files
- `lib/PicoDCCDisplay/touch_calibration.h` - Calibration class interface
- `lib/PicoDCCDisplay/touch_calibration.cpp` - Calibration logic and output generation
- `lib/PicoDCCDisplay/pico_dcc_display.cpp` - Integration with display loop
- `lib/PicoDCCEX/pico_dccexpacket.h` - `<CAL>` command detection
- `lib/PicoDCCController/pico_dcccontroller.cpp` - Calibration request handling

### Serial Protocol
- Command: `<CAL>` (DCC-EX style bracket syntax)
- Response: `<CAL OK - Touch CALIBRATE button on LCD>`
- Opcode: `C` with parameters `A` and `L` (spells "CAL")

### Calibration Algorithm
1. Initialize 3×3 grid of calibration points
2. For each point (9 total):
   - Display red crosshair at position
   - Collect 3 raw touch samples
   - Average the samples to reduce noise
3. Compute piecewise linear mappings:
   - X-axis: Use middle row (Y=120) samples
   - Y-axis: Use middle column (X=160) samples
4. Generate interpolation code
5. Output to serial console

### Display Integration
- Calibration screen uses LVGL graphics
- Crosshair drawn with lines (2px red)
- Instruction text updated per point
- Automatic return to diagnostic screen when complete

## See Also
- `docs/lcd-touch-debug-status.md` - Touch debugging history
- `docs/lcd-implementation-plan.md` - Overall LCD integration plan
- `docs/architecture.md` - System architecture overview
