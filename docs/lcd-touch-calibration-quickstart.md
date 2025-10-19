# Touch Calibration Quick Start

## TL;DR - Just Do This

1. Connect serial terminal (115200 baud)
2. Send: `<C>` (or `<CAL>`)
3. Touch each red crosshair as it appears (27 touches total)
4. Copy the code from serial output
5. Paste into `touchCallback()` in `lib/PicoDCCDisplay/pico_dcc_display.cpp`
6. Rebuild and flash

**Note**: Touch calibration is **serial-only**. The CALIBRATE button on the LCD is for programming track settings, not touch calibration.

## What You'll See

### Serial Terminal
```
<CAL OK - Starting calibration...>

========================================
TOUCH CALIBRATION STARTED
========================================
Instructions:
- You will see a crosshair (+) on the display
- Touch the CENTER of each crosshair
- Each point will be sampled 3 times
- Total: 9 points × 3 samples = 27 touches
- Wait for 'Next point...' before moving
========================================

Point 1/9: Touch crosshair at (40, 30)
  Sample 1/3: Raw(1547, 3370)
  Sample 2/3: Raw(1552, 3361)
  Sample 3/3: Raw(1547, 3384)

Point 2/9: Touch crosshair at (160, 30)
  ...

[After all points collected]

========================================
CALIBRATION DATA COLLECTED
========================================

[Detailed calibration data and ready-to-use code]

========================================
CALIBRATION COMPLETE
Copy the calibration code above into
touchCallback() in pico_dcc_display.cpp
========================================
```

### LCD Display
```
┌────────────────────────┐
│                        │
│        Touch +         │
│       at (40,30)       │
│       Sample 1/3       │
│                        │
│          +             │  ← Red crosshair
│                        │
│                        │
└────────────────────────┘
```

## Where to Paste the Code

Open `lib/PicoDCCDisplay/pico_dcc_display.cpp` and find the `touchCallback()` function.

Replace this section:
```cpp
// CST328 provides 12-bit coordinates (0-4095)
// Touch panel is rotated 90° with axes swapped AND inverted

// [Current calibration code]

int raw_y = points[0].y;
int scaled_x;

if (raw_y <= 572) {
    // [Old calibration logic]
}
// ... more old code ...

data->point.x = scaled_x;
data->point.y = scaled_y;
```

With the generated code from the serial output (will look like):
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

## Tips

- **Touch firmly**: But not too hard
- **Center of crosshair**: Accuracy matters
- **Wait for confirmation**: LCD updates between points
- **Keep serial log**: Save the output for reference
- **Test after**: Verify buttons activate correctly

## Troubleshooting

### "No response to <CAL>"
- Check baud rate (115200)
- Use angle brackets: `<CAL>` not `CAL`
- Ensure firmware is flashed

### "Touch not detected"
- Clean LCD screen
- Touch more firmly
- Check if INT pin is working (should pulse on touch)

### "Buttons still wrong after calibration"
- Did you rebuild the firmware?
- Did you paste the code correctly?
- Try calibrating again - first attempt may not be perfect

## See Full Documentation
`docs/lcd-touch-calibration-guide.md` - Complete guide with technical details
