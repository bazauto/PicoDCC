# LCD Touch Input Debug Status - Phase 4

**Date**: 2025-10-19  
**Status**: Touch interrupt handler working, callback being called, but touch data read issues

## Problem Overview
Implementing CST328 capacitive touch controller for interactive LCD display. Touch hardware works (INT pin pulses confirmed with oscilloscope), but touch data isn't being read correctly.

## Hardware Configuration
- **Touch Controller**: CST328 (I2C address 0x1A)
- **GPIO Pins**:
  - GP8: I2C0 SDA
  - GP9: I2C0 SCL
  - GP10: INT (interrupt, active-low)
  - GP11: RST (reset)
- **INT Pulse Characteristics**: <100µs duration (measured with oscilloscope)
- **Display**: 320×240 landscape orientation
- **Touch Coordinates**: 12-bit (0-4095) raw values

## Root Cause Discovery
**Critical timing issue**: INT pulses are <100µs, but LVGL polls at 10Hz (100ms intervals). Polling misses 99.9% of interrupt events. Solution: GPIO interrupt handler.

## Current Implementation Status

### ✅ COMPLETED
1. **GPIO Interrupt Handler** (`lib/PicoDCCDisplay/touch_driver.cpp`):
   ```cpp
   void TouchDriver::touchInterruptHandler(unsigned int gpio, uint32_t events) {
       if (gpio == TOUCH_INT_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
           touch_interrupt_pending_ = true;  // Volatile flag
           uart_puts(uart0, "[ISR] Touch interrupt! Flag set\n");
       }
   }
   ```
   - ISR catches falling edge on GP10
   - Sets `volatile bool touch_interrupt_pending_` flag
   - Registered via `gpio_set_irq_enabled_with_callback()`

2. **LVGL Tick Integration** (`lib/PicoDCCDisplay/pico_dcc_display.cpp`):
   ```cpp
   void PicoDCCDisplay::update() {
       // Tell LVGL how much time has passed (CRITICAL!)
       static uint32_t last_tick_time = 0;
       uint32_t now = time_us_32() / 1000;
       uint32_t elapsed_ms = now - last_tick_time;
       if (elapsed_ms > 0) {
           lv_tick_inc(elapsed_ms);
           last_tick_time = now;
       }
       lv_timer_handler();  // Now processes timers correctly
   }
   ```
   - Without `lv_tick_inc()`, LVGL timers never fire
   - This was the reason touch callback wasn't being called

3. **Touch Callback Registration**:
   ```cpp
   lv_indev_drv_init(&indev_drv_);
   indev_drv_.type = LV_INDEV_TYPE_POINTER;
   indev_drv_.read_cb = touchCallback;
   lv_indev_t* indev = lv_indev_drv_register(&indev_drv_);
   ```
   - Timer created: 10ms period, not paused ✓
   - Callback confirmed being called by LVGL ✓

4. **I2C Communication Working**:
   - CST328 responds to I2C reads ✓
   - Data is being received ✓

### 🔧 CURRENT ISSUES (19 Oct 2025 - Active Debug)

#### Issue 1: CST328 Data Format Mismatch - ✅ RESOLVED!
**Original Problem**:
```
[TOUCH_DRV] I2C read OK, num_touches=6
[CALLBACK] readTouchPoints returned 0 touches
```

**Root Cause Discovered**:
The CST328 doesn't reliably report touch count in byte 0. Analysis of raw data revealed:
```
Touch press:   Raw[0-6]: 06 07 11 3B 37 01 AB
Touch release: Raw[0-6]: 00 07 11 3B 34 01 AB
```
- Byte 0 = `0x06` (garbage, not touch count)
- Bytes 1-6 contain valid coordinate data: X=1809 (0x711), Y=2871 (0xB37)
- Scaled coordinates: (141, 168) - perfectly reasonable for 320×240 screen!

**Solution Implemented**:
Changed from trusting byte 0 to validating actual coordinate data:
```cpp
// OLD: uint8_t num_touches = raw_data[0] & 0x0F;  // Unreliable!

// NEW: Check if coordinates are valid (non-zero, in range 0-4095)
uint16_t x_test = ((raw_data[1] & 0x0F) << 8) | raw_data[2];
uint16_t y_test = ((raw_data[3] & 0x0F) << 8) | raw_data[4];
if (x_test > 0 && x_test < 4096 && y_test > 0 && y_test < 4096) {
    num_touches = 1;  // Valid touch detected
}
```

**Bug Fix #2**: Event type extraction was wrong
```cpp
// OLD: uint8_t event = (raw_data[offset] >> 4) & 0x03;  // Wrong bits!
// NEW: uint8_t event = (raw_data[offset] >> 6) & 0x03;  // Upper 2 bits
```

#### Issue 2: Touch Coordinates Axis Swap - ✅ SOLUTION IMPLEMENTED!

**Final Analysis from Testing**:

Multiple touches to MAIN PWR button showed:
```
Touch 1a: Raw(1554,1041) -> Swap(60,121)   ← Y=121 perfect for buttons!
Touch 1b: Raw(1554,1052) -> Swap(61,121)   ← Y=121 consistent!
Touch 2a: Raw(1553,2867) -> Swap(167,121)  ← Y=121 again!
Touch 2b: Raw(1553,2871) -> Swap(168,121)  ← Y=121 stable!
```

**Key Discovery**:
- **Swapped Y coordinate = 121** is consistently in button range (Y=100-140)! ✅
- **Swapped X coordinate varies 60-168** covering different screen areas ✅
- This matches 90° rotation: Touch X→Display Y, Touch Y→Display X

**Button Positions**:
- MAIN PWR: X=5-75, Y=100-140
- PROG PWR: X=85-155, Y=100-140
- RESET TRIPS: X=165-235, Y=100-140
- CALIBRATE: X=245-315, Y=100-140

**Expected Touch Mapping** (with swap):
- Touch MAIN PWR → Raw X≈1550, Raw Y≈200-500 → Swap(11-30, 121)
- Touch PROG PWR → Raw X≈1550, Raw Y≈700-1000 → Swap(40-60, 121)
- Touch RESET TRIPS → Raw X≈1550, Raw Y≈2700-3000 → Swap(160-175, 121)
- Touch CALIBRATE → Raw X≈1550, Raw Y≈3500-4000 → Swap(205-234, 121)

**Solution Applied**:
```cpp
// Simple axis swap (90° rotation)
int scaled_x = (points[0].x * 320) / 4096;
int scaled_y = (points[0].y * 240) / 4096;
data->point.x = scaled_y;  // Touch Y becomes Display X
data->point.y = scaled_x;  // Touch X becomes Display Y
```

**Debug Output Cleaned Up**:
- Removed: ISR messages, DRIVER messages, TOUCH_DRV verbose output, UPDATE messages, CALLBACK spam
- Kept: Button click events, final touch coordinates only

**Next Test**: Should see MAIN PWR button activate when touched!

## Key Files Modified

### `lib/PicoDCCDisplay/touch_driver.h`
- Added: `static volatile bool touch_interrupt_pending_`
- Added: `static TouchDriver* instance_` (for ISR access)
- Added: `static void touchInterruptHandler(unsigned int gpio, uint32_t events)`
- Added: `bool hasPendingTouch()`

### `lib/PicoDCCDisplay/touch_driver.cpp`
- Static members initialized
- `touchInterruptHandler()`: ISR sets flag on GPIO_IRQ_EDGE_FALL
- `hasPendingTouch()`: Returns interrupt flag state (with debug output when true)
- `init()`: Calls `enableInterrupt(true)` at end
- `enableInterrupt()`: Uses `gpio_set_irq_enabled_with_callback()` to register ISR
- `readTouchPoints()`: Clears flag at start, added debug output for I2C reads
- **Includes**: Added `#include <cstdio>` and `#include <hardware/uart.h>`

### `lib/PicoDCCDisplay/pico_dcc_display.cpp`
- `update()`: Added `lv_tick_inc(elapsed_ms)` **CRITICAL FIX**
- `touchCallback()`: 
  - Changed counter to print every 10 calls (more frequent)
  - Added debug output for `readTouchPoints()` return value
  - Checks `hasPendingTouch()` before I2C read
- `initLVGL()`: Added timer status debug (period, paused state)

## Debug Output Sequence (Current)

**Normal operation** (no touch):
```
[UPDATE] lv_timer_handler() being called (every ~1 second)
[CALLBACK] touchCallback() called by LVGL (every 10 seconds)
```

**When touch occurs**:
```
[ISR] Touch interrupt! Flag set                    ← GPIO ISR fires
[DRIVER] hasPendingTouch() = TRUE!                  ← Next LVGL poll
[CALLBACK] Flag is set! Reading touch data...       ← Callback proceeds
[TOUCH_DRV] Flag cleared, reading I2C...           ← Start I2C read
[TOUCH_DRV] I2C read OK/FAILED, num_touches=X      ← I2C result (NEED TO TEST)
[CALLBACK] readTouchPoints returned X touches       ← Callback result (NEED TO TEST)
Touch: Raw (X,Y) -> Scaled (x,y) Event=N           ← If num_touches > 0 (NOT SEEN YET)
```

## Possible Root Causes for Missing Touch Data

1. **I2C Communication Failure**:
   - Check: Does `[TOUCH_DRV] I2C read FAILED!` appear?
   - Possible: Bus contention, timing issues, wrong register address
   - CST328 status register: `CST328_REG_STATUS = 0x00`

2. **CST328 Reporting Zero Touches**:
   - Check: Does `[TOUCH_DRV] I2C read OK, num_touches=0` appear?
   - Possible: Touch data cleared between INT and I2C read
   - Possible: Wrong data format interpretation
   - CST328 spec: Byte 0 lower 4 bits = number of touch points

3. **Timing Issue**:
   - INT pulse <100µs, but I2C read takes longer
   - Touch data might be cleared by controller before read completes
   - May need to read immediately in ISR context (risky) or accept data loss

4. **Register Address Wrong**:
   - Verify CST328_REG_STATUS is correct (should be 0x00 for most touch controllers)
   - May need datasheet confirmation

5. **Data Parsing Issue**:
   - Even if I2C succeeds and num_touches > 0, coordinate extraction might fail
   - Raw data format: 6 bytes per touch point starting at byte 1

## UART Output Configuration
**Current setup**: Using `uart_puts(uart0, ...)` for hardware compatibility
- All printf/iostream changed to uart_puts
- Formatted output uses snprintf + uart_puts

## Next Steps (Priority Order)

### IMMEDIATE (Hardware Debug Session - IN PROGRESS)
1. ✅ **Touch detection working** (coordinates being read)
2. ✅ **Axis swap confirmed** (Y stable at 120-141, perfect for buttons)
3. 🔧 **Debounce added** - filters out jittery/unstable X coordinates
   - Ignores touches within 50ms and 100 raw units of previous touch
   - Should prevent random button activations
4. **Test debounced touch** - should now consistently hit correct buttons

### DEBOUNCE LOGIC DETAILS
**Problem Observed**:
```
Touch 1: Display(18,141)   ← Should hit MAIN PWR (X=5-75) ✓
Touch 2: Display(92,120)   ← Should hit PROG PWR (X=85-155) ✓
Touch 3: Display(153,120)  ← Should hit RESET TRIPS (X=165-235) ✓
Touch 4: Display(2,120)    ← X=2 is wrong (too far left)
```

Y coordinates perfect, but X jumps around during same physical touch.

**Solution Implemented**:
```cpp
// Track last touch coordinates and time
static int last_raw_x = 0;
static int last_raw_y = 0;
static uint32_t last_touch_time = 0;

// Calculate deltas
int delta_x = abs((int)points[0].x - last_raw_x);
int delta_y = abs((int)points[0].y - last_raw_y);
uint32_t delta_time = now - last_touch_time;

// Ignore if too similar (within 100 units and 50ms)
if (delta_time < 50 && delta_x < 100 && delta_y < 100) {
    // Same touch continuing - keep previous position
    return;
}
```

This filters out rapid jittery readings while allowing:
- New touches after 50ms
- Touch movement >100 units (~2.4% of range)
- Intentional drags/swipes

### ONCE DEBOUNCE WORKS
5. **Verify all 4 buttons respond correctly**
6. **Check button visual feedback** (press animation)
7. **Test power toggle functionality**
8. **Remove touch coordinate debug output** (keep button events only)

## Expected UART Output (Next Test)

**When touch occurs** (should now work correctly):
```
[ISR] Touch interrupt! Flag set                    
[DRIVER] hasPendingTouch() = TRUE!                  
[CALLBACK] Flag is set! Reading touch data...       
[TOUCH_DRV] Flag cleared, reading I2C...           
[TOUCH_DRV] I2C read OK, reported_count=6, x_test=1809, y_test=2871, num_touches=1
[TOUCH_DRV] Raw[0-6]: 06 07 11 3B 37 01 AB        
[TOUCH_DRV] Raw[7-12]: 00 00 00 56 00 00          
[TOUCH_DRV] Point 0: X=1809 Y=2871 Event=0 ID=11 (offset=1)
[TOUCH_DRV] Returning 1 touch points               
[CALLBACK] readTouchPoints returned 1 touches       
Touch: Raw (1809,2871) -> Scaled (141,168) Event=0 ← Should work now!
```

**When touch releases**:
```
[TOUCH_DRV] I2C read OK, reported_count=0, x_test=1809, y_test=2852, num_touches=0
[CALLBACK] readTouchPoints returned 0 touches
```
Note: Stale coordinates remain in buffer, but num_touches=0 because coordinates haven't changed significantly (release detection).

## Critical Implementation Notes

### Volatile Keyword
The `volatile` keyword on `touch_interrupt_pending_` is **essential**:
- Prevents compiler from caching the flag value
- ISR writes, main loop reads (cross-context communication)
- Without volatile, main loop might never see ISR's changes

### LVGL Tick Source
`lv_tick_inc()` is **mandatory** for LVGL timers to work:
- Tells LVGL how much time has passed
- Without it, timers never expire (no callbacks fire)
- Must be called every time `lv_timer_handler()` is called
- Uses system tick (time_us_32() / 1000 for milliseconds)

### GPIO Interrupt Priority
The ISR catches <100µs pulses that polling (100ms) misses:
- ISR executes in microseconds (fast enough to catch pulse)
- Sets flag for polling domain to check
- Standard embedded pattern: ISR → volatile flag → polling check

## Code Architecture

```
Hardware INT Pin (GP10)
    ↓ <100µs pulse
GPIO ISR (touchInterruptHandler)
    ↓ sets
volatile bool touch_interrupt_pending_
    ↓ checked by
LVGL touchCallback() [every 10ms via timer]
    ↓ if true
readTouchPoints() [I2C read]
    ↓ clears flag, reads data
Parse coordinates & event
    ↓
Report to LVGL (PRESSED/RELEASED)
    ↓
LVGL processes button clicks
```

## Known Working Components
- ✅ LCD hardware (320×240 ST7789T3)
- ✅ LVGL graphics (diagnostic screen, labels, buttons)
- ✅ CST328 I2C communication during init
- ✅ GPIO interrupt handler (catches short pulses)
- ✅ LVGL timer system (after adding lv_tick_inc)
- ✅ Touch callback registration and invocation
- ✅ Interrupt flag setting and checking

## Known Issues / Unknowns
- ❓ I2C read success during touch event (TESTING NOW)
- ❓ CST328 touch point count (TESTING NOW)
- ❓ Coordinate data parsing (blocked on above)
- ❓ Actual callback frequency vs expected (may be cosmetic counter issue)

## Reference Documentation
- Main architecture: `docs/architecture.md`
- LCD implementation plan: `docs/lcd-implementation-plan.md`
- Touch callback fix notes: `docs/lcd-touch-callback-fix.md`
- CST328 I2C address correction: 0x15 → 0x1A (confirmed working)

## Hardware Test Setup
- Development board: Raspberry Pi Pico 2
- LCD: Waveshare WAV-27579 (320×240, ST7789T3 controller)
- Touch: CST328 capacitive touch controller
- Debug tool: Oscilloscope (used to measure INT pulse width)
- Communication: UART0 for debug output

## Build Command
```powershell
cd e:\Development\PicoDCC\build
cmake --build .
```

Firmware output: `build/src/PicoDCC.uf2`
