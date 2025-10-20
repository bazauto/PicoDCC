# LCD Integration Documentation

**Hardware**: Waveshare WAV-27579  
**Display**: ST7789T3 (320×240 landscape)  
**Touch**: CST328 capacitive I²C  
**Status**: Phase 4 Complete (Touch Input Working)  
**Branch**: `feature/lcd-display`

---

## Hardware Configuration

### Display Controller (ST7789T3)
- **Interface**: SPI0 (10 MHz)
- **Resolution**: 320×240 pixels (landscape)
- **Color Depth**: 16-bit RGB565 (262K colors)
- **Rotation**: 270° (MADCTL 0x60)
- **Pins**:
  - GP4: SPI0 SCK (Clock)
  - GP5: SPI0 MOSI (Data)
  - GP6: SPI0 CS (Chip Select)
  - GP2: DC (Data/Command)
  - GP3: RST (Reset)
  - Backlight: Tied to 3.3V (always on)

### Touch Controller (CST328)
- **Interface**: I²C0 (400 kHz, address 0x1A)
- **Touch Points**: Up to 5 simultaneous (using 1)
- **Interrupt**: Falling edge on GP10
- **Coordinate Mapping**: 
  - Display X ← Raw Y (inverted): `320 - (raw_y * 320 / 300)`
  - Display Y ← Raw X (normal): `raw_x * 240 / 220`
- **Pins**:
  - GP8: I²C0 SDA (Data)
  - GP9: I²C0 SCL (Clock)
  - GP10: INT (Interrupt, active low)
  - GP11: RST (Reset)

### Memory Usage
- **Display Buffer**: 150 KB (320 × 240 × 2 bytes)
- **LVGL Heap**: ~20-30 KB
- **Total LCD RAM**: ~165 KB
- **Available RAM**: ~95 KB remaining (264 KB total on RP2350)

---

## Software Architecture

### Graphics Library (LVGL 8.3)
- **Location**: `lib/external/lvgl/` (git submodule)
- **Configuration**: `lib/PicoDCCDisplay/lv_conf.h`
- **Update Strategy**: Polled in main loop via `display.loop()`
- **Update Rate**: 10 Hz (every 100ms)

### Component Structure
```
lib/PicoDCCDisplay/
├── pico_dcc_display.{cpp,h}      # Main display controller
├── lcd_driver.{cpp,h}            # ST7789T3 SPI driver
├── touch_driver.{cpp,h}          # CST328 I²C driver
├── lv_conf.h                     # LVGL configuration
└── ui/
    ├── ui_screens.{cpp,h}        # Main status screen
    └── ui_diagnostic_screen.{cpp,h}  # Diagnostic detail screen
```

### Integration Pattern
```cpp
// In main() - keep minimal
#ifndef TEST_BUILD
PicoDCCDisplay display;
display.init();
display.runBootSequence();
#endif

// Main loop
while (true) {
    pico_controller.dccexLoop();
    #ifndef TEST_BUILD
    display.loop(&pico_controller);  // Self-contained updates
    #endif
}
```

**Key Design Principle**: PicoDCCDisplay is self-contained. Main application only calls `init()`, `runBootSequence()`, and `loop()`. All timing, data gathering, and updates are managed internally.

---

## Implementation Status

### ✅ Phase 1: Hardware Bring-Up (Complete)
- ST7789T3 SPI driver working
- CST328 I²C driver working
- LVGL integrated and configured
- Test patterns validated

### ✅ Phase 2: Basic UI (Complete)
- Main status screen with live data
- Landscape layout (320×240)
- Track power indicators
- Current monitoring
- Trip status display

### ✅ Phase 3: Track Data Integration (Complete)
- Real-time data from PicoDCCController
- Main track: power, current, trips
- Programming track: power, current, trips
- Color-coded status indicators
- Automatic updates at 10 Hz

### ✅ Phase 4: Touch Input (Complete)
- Touch interrupt handling
- Coordinate mapping (axis swap + inversion)
- Button functionality:
  - **MAIN PWR**: Toggle main track power
  - **PROG PWR**: Toggle programming track power
  - **RESET TRIPS**: Power cycle both tracks
  - **CALIBRATE**: Reserved for CV programming (future)
- Stable, responsive touch input
- No calibration needed (hardcoded mapping works universally)

### ⏳ Phase 5: Settings Screen (Future - Requires Non-Volatile Storage)
**Blocked by**: Need PicoConfigStorage implementation for persistent settings

Planned features:
- **Settings Menu**: Access via touch button on main screen
- **ADC Calibration**: Adjust current sensor scaling factors
  - Live ADC value display during calibration
  - Store calibration constants in flash
  - Test with known loads (e.g., resistive dummy)
- **Display Settings**: Brightness control (requires PWM on backlight)
- **System Info**: Firmware version, uptime, memory stats

**Why Blocked**: 
- ADC calibration values must persist across reboots
- Cannot implement useful settings screen without storage backend
- Current `PicoConfigStorage` stub exists but is not fully implemented
- Need to define flash sector allocation and API for config read/write

**Next Steps for Phase 5**:
1. Complete `lib/PicoConfigStorage/` implementation
2. Define calibration data structure
3. Add settings screen UI
4. Implement ADC calibration workflow
5. Store/restore calibration on boot

---

## Current State

### Working Features
- Display shows real-time track status
- Touch buttons control track power
- Trip detection and reset working
- No UART pollution (clean DCC-EX protocol)
- Stable, production-ready operation

### Known Limitations
- No ADC calibration (using hardcoded scaling)
- No settings persistence (config storage not implemented)
- Backlight always on (tied to 3.3V, no PWM control)
- Single screen (no multi-screen navigation yet)

### Future Enhancements (Post-Config-Storage)
- Settings screen with ADC calibration
- Multi-screen navigation (swipe/button)
- Brightness control (requires hardware mod for PWM backlight)
- Locomotive roster screen (when loco management expanded)
- System diagnostics screen (memory, uptime, errors)

---

## Technical Notes

### Touch Coordinate Mapping
The CST328 touch panel is rotated 90° relative to the display. Mapping formula:
```cpp
// Raw touch: 12-bit (0-4095), actual range: X ~10-210, Y ~30-290
int scaled_x = 320 - ((raw_y * 320) / 300);  // Inverted
int scaled_y = (raw_x * 240) / 220;           // Normal
// Clamp to 0-319, 0-239
```

This simple linear mapping works reliably without complex calibration because the CST328 driver (with corrected 16-bit register addressing) provides clean, linear coordinates.

### Display Driver Details
- **Register Access**: 16-bit addresses (0xD000 for touch data)
- **Touch Data Format**: 5 bytes per point
  - Byte 0: [ID (4 bits)][State (4 bits)] - state == 6 means pressed
  - Byte 1: X high 8 bits
  - Byte 2: Y high 8 bits
  - Byte 3: [X low 4 bits][Y low 4 bits]
  - Byte 4: Pressure/weight
- **Interrupt**: Falling edge on GP10 triggers LVGL read callback

### LVGL Configuration Highlights
```c
// lv_conf.h key settings
#define LV_COLOR_DEPTH 16              // RGB565
#define LV_MEM_SIZE (30 * 1024U)       // 30KB heap
#define LV_DISP_DEF_REFR_PERIOD 100    // 10 Hz refresh
#define LV_USE_LOG 0                   // No debug logs (UART clean)
#define LV_TICK_CUSTOM 1               // Use time_us_32() for timing
```

### GPIO Pin Assignments
| Function      | GPIO | Notes                          |
|---------------|------|--------------------------------|
| LCD SCK       | GP4  | SPI0 Clock                     |
| LCD MOSI      | GP5  | SPI0 Data Out                  |
| LCD CS        | GP6  | SPI0 Chip Select               |
| LCD DC        | GP2  | Data/Command Select            |
| LCD RST       | GP3  | Reset (active low)             |
| Touch SDA     | GP8  | I²C0 Data                      |
| Touch SCL     | GP9  | I²C0 Clock                     |
| Touch INT     | GP10 | Interrupt (falling edge)       |
| Touch RST     | GP11 | Reset (active low)             |

---

## Development Guidelines

### Adding New UI Elements
1. Create UI files in `lib/PicoDCCDisplay/ui/`
2. Keep LVGL code wrapped in `#ifndef TEST_BUILD`
3. Update `PicoDCCDisplay::loop()` for periodic updates
4. Never add logic directly to `main()` - keep it in component methods

### Testing Strategy
- Use test pattern mode for display verification
- Touch calibration not needed (hardcoded mapping works)
- Test all buttons for responsiveness
- Monitor UART for any debug pollution (should be silent)

### Memory Management
- LVGL allocates from its own heap (30 KB)
- Display buffer is static (150 KB)
- Monitor remaining RAM if adding large UI elements
- Use `lv_mem_monitor()` for LVGL heap diagnostics

---

## References

### Hardware Documentation
- **Display**: ST7789T3 Datasheet (240×320 TFT LCD Controller)
- **Touch**: CST328 Datasheet (Capacitive Touch Controller)
- **Board**: Waveshare WAV-27579 Product Page

### Software Documentation
- **LVGL**: [docs.lvgl.io](https://docs.lvgl.io/8.3/) (version 8.3.x)
- **Pico SDK**: [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)

### Project Files
- Main implementation: `lib/PicoDCCDisplay/`
- Architecture doc: `docs/architecture.md` (PicoDCCDisplay component section)
- This document: `docs/lcd-integration.md`

---

**Last Updated**: 2025-10-19  
**Next Milestone**: Phase 5 (Settings Screen) - pending PicoConfigStorage implementation
