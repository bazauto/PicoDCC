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

### 🔧 CURRENT ISSUES

#### Issue 1: Callback Frequency
**Observed**: `[CALLBACK] touchCallback() called by LVGL` prints every 10 seconds  
**Expected**: Every 1 second (counter prints every 10 calls at 10ms intervals)  
**Analysis**: May indicate actual callback rate is lower than expected, or counter math is off.

#### Issue 2: No Touch Data Read
**Observed**: 
- `[ISR] Touch interrupt! Flag set` ✓ (confirms interrupt works)
- `[DRIVER] hasPendingTouch() = TRUE!` ✓ (confirms flag check works)
- `[CALLBACK] Flag is set! Reading touch data...` ✓ (confirms callback proceeds)
- **MISSING**: `Touch: Raw (X,Y) -> Scaled (x,y) Event=N` (no touch coordinates)
- **MISSING**: `[CALLBACK] readTouchPoints returned X touches`

**Current Debug Code** (just added):
```cpp
// In touchCallback():
uint8_t num_touches = instance_->touch_.readTouchPoints(points, 1);
char debug_buf[64];
snprintf(debug_buf, sizeof(debug_buf), "[CALLBACK] readTouchPoints returned %u touches\n", num_touches);
uart_puts(uart0, debug_buf);

// In readTouchPoints():
uart_puts(uart0, "[TOUCH_DRV] Flag cleared, reading I2C...\n");
if (!readMultipleRegisters(CST328_REG_STATUS, raw_data, 32)) {
    uart_puts(uart0, "[TOUCH_DRV] I2C read FAILED!\n");
    return 0;
}
uint8_t num_touches = raw_data[0] & 0x0F;
snprintf(buf, sizeof(buf), "[TOUCH_DRV] I2C read OK, num_touches=%u\n", num_touches);
uart_puts(uart0, buf);
```

**Next Test**: Flash latest firmware and check UART output for:
1. Does I2C read succeed or fail?
2. If successful, what is `num_touches` value?
3. If 0, why is CST328 reporting no touches despite INT firing?

#### Issue 3: Buttons Don't Respond
**Expected**: Will work once touch coordinates are correctly read.

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

### IMMEDIATE (Hardware Debug Session)
1. **Flash latest firmware** (includes all new debug output)
2. **Touch screen and capture UART output**:
   - Does `[TOUCH_DRV] I2C read FAILED!` appear?
   - Does `[TOUCH_DRV] I2C read OK, num_touches=X` appear?
   - What is the value of `X`?
3. **Analyze based on results**:
   - **If I2C fails**: Check bus, address, timing
   - **If num_touches=0**: Check timing between INT and read, verify register address
   - **If num_touches>0 but no "Touch: Raw"**: Check coordinate parsing logic

### ONCE TOUCH DATA READS WORK
4. **Verify coordinate scaling** (12-bit → 320×240 pixels)
5. **Test button press detection** (should work automatically)
6. **Remove/reduce debug output** (too much UART spam)
7. **Verify touch release detection**

### OPTIMIZATION (Phase 4 Completion)
8. **Test all 4 buttons**: MAIN PWR, PROG PWR, RESET TRIPS, CALIBRATE
9. **Verify power toggle functionality**
10. **Clean up debug code** (reduce to initialization messages only)

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
