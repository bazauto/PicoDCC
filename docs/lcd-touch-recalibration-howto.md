# Touch Calibration - Code Replacement Guide

## Quick Instructions

1. **Run Calibration**
   ```
   Send: <C>
   Follow the on-screen prompts (27 touches total)
   ```

2. **Copy the Generated Code**
   Look for this section in the serial output:
   ```
   Suggested Calibration Code:
   ----------------------------------------
   [Copy everything from here...]
   ----------------------------------------
   ```

3. **Open the File**
   ```
   lib/PicoDCCDisplay/pico_dcc_display.cpp
   ```

4. **Find the Calibration Section**
   Search for: `BEGIN TOUCH CALIBRATION MAPPING`
   
   You'll see:
   ```cpp
   // ========== BEGIN TOUCH CALIBRATION MAPPING ==========
   // This section maps raw touch coordinates to display coordinates.
   // To recalibrate: Send <C> command, follow prompts, and replace this entire section
   // with the generated code from the serial output.
   
   [Old calibration code here]
   
   // ========== END TOUCH CALIBRATION MAPPING ==========
   ```

5. **Replace Everything Between the Markers**
   - Keep the `BEGIN` marker line
   - **Delete** everything between BEGIN and END
   - **Paste** the new calibration code (including comments)
   - Keep the `END` marker line

6. **Rebuild and Flash**
   ```bash
   cd build
   cmake --build .
   # Flash the firmware
   ```

## Example Replacement

### Before (old calibration):
```cpp
// ========== BEGIN TOUCH CALIBRATION MAPPING ==========
// This section maps raw touch coordinates to display coordinates.
// To recalibrate: Send <C> command, follow prompts, and replace this entire section
// with the generated code from the serial output.

// X-axis calibration (from middle row Y=120):
// Left   (X= 40): Raw Y = 3269
// Center (X=160): Raw Y = 1151
// Right  (X=280): Raw Y = 1323

// Y-axis calibration (from middle column X=160):
// Top    (Y= 30): Raw X =  265
// Middle (Y=120): Raw X = 1801
// Bottom (Y=210): Raw X = 3254

int raw_y = points[0].y;
int raw_x = points[0].x;
int scaled_x, scaled_y;

// Map raw Y to display X:
if (raw_y <= 1151) {
    scaled_x = 40 + ((raw_y - 3269) * (160 - 40)) / (1151 - 3269);
} else if (raw_y <= 1323) {
    scaled_x = 160 + ((raw_y - 1151) * (280 - 160)) / (1323 - 1151);
} else {
    scaled_x = 280 + ((raw_y - 1323) * (320 - 280)) / (4095 - 1323);
}

// Map raw X to display Y:
if (raw_x <= 1801) {
    scaled_y = 30 + ((raw_x - 265) * (120 - 30)) / (1801 - 265);
} else if (raw_x <= 3254) {
    scaled_y = 120 + ((raw_x - 1801) * (210 - 120)) / (3254 - 1801);
} else {
    scaled_y = 210 + ((raw_x - 3254) * (240 - 210)) / (4095 - 3254);
}

// Clamp to screen bounds:
if (scaled_x < 0) scaled_x = 0;
if (scaled_x > 320) scaled_x = 320;
if (scaled_y < 0) scaled_y = 0;
if (scaled_y > 240) scaled_y = 240;

data->point.x = scaled_x;
data->point.y = scaled_y;

// ========== END TOUCH CALIBRATION MAPPING ==========
```

### After (new calibration from serial output):
```cpp
// ========== BEGIN TOUCH CALIBRATION MAPPING ==========
// This section maps raw touch coordinates to display coordinates.
// To recalibrate: Send <C> command, follow prompts, and replace this entire section
// with the generated code from the serial output.

// X-axis calibration (from middle row Y=120):
// Left   (X= 40): Raw Y = 3301    <-- New values
// Center (X=160): Raw Y = 1165
// Right  (X=280): Raw Y = 1340

// Y-axis calibration (from middle column X=160):
// Top    (Y= 30): Raw X =  271
// Middle (Y=120): Raw X = 1815
// Bottom (Y=210): Raw X = 3268

int raw_y = points[0].y;
int raw_x = points[0].x;
int scaled_x, scaled_y;

// Map raw Y to display X:
if (raw_y <= 1165) {
    scaled_x = 40 + ((raw_y - 3301) * (160 - 40)) / (1165 - 3301);
} else if (raw_y <= 1340) {
    scaled_x = 160 + ((raw_y - 1165) * (280 - 160)) / (1340 - 1165);
} else {
    scaled_x = 280 + ((raw_y - 1340) * (320 - 280)) / (4095 - 1340);
}

// Map raw X to display Y:
if (raw_x <= 1815) {
    scaled_y = 30 + ((raw_x - 271) * (120 - 30)) / (1815 - 271);
} else if (raw_x <= 3268) {
    scaled_y = 120 + ((raw_x - 1815) * (210 - 120)) / (3268 - 1815);
} else {
    scaled_y = 210 + ((raw_x - 3268) * (240 - 210)) / (4095 - 3268);
}

// Clamp to screen bounds:
if (scaled_x < 0) scaled_x = 0;
if (scaled_x > 320) scaled_x = 320;
if (scaled_y < 0) scaled_y = 0;
if (scaled_y > 240) scaled_y = 240;

data->point.x = scaled_x;
data->point.y = scaled_y;

// ========== END TOUCH CALIBRATION MAPPING ==========
```

## Important Notes

- **Don't modify the marker comments** (`BEGIN`/`END` lines)
- **Keep the instructions comment** at the top of the section
- **Include all the calibration comments** (X-axis, Y-axis values)
- **Paste the exact code** from the serial output (don't try to manually edit values)
- The generated code is **complete** - don't add or remove anything

## Troubleshooting

**Q: I only see partial code in the serial output**  
A: Your terminal buffer might be too small. Increase buffer size or use a different terminal program.

**Q: The code has syntax errors**  
A: Make sure you copied the complete section between the dashed lines in the serial output.

**Q: Touch is still inaccurate after calibration**  
A: Try recalibrating - touch the exact center of each crosshair more carefully.

**Q: I lost my previous calibration**  
A: Check git history or re-run calibration. The current values are saved in the code comments.
