# PicoDCC GPIO Pin Assignments - Complete Reference

**Date**: October 19, 2025  
**Hardware**: Raspberry Pi Pico (RP2040)  
**Status**: ✅ VERIFIED AND LOCKED

---

## Complete GPIO Allocation Table

| GPIO | Direction | Function | Component | Hardware | Notes |
|------|-----------|----------|-----------|----------|-------|
| **GP0** | Output | UART0 TX | DCC-EX Protocol | UART0 | Commands out to host |
| **GP1** | Input | UART0 RX | DCC-EX Protocol | UART0 | Commands in from host |
| **GP2** | Output | DC (Data/Command) | LCD Display | GPIO | Display control signal |
| **GP3** | Output | RST (Reset) | LCD Display | GPIO | Display reset |
| **GP4** | Input | SPI0 RX (MISO) | LCD Display | SPI0 | Not connected (display is write-only) |
| **GP5** | Output | SPI0 CS | LCD Display | SPI0 | Chip select (active low) |
| **GP6** | Output | SPI0 SCK | LCD Display | SPI0 | SPI clock |
| **GP7** | Output | SPI0 TX (MOSI) | LCD Display | SPI0 | SPI data out |
| **GP8** | Bidir | I2C0 SDA | LCD Touch | I2C0 | Touch data line (CST328) |
| **GP9** | Output | I2C0 SCL | LCD Touch | I2C0 | Touch clock line (CST328) |
| **GP10** | Input | INT (Interrupt) | LCD Touch | GPIO | Touch interrupt (active low) |
| **GP11** | Output | RST (Reset) | LCD Touch | GPIO | Touch controller reset |
| **GP12** | - | **AVAILABLE** | - | - | Free for expansion |
| **GP13** | - | **AVAILABLE** | - | - | Free for expansion |
| **GP14** | - | **AVAILABLE** | - | - | Free for expansion |
| **GP15** | - | **AVAILABLE** | - | - | Free (PWM7B capable) |
| **GP16** | Output | Short/Overcurrent LED | Main Track | GPIO | Visual indicator for main track fault |
| **GP17** | Output | PWM Output | Main Track | PIO | DCC signal to H-bridge |
| **GP18** | Output | Enable | Main Track | GPIO | Track power on/off control |
| **GP19** | Output | Short/Overcurrent LED | Prog Track | GPIO | Visual indicator for prog track fault |
| **GP20** | Output | PWM Output | Prog Track | PIO | DCC signal to H-bridge (20-bit preamble) |
| **GP21** | Output | Enable | Prog Track | GPIO | Track power on/off control |
| **GP22** | - | Not Surfaced | - | - | Not available on PCB connector |
| **GP23** | - | SWDCLK | Debug | SWD | Debug/programming interface |
| **GP24** | - | SWDIO | Debug | SWD | Debug/programming interface |
| **GP25** | Output | Error LED | System | GPIO | Onboard LED (general error state) |
| **GP26** | Input | ADC0 | Main Track | ADC | Main track current monitoring |
| **GP27** | Input | ADC1 | Prog Track | ADC | Prog track current monitoring |
| **GP28** | - | Not Surfaced | - | - | Not available on PCB connector |
| **GP29** | - | ADC3 (Optional) | - | ADC | Not surfaced on PCB |

---

## Pin Usage by Subsystem

### Main Track DCC Output (4 pins)
```yaml
GP16: Overcurrent LED (output) - Lights when main track trips overcurrent
GP17: PWM Signal (PIO output) - DCC waveform to BTS7960 H-bridge
GP18: Enable (GPIO output) - Controls track power (active high enables)
GP26: Current Sense (ADC0 input) - Analog current monitoring
```

### Programming Track DCC Output (4 pins)
```yaml
GP19: Overcurrent LED (output) - Lights when prog track trips overcurrent
GP20: PWM Signal (PIO output) - DCC waveform with 20-bit preamble
GP21: Enable (GPIO output) - Controls track power (active high enables)
GP27: Current Sense (ADC1 input) - Analog current monitoring for ACK detection
```

### DCC-EX Communication (2 pins)
```yaml
GP0: UART TX (output) - Sends responses back to DCC-EX client
GP1: UART RX (input) - Receives commands from DCC-EX client
```

### LCD Display - ST7789T3 (7 pins)
```yaml
GP2:  DC (output) - Data/Command select (high=data, low=command)
GP3:  RST (output) - Display reset (active low)
GP4:  MISO (input, unused) - SPI0 RX not connected (display is write-only)
GP5:  CS (output) - Chip select (active low)
GP6:  SCK (output) - SPI clock
GP7:  MOSI (output) - SPI data to display
BL:   Tied to 3.3V - Backlight always on (no GPIO control)
```

### LCD Touch - CST328 (4 pins)
```yaml
GP8:  SDA (bidirectional) - I2C data line
GP9:  SCL (output) - I2C clock line
GP10: INT (input) - Touch interrupt, goes low when screen touched
GP11: RST (output) - Touch controller reset (active low)
```

### System/Debug (3 pins)
```yaml
GP25: Onboard LED (output) - General system error indicator
GP23: SWDCLK (debug) - SWD debug clock (used during programming)
GP24: SWDIO (debug) - SWD debug data (used during programming)
```

### Available for Future Use (4 pins)
```yaml
GP12: Free GPIO
GP13: Free GPIO
GP14: Free GPIO
GP15: Free GPIO (PWM7B capable - could add backlight dimming if needed)
```

---

## Hardware Interface Summary

### SPI0 Block (LCD Display)
- **SCK**: GP6 (SPI0 SCK)
- **MOSI**: GP7 (SPI0 TX)
- **MISO**: GP4 (SPI0 RX) - not connected
- **CS**: GP5 (SPI0 CSn)
- **Control**: GP2 (DC), GP3 (RST)

### I2C0 Block (LCD Touch)
- **SDA**: GP8 (I2C0 SDA)
- **SCL**: GP9 (I2C0 SCL)
- **Control**: GP10 (INT), GP11 (RST)

### UART0 Block (DCC-EX Protocol)
- **TX**: GP0 (UART0 TX)
- **RX**: GP1 (UART0 RX)

### ADC Channels
- **ADC0**: GP26 (Main track current, 0-3.3V analog)
- **ADC1**: GP27 (Prog track current, 0-3.3V analog)
- **ADC2**: GP28 (Not surfaced on PCB)
- **ADC3**: GP29 (Not surfaced on PCB)

### PIO State Machines
- **PIO SM**: GP17 (Main track DCC waveform generation)
- **PIO SM**: GP20 (Prog track DCC waveform with 20-bit preamble)

---

## Conflict Analysis - NO CONFLICTS DETECTED ✅

### UART vs SPI/I2C:
- ✅ UART uses GP0-1, SPI uses GP4-7, I2C uses GP8-9 → **No overlap**

### DCC Tracks vs LCD:
- ✅ Main track uses GP16-18 + GP26 → **No overlap with LCD (GP2-11)**
- ✅ Prog track uses GP19-21 + GP27 → **No overlap with LCD (GP2-11)**

### ADC vs Digital:
- ✅ ADC pins (GP26, GP27) are dedicated, not shared

### Debug vs Functional:
- ✅ SWD (GP23-24) only active during programming, not at runtime
- ✅ GP25 (onboard LED) available for general error indication

---

## Power Budget Estimate

| Component | Pins | Current Draw | Notes |
|-----------|------|--------------|-------|
| Main Track LED | GP16 | ~10mA | Through current-limiting resistor |
| Prog Track LED | GP19 | ~10mA | Through current-limiting resistor |
| Error LED | GP25 | ~2mA | Onboard LED, low current |
| LCD Backlight | - | ~50-100mA | Tied to 3.3V rail (not GPIO) |
| LCD Display | GP2-7 | ~5mA | SPI signals, low current |
| LCD Touch | GP8-11 | ~3mA | I2C signals, very low current |
| **Total GPIO** | - | **~30mA** | Well within Pico limits |

**Note**: Main current loads (H-bridge, LCD backlight) are powered separately, not through GPIO pins.

---

## Code Reference

### Pin Definitions (from `src/pico_dcc.cpp`):
```cpp
// Main Track
#define TRACK_MAIN_SHORT_LED 16       // Overcurrent indicator
#define TRACK_MAIN_SIGNAL_PIN 17      // PWM to H-bridge
#define TRACK_MAIN_POWER_CTRL_PIN 18  // Enable control
#define TRACK_MAIN_POWER_ADC_NUM 0    // ADC0 on GP26

// Programming Track
#define TRACK_PROG_SHORT_LED 19       // Overcurrent indicator
#define TRACK_PROG_SIGNAL_PIN 20      // PWM to H-bridge
#define TRACK_PROG_POWER_CTRL_PIN 21  // Enable control
#define TRACK_PROG_POWER_ADC_NUM 1    // ADC1 on GP27

// System
#define TIMING_ERROR_LED_PIN 25       // General error state
```

### LCD Pin Definitions (to be added in `lib/PicoDCCDisplay/`):
```cpp
// Display (SPI0)
#define LCD_PIN_DC   2    // Data/Command
#define LCD_PIN_RST  3    // Reset
#define LCD_PIN_MISO 4    // Not connected (write-only)
#define LCD_PIN_CS   5    // Chip Select
#define LCD_PIN_SCK  6    // SPI Clock
#define LCD_PIN_MOSI 7    // SPI Data

// Touch (I2C0)
#define LCD_PIN_SDA  8    // I2C Data
#define LCD_PIN_SCL  9    // I2C Clock
#define LCD_PIN_INT  10   // Touch Interrupt
#define LCD_PIN_TRST 11   // Touch Reset
```

---

## Design Rationale

### Why SPI0 for Display?
- ✅ Avoids UART0 conflict (GP0-1 used for DCC-EX protocol)
- ✅ Hardware SPI is faster than bit-banging
- ✅ Pins GP4-7 are grouped together for clean PCB routing

### Why I2C0 for Touch?
- ✅ Standard interface for CST328 capacitive touch controller
- ✅ Only needs 2 signal wires (SDA, SCL) plus 2 control (INT, RST)
- ✅ Hardware I2C reduces CPU overhead

### Why Separate INT and RST for Touch?
- ✅ INT allows interrupt-driven touch detection (faster response than polling)
- ✅ RST allows software reset of touch controller if it hangs

### Why No Backlight PWM?
- ✅ Simplifies design (one less GPIO needed)
- ✅ Backlight always visible (no accidental dimming)
- ✅ Can add later if needed (GP15 available with PCB rework)

---

## Future Expansion Options

With **4 spare GPIOs** (GP12-15), you could add:

- **GP12**: Second I2C device (e.g., RTC, EEPROM)
- **GP13**: SPI device (e.g., SD card for logging)
- **GP14**: GPIO button or sensor input
- **GP15**: PWM backlight control (requires PCB rework to disconnect BL from 3.3V)

---

**This document is the authoritative GPIO reference for PicoDCC hardware.**  
**All code and documentation should reference this for pin assignments.**
