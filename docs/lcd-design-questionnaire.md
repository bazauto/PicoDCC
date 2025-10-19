# LCD Integration Design Questionnaire

**Date**: October 19, 2025  
**Branch**: `feature/lcd-display`  
**Status**: ✅ COMPLETE - Ready for Implementation  
**Implementation Plan**: `lcd-implementation-plan.md`

---

## Overview

This document guides you through the critical design decisions needed before implementing the LCD integration. Answer each question, and we'll use your responses to generate the implementation plan.

---

## Section 1: Hardware Identification 🔌

### Q1.1: LCD Controller IC
**Question**: What is the exact driver IC in your Waveshare WAV-27579?

Common options for 2.7" Waveshare displays:
- [ ] **ILI9341** (very common, 240x320)
- [X] **ST7789** (common, 240x320 or 240x240)
- [ ] **ILI9488** (480x320 - higher resolution)
- [ ] **ST7735** (older, 128x160)
- [ ] **Other**: _____________

**How to find out**:
1. Check the product page or datasheet for WAV-27579
2. Look for markings on the LCD module's PCB (near ribbon cable connector)
3. Check Waveshare wiki: https://www.waveshare.com/wiki/
4. Search for "WAV-27579 datasheet" or "WAV-27579 driver"

**Your Answer**: 
```
This is using ST7789T3 at 240x320.

Features from website:
240 x 320 resolution, 262K colours, clear and colourful displaying effect
High touch screen transmittance, fast response and long lifetime
Embedded with ST7789T3 driver chip and CST328 capacitive touch control chip, using SPI and I2C communication respectively, minimizes required IO pins
13-pin connector and 18-pin FPC slot for connecting the LCD module to the host board
Onboard voltage translator, compatible with 3.3V / 5V operating voltage
Comes with online development resources and manual (examples for Raspberry Pi / ESP32S3 / Raspberry Pi Pico / Arduino)
```

---

### Q1.2: LCD Resolution
**Question**: What is the screen resolution?

- [X] **240x320** (most common for 2.7" ILI9341/ST7789)
- [ ] **480x320** (higher res, needs more RAM)
- [ ] **240x240** (square display)
- [ ] **Other**: _____ x _____

**Your Answer**:
```
240 x 320 resolution, 262K colours, clear and colourful displaying effect
```

---

### Q1.3: Touch Controller Type
**Question**: What type of touch input does your LCD have?

- [ ] **Resistive Touch** (4-wire or 5-wire, requires ADC or touch controller IC)
- [X] **Capacitive Touch** (I2C touch controller like FT6336 or similar)
- [ ] **No Touch** (display only)

**How to identify**:
- Resistive: Usually 4-5 extra wires for touch (T_CLK, T_CS, T_DIN, T_DO, T_IRQ)
- Capacitive: Usually I2C interface (SDA, SCL, INT, RST)
- Check Waveshare specifications

**Your Answer**:
```
CST328 using I2C
```

---

### Q1.4: Interface Type
**Question**: What communication interface does the LCD use?

- [X] **SPI (4-wire)** - Most common: SCK, MOSI, CS, DC, RST
- [ ] **SPI (3-wire)** - Rare: combines DC into data stream
- [ ] **Parallel (8-bit/16-bit)** - Fast but needs many GPIOs
- [ ] **I2C** - Uncommon for displays this size

**Your Answer**:
```
SPI (4-wire) for LCD
I2C for touch
```

---

## Section 2: PCB GPIO Mapping 📍

### Q2.1: Available GPIO Pins
**Question**: Which Raspberry Pi Pico GPIO pins are routed to your LCD connector on the PCB?

**Pico Pinout Reference**:
```
GP0-GP28 are available (GP23-GP25 sometimes used for special functions)
GP29 = ADC3 (can be used if not using ADC3, ADC3 is used internally on the PICO board so unavailable for our use)
```

**Current PicoDCC GPIO Usage** (from your main code):
```
MAIN TRACK:
- GP16 - Main track short circuit / overcurrent LED
- GP17 - Main track PWM output (DCC signal)
- GP18 - Main track enable (power on/off)
- GP26 (ADC0) - Main track current monitoring

PROGRAMMING TRACK:
- GP19 - Prog track short circuit / overcurrent LED
- GP20 - Prog track PWM output (DCC signal)
- GP21 - Prog track enable (power on/off)
- GP27 (ADC1) - Prog track current monitoring

COMMUNICATION:
- GP0 (UART0 TX) - DCC-EX protocol output
- GP1 (UART0 RX) - DCC-EX protocol input

DEBUG/SPECIAL:
- GP25 - Onboard LED (general error state)
- GP23, GP24 - SWD debug pins (used during programming)

AVAILABLE FOR LCD:
GP2, GP3, GP4, GP5, GP6, GP7, GP8, GP9, GP10, GP11, GP12, GP13, GP14, GP15

AVAILABLE BUT NOT SURFACED ON PCB:
GP22, GP28, GP29 (ADC3)
```

**LCD Pin Requirements** (typical for SPI + touch):
```
DISPLAY (6 pins minimum):
- SCK  (SPI Clock)
- MOSI (SPI Data Out)
- CS   (Chip Select)
- DC   (Data/Command)
- RST  (Reset)
- BL   (Backlight PWM - optional, can tie to 3.3V)

TOUCH (if resistive, +5 pins):
- T_CLK, T_CS, T_DIN, T_DO, T_IRQ

TOUCH (if capacitive I2C, +4 pins):
- SDA, SCL, INT, RST
```

**Your PCB Pin Assignments**:
```
Please list which GPIO pins are connected to which LCD signals on your PCB:

Display Pins (SPI0):
SCK  = GP6  (SPI0 SCK)
MISO = GP4  (SPI0 RX - not connected to LCD, display is write-only)
MOSI = GP7  (SPI0 TX)
CS   = GP5  (SPI0 CS)
DC   = GP2  (Data/Command select)
RST  = GP3  (Reset)
BL   = Tied to 3.3V (always on, no PWM dimming - intentional for simplicity)

Touch Pins (I2C0):
SDA = GP8  (I2C0 SDA)
SCL = GP9  (I2C0 SCL)
INT = GP10 (Touch interrupt - active when screen touched)
RST = GP11 (Touch controller reset)
```

**Your Answer**:
```
Updated above
```

---

### Q2.2: SPI Instance Selection
**Question**: Which Pico SPI hardware block should we use?

**Options**:
- [X] **SPI0** (GP16-19 or GP0-3 or GP4-7)
- [ ] **SPI1** (GP8-11 or GP12-15)
- [ ] **Bit-bang software SPI** (any GPIO, slower)

**Recommendation**: Use **SPI1 (GP8-11)** to avoid UART conflict on GP0-1

**Suggested Mapping** (if not yet decided):
```
SPI1 Pins (hardware SPI):
GP4  = SPI1 RX  (not used for display, but part of SPI1 block)
GP5  = SPI1 CS  → LCD CS
GP6  = SPI1 SCK → LCD SCK
GP7  = SPI1 TX  → LCD MOSI

Control Pins (any available GPIO):
GP2 = LCD DC   (Data/Command)
GP3 = LCD RST  (Reset)
N/C = LCD BL   (Backlight tied to 3.3v)

Touch Pins (I2C):
GP8  = SDA
GP9  = SCL
GP10 = INT
GP11 = RST
```

**Your Answer**:
```
Updated the mapping above
```

---

## Section 3: Software Architecture 🏗️

### Q3.1: Display Update Strategy
**Question**: How should the display update?

**Options**:
- [X] **Polling in main loop** (simple, ~50-100ms refresh)
  - Call `displayManager.update()` in `PicoDccController::loop()`
  - Updates every loop iteration, throttled internally
  
- [ ] **Timer-based updates** (consistent refresh rate)
  - Use Pico timer interrupt for 10Hz or 20Hz updates
  - Decoupled from main loop timing
  
- [ ] **Event-driven only** (updates on state changes)
  - Only redraw when track power changes, loco added, etc.
  - Most efficient, but may miss transient states

**Recommendation**: **Polling in main loop** (simplest integration)

**Your Preference**:
```
Accept recommendation of polling in main loop
```

---

### Q3.2: Framebuffer vs Direct Draw
**Question**: Should we use a framebuffer in RAM?

**Framebuffer Approach**:
- **Pros**: Smooth updates, no flicker, can do complex graphics
- **Cons**: Uses 38-76KB RAM for 240x320 display
- **Calculation**: 240×320×1 byte = 76KB (16-bit color) or 38KB (8-bit color)

**Direct Draw Approach**:
- **Pros**: Minimal RAM usage (~2KB for line buffers)
- **Cons**: Can flicker if updates are complex, slower for partial redraws
- **Method**: Send pixels directly to LCD during rendering

**Pico RAM Available**: 264KB total, ~200KB free after DCC code

**Recommendation**: 
- **Framebuffer** if using complex graphics, animations, or frequent updates
- **Direct Draw** if keeping UI simple (text, basic shapes, status icons)

**Your Preference**:
```
Framebuffer - 16-bit RGB565 color (76KB RAM)
Reasoning: LVGL works best with framebuffer architecture, provides smooth UI with no flicker, and Pico has sufficient RAM (264KB total, ~130KB free after framebuffer allocation)
```

---

### Q3.3: Graphics Library Selection
**Question**: Which graphics library should we use?

**Option A: TFT_eSPI** (Recommended for quick start)
- **Pros**: 
  - Very popular, well-documented
  - Excellent Pico support
  - Fast rendering, hardware SPI
  - Many examples available
- **Cons**: 
  - Arduino-style API (not pure Pico SDK)
  - Requires porting to Pico SDK patterns
- **GitHub**: https://github.com/Bodmer/TFT_eSPI
- **Best for**: Getting LCD working quickly, simple UI

**Option B: Waveshare Official Examples** (Hardware-specific)
- **Pros**: 
  - Exact code for your LCD model (if available)
  - Known to work with hardware
  - Easy to port to PicoDCC architecture
- **Cons**: 
  - May not have example for exact model
  - Code quality varies
  - Less community support
- **GitHub**: https://github.com/waveshare/Pico_code
- **Best for**: Hardware compatibility certainty

**Option C: LVGL (Light and Versatile Graphics Library)** (Professional UI)
- **Pros**: 
  - Professional-grade UI toolkit
  - Beautiful widgets (buttons, sliders, charts)
  - Touch input handling built-in
  - Theme support, animations
- **Cons**: 
  - Steeper learning curve
  - Larger footprint (~100-150KB flash)
  - More complex setup
- **Website**: https://lvgl.io/
- **Best for**: Polished, production-quality UI

**Option D: Raw Pico SDK + Custom** (Full control)
- **Pros**: 
  - Complete control, minimal dependencies
  - Smallest footprint
  - Tailored to exact needs
- **Cons**: 
  - More development time
  - Must implement everything from scratch
- **Best for**: Experienced embedded developers, custom requirements

**Your Preference**:
```
Option C
This seems to be the most maintained option based on their Git repo activity. And comes with everything I'd ever need.
```

---

### Q3.4: Touch Input Handling
**Question**: How should touch events be processed?

**Options**:
- [ ] **Polling in main loop** (check touch state every loop)
  - Simple, works with all touch types
  - 10-50ms response time
  
- [X] **Interrupt-driven** (GPIO interrupt on touch)
  - Fastest response (<1ms)
  - Requires IRQ pin from touch controller
  - More complex code
  
- [ ] **No touch yet** (display only for Phase 1)
  - Implement touch later once display working
  - Focus on diagnostic output first

**Recommendation**: **Polling for resistive, interrupt for capacitive** (or start with no touch)

**Your Preference**:
```
Interrupt driven
```

---

## Section 4: UI Design 🎨

### Q4.1: Screen Layout Priority
**Question**: What should the default screen show?

**Option A: Status Dashboard** (Recommended)
```
┌─────────────────────────┐
│ PicoDCC v1.0  [12:34:56]│
├─────────────────────────┤
│ MAIN: ON    [████▒▒▒▒▒] │
│ Current: 850mA / 5000mA │
│                          │
│ PROG: OFF   [▒▒▒▒▒▒▒▒▒] │
│ Current: 0mA / 250mA    │
├─────────────────────────┤
│ Locos Active: 3         │
│  #3  [→] 28 F0 F1       │
│  #42 [→] 56             │
│  #128[←] 12 F0 F2       │
├─────────────────────────┤
│ Last: Throttle #3 set   │
└─────────────────────────┘
```

**Option B: Diagnostic Focus**
```
┌─────────────────────────┐
│ System Messages         │
├─────────────────────────┤
│ ✓ 12:34:56 TRACK: Main  │
│   power enabled         │
│                          │
│ ⚠ 12:34:52 POWER: Main  │
│   current 4200mA        │
│                          │
│ ✓ 12:34:48 CONTROLLER:  │
│   Loco #3 registered    │
│                          │
│ ✓ 12:34:45 SYSTEM: Boot │
│   complete              │
│                          │
│        [CLEAR LOG]      │
└─────────────────────────┘
```

**Option C: Simplified** (minimal text)
```
┌─────────────────────────┐
│        PicoDCC          │
│                          │
│   MAIN: ON   850mA      │
│   PROG: OFF    0mA      │
│                          │
│   LOCOS: 3              │
│                          │
│   [PWR] [STOP] [RESET]  │
│                          │
└─────────────────────────┘
```

**Your Preference**:
```
For now lets go with Option B for diagnostics.
We can investigate the features of the LVGL later for a more feature rich UI.
```

---

### Q4.2: Touch Button Layout (if using touch)
**Question**: What buttons do you want on the main screen?

**Suggested Buttons** (pick 4-6 most important):
- [ ] **[MAIN PWR]** - Toggle main track power on/off
- [ ] **[PROG PWR]** - Toggle programming track power on/off
- [ ] **[EMERGENCY STOP]** - Send broadcast emergency stop
- [ ] **[RESET TRIPS]** - Clear overcurrent trip flags
- [ ] **[CALIBRATE]** - Enter calibration mode (show ADC values)
- [ ] **[LOCOS]** - View detailed locomotive list (separate screen)
- [ ] **[DIAGNOSTICS]** - View full error log (separate screen)
- [ ] **[SETTINGS]** - Configuration menu

**Your Selection** (rank top 4-6):
```
1. [MAIN PWR]
2. [PROG PWR]
3. [RESET TRIPS]
4. [CALIBRATE]
5. [DIAGNOSTICS]
6. [SETTINGS]
```

---

### Q4.3: Color Scheme
**Question**: What color scheme do you prefer?

**Option A: DCC-EX Style** (traditional)
- Background: Black
- Text: White/Green
- Critical alerts: Red
- Warnings: Yellow
- OK status: Green

**Option B: Modern Dark** (high contrast)
- Background: Dark gray (#1a1a1a)
- Text: Light gray (#e0e0e0)
- Accents: Blue (#4a90e2)
- Critical: Red (#e74c3c)
- Warnings: Orange (#f39c12)

**Option C: Light Mode** (easier to read in bright light)
- Background: White
- Text: Black
- Accents: Blue
- Critical: Red
- Warnings: Orange

**Option D: Custom**
- Background: ___________
- Text: ___________
- Accents: ___________

**Your Preference**:
```
Options A
```

---

## Section 5: Integration Points 🔗

### Q5.1: Diagnostic Logging Integration
**Question**: When should diagnostics appear on LCD?

**Current System**: `pico_diagnostic.h` has severity levels:
- DIAG_INFO (informational)
- DIAG_WARNING (warning conditions)
- DIAG_ERROR (error conditions)
- DIAG_CRITICAL (critical failures)

**Display Options**:
- [X] **All levels** - Show everything (verbose, may scroll quickly)
- [ ] **WARNING and above** - Skip routine info messages
- [ ] **ERROR and above** - Only show problems
- [ ] **CRITICAL only** - Only show critical failures

**Display Duration**:
- CRITICAL: Keep on screen until acknowledged (button press or timeout)
- ERROR: Display for ____ seconds (suggest 5s)
- WARNING: Display for ____ seconds (suggest 3s)
- INFO: Display for ____ seconds (suggest 1s)

**Your Preference**:
```
Show severity: All
Durations: Accept suggestions
```

---

### Q5.2: Configuration Storage Integration
**Question**: Should LCD show calibration workflow prompts?

**Scenario**: User sends `<D CAL START>` command
- **Option A**: Display shows: "CALIBRATION MODE: Enable prog track power, then send <D CAL ADC>"
- **Option B**: Display unchanged, rely on UART responses only
- **Option C**: Display shows live ADC readings during calibration

**Your Preference**:
```
Option C
```

---

### Q5.3: Power Control via Touch (if using touch)
**Question**: Should touch buttons directly control track power?

**Security Consideration**:
- Allowing direct power control from LCD bypasses DCC-EX protocol commands
- Could be useful for emergency situations (broken UART connection)
- Could be dangerous if accidental touch

**Options**:
- [ ] **Yes, direct control** - Touch button immediately toggles power
- [ ] **Confirmation required** - "Are you sure?" dialog before power change
- [ ] **No, display only** - Must use DCC-EX commands over UART
- [ ] **Emergency stop only** - Only allow emergency stop button, not power on

**Your Preference**:
```
Direct control but send the update back over DCC-EX to update computer
```

---

## Section 6: Development Phases 📅

### Q6.1: Implementation Order
**Question**: What order should we implement features?

**Suggested Phases**:

**Phase 1: Hardware Bring-Up** (Week 1)
- [ ] Initialize SPI and GPIO pins
- [ ] Test LCD controller communication (read ID register)
- [ ] Display test pattern (color bars, text)
- [ ] Backlight control (PWM if applicable)
- **Deliverable**: LCD shows "PicoDCC v1.0" and test pattern

**Phase 2: Basic UI** (Week 2)
- [ ] Implement screen layout structure
- [ ] Display track power status (ON/OFF)
- [ ] Display current draw (main + prog)
- [ ] Display system uptime/timestamp
- **Deliverable**: Real-time status display

**Phase 3: Diagnostic Integration** (Week 2-3)
- [ ] Connect `log_diagnostic()` to LCD
- [ ] Implement message scrolling/clearing
- [ ] Color-code by severity
- [ ] Test with generated error conditions
- **Deliverable**: Errors appear on LCD automatically

**Phase 4: Touch Input** (Week 3-4, optional)
- [ ] Initialize touch controller
- [ ] Calibrate touch (if resistive)
- [ ] Implement button detection
- [ ] Add button handlers (power, emergency stop, etc.)
- **Deliverable**: Touch buttons control track power

**Phase 5: Advanced UI** (Week 4+, optional)
- [ ] Locomotive list screen
- [ ] Configuration/calibration screen
- [ ] Multi-screen navigation
- [ ] Settings menu
- **Deliverable**: Full interactive UI

**Your Preferred Order**:
```
Order works for me
```

---

## Section 7: Testing Strategy 🧪

### Q7.1: Test Hardware
**Question**: Do you have the LCD module physically available now?

- [ ] **Yes, LCD is on hand** - Can test immediately
- [ ] **No, LCD on order** - Will arrive: __________
- [ ] **Unsure, need to locate**

**Your Answer**:
```
Yes, have the LCD on hand
```

---

### Q7.2: Test-Driven Development
**Question**: Should we write tests before implementation?

**Options**:
- [ ] **Mock LCD in tests** - Unit tests with simulated display (TEST_BUILD mode)
- [ ] **Hardware-only testing** - Skip mocks, test on real LCD only
- [ ] **Hybrid** - Basic mocks for compilation, detailed testing on hardware

**Recommendation**: **Hybrid approach**
- Create mock LCD driver for TEST_BUILD (prevents compile errors)
- Write integration tests that run on hardware
- Use simulator/emulator if available for UI layout testing

**Your Preference**:
```
Hybrid, ensure tests of any business logic but hardware manual testing will confirm UI elements and such
```

---

## Section 8: Performance & Power ⚡

### Q8.1: Display Refresh Rate
**Question**: How often should the display update?

**Typical Rates**:
- 1 Hz (1 second) - Very slow, power-saving, OK for static info
- 5 Hz (200ms) - Moderate, smooth current meter updates
- 10 Hz (100ms) - Fast, responsive UI
- 20+ Hz - Very fast, overkill for status display, uses more CPU

**Recommendation**: **5-10 Hz** for status screen, **1 Hz** when idle

**Your Preference**:
```
Accept recommendation
```

---

### Q8.2: Backlight Control
**Question**: How should the LCD backlight be controlled?

**Options**:
- [ ] **Always on full brightness** - Simple, tie BL to 3.3V
- [ ] **PWM dimming** - Adjustable brightness via GPIO
- [ ] **Auto-dim timeout** - Full brightness on activity, dim after 30s idle
- [ ] **Off when no power** - Turn off display when tracks are powered off

**Recommendation**: **PWM dimming with auto-dim** (user-friendly, power-saving)

**Your Preference**:
```
Always on, will tie BL to 3.3v
```

---

## Section 9: Dependencies & Libraries 📚

### Q9.1: External Library Management
**Question**: How should we include graphics library code?

**Options**:
- [ ] **Git submodule** - Add library as git submodule in `lib/external/`
- [ ] **Copy into repo** - Copy library source directly into PicoDCC repo
- [ ] **CMake FetchContent** - Download library during CMake configure
- [ ] **Manual download** - User downloads separately, points PICO_SDK_PATH

**Recommendation**: **Git submodule** (clean, version-controlled, easy updates)

**Your Preference**:
```
Git submodule
```

---

## Section 10: Documentation 📝

### Q10.1: User Documentation
**Question**: What documentation should we create alongside implementation?

**Suggested Documents**:
- [ ] **Hardware connection guide** (`docs/lcd-hardware-setup.md`)
  - Pinout, wiring diagram, photo of connections
  
- [ ] **LCD API reference** (`docs/lcd-api.md`)
  - PicoDCCDisplay class methods, screen structure
  
- [ ] **UI user guide** (`docs/lcd-user-guide.md`)
  - What each screen shows, button functions, troubleshooting
  
- [ ] **Integration guide** (`docs/lcd-integration.md`)
  - How other components use display (for future developers)

**Your Selection**:
```
All sounds good and useful
```

---

## Summary Checklist ✅

Before proceeding to implementation, ensure you've answered:

### Critical (Must Answer):
- [ ] Q1.1: LCD controller IC (ILI9341, ST7789, etc.)
- [ ] Q1.2: Screen resolution (240x320, etc.)
- [ ] Q2.1: GPIO pin assignments for LCD
- [ ] Q2.2: SPI instance selection (SPI0 or SPI1)
- [ ] Q3.3: Graphics library choice (TFT_eSPI, LVGL, etc.)

### Important (Recommended):
- [ ] Q1.3: Touch controller type
- [ ] Q3.1: Display update strategy
- [ ] Q3.2: Framebuffer vs direct draw
- [ ] Q4.1: Screen layout priority
- [ ] Q5.1: Diagnostic logging integration

### Optional (Can Defer):
- [ ] Q4.2: Touch button layout
- [ ] Q4.3: Color scheme
- [ ] Q6.1: Implementation phase order
- [ ] Q8.1: Display refresh rate
- [ ] Q8.2: Backlight control

---

## Next Steps

**After answering these questions**:
1. Review answers together
2. Generate implementation plan (`docs/lcd-implementation-plan.md`)
3. Create component structure (`lib/PicoDCCDisplay/`)
4. Write hardware abstraction layer (HAL)
5. Implement Phase 1 (hardware bring-up)

**How to proceed**:
- Fill in answers in this document
- Commit to `feature/lcd-display` branch
- Ask for next steps, or start with hardware testing if LCD is available

---

## Quick Start Path (If Unsure)

**If you're uncertain about some answers, here are safe defaults**:

```yaml
LCD Controller: ILI9341 (most common 2.7")
Resolution: 240x320
Touch: Resistive 4-wire
Interface: SPI 4-wire
SPI Instance: SPI1 (GP8-14)
GPIO Mapping: Use suggested mapping (GP8-19)
Graphics Library: TFT_eSPI
Update Strategy: Polling in main loop
Framebuffer: No (direct draw for simplicity)
Screen Layout: Status Dashboard (Option A)
Color Scheme: DCC-EX Style (black background, green/white text)
Diagnostic Level: WARNING and above
Touch Control: Confirmation required for power changes
Phases: 1→2→3 (skip touch initially)
Refresh Rate: 5 Hz
Backlight: PWM with auto-dim
```

Use these defaults to get started quickly, then refine later based on testing!
