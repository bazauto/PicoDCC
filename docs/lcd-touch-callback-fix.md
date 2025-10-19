# Touch Callback Fix - CST328 Polling Implementation

## Problem Identified

**User Report**: "The interrupt is not fired. With an oscilloscope on the output from the driver chip I see it pulling the line low. But the touchCallback isn't called."

## Root Cause Analysis

### Misconception About "Interrupt"
The INT pin is being monitored by the **CST328 hardware**, but our implementation uses **LVGL polling**, not GPIO interrupts. Here's what's actually happening:

1. **CST328 pulls INT pin LOW** when touched (confirmed via oscilloscope ✓)
2. **LVGL calls `touchCallback()`** periodically during `lv_timer_handler()` (~10-30Hz)
3. **Callback should read I2C data** whenever LVGL polls it
4. **No GPIO interrupt handler** is involved in this design

### Bugs in Original Callback

**Original code had two critical bugs**:

```cpp
// BUG 1: Checked stale data before reading new data
uint16_t x, y;
if (instance_->touch_.getLastTouch(&x, &y)) {  // Old data!
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
} else {
    data->state = LV_INDEV_STATE_RELEASED;
}

// BUG 2: Read touch AFTER reporting to LVGL
TouchPoint points[1];
instance_->touch_.readTouchPoints(points, 1);  // Too late!
```

**Result**: Callback always reported stale data (from previous poll), causing a 1-frame delay that made touches appear unresponsive.

### Additional Bug in `readTouchPoints()`

When no touch was detected, the function returned 0 but **didn't clear the internal state**:

```cpp
if (num_touches == 0 || num_touches > MAX_TOUCH_POINTS) {
    has_touch_ = false;  // Only cleared has_touch_
    return 0;             // BUT left last_touch_.valid = true!
}
```

**Result**: `getLastTouch()` would keep returning the last touch coordinates even after finger was lifted.

## Fixes Applied

### Fix 1: Read Touch Data BEFORE Checking It

```cpp
void PicoDCCDisplay::touchCallback(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!instance_) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    // FIX: Read fresh touch data FIRST
    TouchPoint points[1];
    uint8_t num_touches = instance_->touch_.readTouchPoints(points, 1);
    
    // THEN report current state to LVGL
    if (num_touches > 0 && points[0].valid) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = (points[0].x * 320) / 4096;  // Scale 12-bit to 320px
        data->point.y = (points[0].y * 240) / 4096;  // Scale 12-bit to 240px
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

### Fix 2: Properly Clear Touch State

```cpp
uint8_t num_touches = raw_data[0] & 0x0F;

// FIX: Clear BOTH flags when no touch
if (num_touches == 0) {
    has_touch_ = false;
    last_touch_.valid = false;  // <-- Added this
    return 0;
}

// Also clear on invalid touch count
if (num_touches > MAX_TOUCH_POINTS) {
    has_touch_ = false;
    last_touch_.valid = false;  // <-- Added this
    return 0;
}
```

### Fix 3: Coordinate Scaling

CST328 returns 12-bit coordinates (0-4095), but screen is 320×240:

```cpp
// Scale from 12-bit (0-4095) to screen pixels
data->point.x = (points[0].x * 320) / 4096;
data->point.y = (points[0].y * 240) / 4096;
```

### Fix 4: Debug Output (Temporary)

Added debug print for first touch to verify coordinate scaling:

```cpp
static bool first_touch_printed = false;
if (!first_touch_printed) {
    printf("Touch: Raw (%u,%u) -> Scaled (%d,%d) Event=%u\n",
           points[0].x, points[0].y, 
           data->point.x, data->point.y,
           points[0].event);
    first_touch_printed = true;
}
```

This will print once on first touch to confirm coordinates are reasonable.

## How LVGL Polling Works

### LVGL's Polling Architecture

LVGL doesn't use hardware interrupts for input devices. Instead:

1. **Main loop** calls `lv_timer_handler()` periodically
2. **LVGL checks** if input device needs polling (based on read period)
3. **LVGL calls** `touchCallback()` to get current touch state
4. **Callback reads** I2C data synchronously
5. **LVGL processes** touch as press/release/move events
6. **Buttons** receive click events if touch was within their bounds

### Timing Diagram

```
Main Loop (10Hz):
  lv_timer_handler()
    └─> touchCallback()  <-- Called here, not by GPIO interrupt
          └─> readTouchPoints()  <-- Reads I2C synchronously
                └─> I2C transaction (1-2ms)
                      └─> Returns touch data
          └─> Report to LVGL (PRESSED or RELEASED)
  
  lv_refr_now()  <-- Screen update
```

**Key Point**: The INT pin going LOW is just a signal that data is ready. We read it via polling when LVGL asks us to.

## Why Not Use GPIO Interrupts?

We could add GPIO interrupt handling, but it's **not necessary** because:

1. **LVGL polls fast enough** (10-30Hz is typical)
2. **Touch is inherently slow** (human finger ~20-50ms events)
3. **Interrupt complexity** adds little benefit
4. **Current approach is simpler** and works well

### If You Want Interrupts (Future Enhancement)

To add interrupt-driven touch:

```cpp
// In init():
gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN, 
                                   GPIO_IRQ_EDGE_FALL, 
                                   true, 
                                   &touch_isr);

// ISR:
void touch_isr(uint gpio, uint32_t events) {
    // Set flag to read touch in next callback
    touch_pending = true;
}

// In touchCallback():
if (!touch_pending) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
}
touch_pending = false;
// ... read touch data ...
```

But this is **optional** - polling works fine!

## Testing Checklist

With these fixes, touch should now work. Test:

- [ ] Touch any button - should highlight when pressed
- [ ] Release - button should trigger action (power toggle, etc.)
- [ ] Touch screen outside buttons - no action (correct)
- [ ] Check UART for debug message: `Touch: Raw (X,Y) -> Scaled (x,y) Event=N`
- [ ] Verify scaled coordinates are within screen bounds (0-319, 0-239)

### Expected Debug Output

First touch should print:
```
Touch: Raw (2048,2048) -> Scaled (160,120) Event=0
```

- **Raw X/Y**: Should be in range 0-4095 (12-bit)
- **Scaled X/Y**: Should be in range 0-319 (X) and 0-239 (Y)
- **Event**: 0=down, 1=up, 2=contact

### Coordinate Troubleshooting

**If touches are offset**:
- X/Y may be swapped: Swap the scaling lines
- May need rotation: Try different coordinate mappings
- May need inversion: Try `(4096 - raw_x)` instead of `raw_x`

**If coordinates look wrong**:
- Check raw values (should change smoothly when finger moves)
- Check scaled values (should be within screen bounds)
- Touch corners to verify mapping:
  - Top-left should be near (0,0)
  - Bottom-right should be near (319,239)

## Files Modified

### `lib/PicoDCCDisplay/pico_dcc_display.cpp`
- Fixed `touchCallback()` to read data before checking it
- Added coordinate scaling (12-bit to screen pixels)
- Added debug output for first touch

### `lib/PicoDCCDisplay/touch_driver.cpp`
- Fixed `readTouchPoints()` to clear `last_touch_.valid` when no touch
- Ensures released state is properly reported

## Next Steps

1. **Flash firmware** and test touch buttons
2. **Check debug output** for first touch coordinates
3. **If coordinates are wrong**: Report raw and scaled values
4. **If buttons don't respond**: Check button positions vs touch coordinates

The touch system should now work correctly with LVGL polling!
