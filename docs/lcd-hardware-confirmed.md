# LCD Hardware Configuration - CONFIRMED

**Date**: October 19, 2025  
**Status**: ✅ Hardware Verified, Ready for Software Design

---

## ✅ CONFIRMED HARDWARE CONFIGURATION

### Display Module
```yaml
Model: Waveshare WAV-27579
Controller: ST7789T3
Resolution: 240x320 pixels (262K colors)
Interface: SPI (4-wire, write-only)
Touch: CST328 (Capacitive, I2C)
Voltage: 3.3V
Backlight: Tied to 3.3V (always on, no PWM control - intentional)
```

---

## ✅ VERIFIED GPIO ASSIGNMENTS

### DCC Track Outputs (Confirmed from Code & User)
```yaml
MAIN TRACK:
  GP16 = Short Circuit / Overcurrent LED (visual indicator)
  GP17 = PWM Output (DCC signal to H-bridge)
  GP18 = Enable (track power on/off control)
  GP26 = Current Monitoring (ADC0)

PROGRAMMING TRACK:
  GP19 = Short Circuit / Overcurrent LED (visual indicator)
  GP20 = PWM Output (DCC signal to H-bridge)
  GP21 = Enable (track power on/off control)
  GP27 = Current Monitoring (ADC1)

Note: Each track uses 4 GPIOs total (LED, PWM, Enable, ADC).
      H-bridge (BTS7960) receives single PWM signal and generates differential output.
      LEDs on GP16/GP19 provide visual feedback for overcurrent conditions.
```

### Communication
```yaml
UART0:
  GP0 = TX (DCC-EX commands out)
  GP1 = RX (DCC-EX commands in)
```

### LCD Display (SPI0)
```yaml
SPI0 Hardware Block:
  GP4 = SPI0 RX (MISO - not connected to LCD, display is write-only)
  GP5 = SPI0 CS
  GP6 = SPI0 SCK
  GP7 = SPI0 TX (MOSI)

Control Pins:
  GP2 = DC (Data/Command select)
  GP3 = RST (Reset)
  
Backlight:
  BL = Tied to 3.3V (always full brightness)
  Note: No PWM dimming - user confirmed this is acceptable for simplicity
```

### LCD Touch (I2C0)
```yaml
I2C0 Hardware Block:
  GP8 = I2C0 SDA (CST328 data line)
  GP9 = I2C0 SCL (CST328 clock line)

Control Pins:
  GP10 = INT (Interrupt - goes low when touched)
  GP11 = RST (Touch controller reset)
```

### Debug/System
```yaml
  GP25 = Onboard LED (timing error indicator)
  GP23, GP24 = SWD (debug/programming interface)
```

---

## 📊 GPIO Summary Table

| GPIO | Function | Component | Notes |
|------|----------|-----------|-------|
| GP0 | UART0 TX | DCC-EX Protocol | Commands out |
| GP1 | UART0 RX | DCC-EX Protocol | Commands in |
| GP2 | DC | LCD Display | Data/Command |
| GP3 | RST | LCD Display | Reset |
| GP4 | SPI0 RX | LCD Display | MISO (unused) |
| GP5 | SPI0 CS | LCD Display | Chip Select |
| GP6 | SPI0 SCK | LCD Display | SPI Clock |
| GP7 | SPI0 TX | LCD Display | MOSI |
| GP8 | I2C0 SDA | LCD Touch | Touch data |
| GP9 | I2C0 SCL | LCD Touch | Touch clock |
| GP10 | GPIO | LCD Touch | INT (interrupt) |
| GP11 | GPIO | LCD Touch | RST (reset) |
| GP12 | **Available** | - | Free for expansion |
| GP13 | **Available** | - | Free for expansion |
| GP14 | **Available** | - | Free for expansion |
| GP15 | **Available** | - | PWM7B capable |
| GP16 | GPIO | Main Track | Short/Overcurrent LED |
| GP17 | PIO | Main Track | PWM Output (DCC signal) |
| GP18 | GPIO | Main Track | Enable (power control) |
| GP19 | GPIO | Prog Track | Short/Overcurrent LED |
| GP20 | PIO | Prog Track | PWM Output (DCC signal) |
| GP21 | GPIO | Prog Track | Enable (power control) |
| GP22 | - | - | Not surfaced on PCB |
| GP23 | SWD | Debug | SWDCLK |
| GP24 | SWD | Debug | SWDIO |
| GP25 | GPIO | System | Onboard LED (timing errors) |
| GP26 | ADC0 | Main Track | Current sense |
| GP27 | ADC1 | Prog Track | Current sense |
| GP28 | - | - | Not surfaced on PCB |

**Available for Future Use**: GP12, GP13, GP14, GP15 (4 pins)

---

## ✅ NO CONFLICTS DETECTED

### Validated:
- ✅ LCD SPI0 (GP4-7) does NOT conflict with UART0 (GP0-1)
- ✅ LCD I2C0 (GP8-9) does NOT conflict with any DCC pins
- ✅ LCD control pins (GP2, GP3, GP10, GP11) are in available range
- ✅ DCC signals (GP17, GP20) are outside LCD GPIO range
- ✅ ADC pins (GP26, GP27) are dedicated to current sensing

### Clarifications:
- ✅ Each track uses 4 GPIOs (LED indicator, PWM signal, Enable control, ADC current sense)
- ✅ GP16/GP19 are overcurrent LED outputs (visual feedback to user)
- ✅ GP17/GP20 are PWM outputs to H-bridge (DCC signal generation)
- ✅ GP18/GP21 are enable pins (software track power control)
- ✅ GP25 is general error LED (built-in Pico LED for system errors)
- ✅ Backlight always on (no dimming) - user confirmed acceptable
- ✅ GP4 (MISO) unused by display - this is normal for write-only LCDs

---

## 🎯 HARDWARE DESIGN DECISIONS - FINALIZED

### Critical Decisions (Locked In):
- [x] **Q1.1**: LCD Controller = ST7789T3 ✅
- [x] **Q1.2**: Resolution = 240x320 ✅
- [x] **Q1.3**: Touch = CST328 (Capacitive I2C) ✅
- [x] **Q1.4**: Interface = SPI (4-wire) ✅
- [x] **Q2.1**: GPIO pins assigned and verified ✅
- [x] **Q2.2**: SPI Instance = SPI0 (GP4-7) ✅

### Hardware Constraints Documented:
- [x] Backlight: No dimming (tied to 3.3V)
- [x] MISO: Not connected (display is write-only)
- [x] Touch: Capacitive with INT + RST
- [x] DCC: Single GPIO per track (H-bridge differential)

---

## 📝 READY FOR SOFTWARE DESIGN

**Next Phase**: User to complete questionnaire Sections 3-10:
- Section 3: Software Architecture (library choice, update strategy)
- Section 4: UI Design (screen layout, colors, buttons)
- Section 5: Integration Points (diagnostics, calibration, touch controls)
- Section 6: Development Phases (implementation order)
- Section 7: Testing Strategy
- Section 8: Performance & Power
- Section 9: Dependencies
- Section 10: Documentation

**Hardware questions are complete** - no blockers for implementation planning!

---

## 🚀 CLEARED FOR TAKEOFF

All hardware aspects are verified and documented. The LCD pin assignments are:
- **Electrically sound** (correct SPI0 and I2C0 pins)
- **Conflict-free** (no overlap with DCC, UART, or ADC)
- **Well-documented** (clear function assignments)
- **Future-proof** (4 spare GPIOs available: GP12-15)

**User can now proceed to software architecture decisions with confidence!**
