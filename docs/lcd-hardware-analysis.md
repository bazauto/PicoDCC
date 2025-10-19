# LCD Hardware Configuration - Analysis & Issues

**Date**: October 19, 2025  
**Reviewed By**: AI Agent  
**Status**: ⚠️ Issues Found - Requires Clarification

---

## ✅ What You Provided (Confirmed Good)

### Display Specifications
```yaml
Controller: ST7789T3
Resolution: 240x320 (262K colors)
Interface: SPI (4-wire)
Touch: CST328 (Capacitive, I2C)
Voltage: 3.3V/5V compatible (using 3.3V)
```
✅ **Clear and complete** - ST7789 is well-supported

---

### GPIO Pin Assignments (As Stated)
```yaml
DISPLAY (SPI0):
  GP4  = SPI0 RX (MISO - not typically used for display)
  GP5  = SPI0 CS
  GP6  = SPI0 SCK
  GP7  = SPI0 TX (MOSI)
  GP2  = DC (Data/Command)
  GP3  = RST (Reset)
  BL   = Tied to 3.3V (always on)

TOUCH (I2C0):
  GP8  = I2C0 SDA
  GP9  = I2C0 SCL
  GP10 = INT (interrupt, active when touched)
  GP11 = RST (reset for touch controller)
```

✅ **Hardware SPI0 usage confirmed** - Good choice, avoids UART conflict  
✅ **Hardware I2C0 usage confirmed** - Correct pins for I2C0  
✅ **Separate INT and RST for touch** - Proper implementation

---

## ⚠️ ISSUES FOUND

### Issue 1: **Main Track GPIO Conflict** 🔴 **CRITICAL**

**You stated**:
```
MAIN TRACK:
- GP16, GP17 - Main track DCC output (PIO)
- GP18 - Main track enable
- GP26 (ADC0) - Main track current sense
```

**But `pico_dcc.cpp` shows**:
```cpp
#define TRACK_MAIN_SIGNAL_PIN 17      // Only GP17 (not GP16+GP17)
#define TRACK_MAIN_POWER_CTRL_PIN 18  // ✅ Matches
#define TRACK_MAIN_POWER_ADC_NUM 0    // ✅ GP26 (ADC0)
```

**Questions**:
1. Is GP16 actually used for main track, or just GP17?
2. If using **H-bridge driver** (like your BTS7960), you might have:
   - GP16 = RPWM (Right PWM)
   - GP17 = LPWM (Left PWM)
   - GP18 = Enable

**Action Required**: 
- Clarify if main track uses 1 or 2 GPIO pins for DCC signal
- If 2 pins (H-bridge with separate RPWM/LPWM), update copilot-instructions.md
- Check your actual hardware: Does main track H-bridge need both pins?

---

### Issue 2: **Programming Track GPIO Mismatch** 🟡 **MODERATE**

**You stated**:
```
PROGRAMMING TRACK:
- GP19, GP20 - Prog track DCC output (PIO, 20-bit preamble)
- GP21 - Prog track enable
- GP27 (ADC1) - Prog track current sense
```

**But `pico_dcc.cpp` shows**:
```cpp
#define TRACK_PROG_SIGNAL_PIN 20      // Only GP20 (not GP19+GP20)
#define TRACK_PROG_POWER_CTRL_PIN 21  // ✅ Matches
#define TRACK_PROG_POWER_ADC_NUM 1    // ✅ GP27 (ADC1)
```

**Same Question**: Is GP19 used or not?
- If prog track also uses H-bridge with 2 pins → Update code
- If only 1 pin (GP20) → Update your questionnaire answer

---

### Issue 3: **SPI0 RX (MISO) Pin Usage** 🟢 **MINOR - INFORMATIONAL**

**You assigned**:
```
GP4 = SPI0 RX (MISO)
```

**Note**: ST7789 displays are **write-only** (no MISO needed)
- The display doesn't send data back to the Pico
- GP4 can be used for SPI0 RX by hardware, but won't be connected to LCD
- This pin is effectively **unused** unless you connect MISO for diagnostics

**Options**:
1. **Leave GP4 unconnected** - Safest, clearest intent
2. **Use GP4 for something else** - Since LCD doesn't use it
3. **Connect to LCD MISO** - For optional ID reading (rare feature)

**Recommendation**: Document that GP4 is part of SPI0 block but not connected to LCD

---

### Issue 4: **Backlight Tied to 3.3V** 🟡 **MODERATE - FUNCTIONAL**

**You stated**:
```
BL = tied to 3.3V (always on)
```

**Implications**:
- ✅ **Simple**: No GPIO needed, no PWM code
- ⚠️ **No dimming**: Can't adjust brightness
- ⚠️ **No power saving**: Backlight always consuming power (~50-100mA)
- ⚠️ **No auto-dim**: Can't implement "dim after idle" feature

**Is this intentional?**
- If **yes** (simple design) → All good, document this limitation
- If **no** (you want dimming) → Need to free up a PWM-capable GPIO

**If you want backlight control later**:
```yaml
Option A: Use GP15 (PWM7B) - Currently available
Option B: Use GP22 (PWM3A) - Not surfaced on PCB, requires rework
```

---

### Issue 5: **I2C Touch Pins - RST vs INT Naming** 🟢 **MINOR - CLARITY**

**You listed**:
```
Touch Pins (I2C):
T_CLK (or SDA) = GP8   ← Should be "SDA (I2C0 SDA)"
T_CS  (or SCL) = GP9   ← Should be "SCL (I2C0 SCL)"
T_DIN (or INT) = GP10  ← Should be "INT (Touch Interrupt)"
T_DO  (or RST) = GP11  ← Should be "RST (Touch Reset)"
```

**Confusion**: Using SPI pin names (T_CLK, T_CS, etc.) for I2C pins

**Correct Naming for Capacitive Touch (CST328)**:
```yaml
GP8  = SDA (I2C0 SDA) - Touch data line
GP9  = SCL (I2C0 SCL) - Touch clock line
GP10 = INT (Touch interrupt) - Goes low when screen is touched
GP11 = RST (Touch reset) - Active low reset for CST328
```

**Action**: Update questionnaire to use I2C terminology, not SPI names

---

## 📊 Corrected GPIO Summary

### Definite Assignments (Confirmed from Code):
```yaml
MAIN TRACK:
  GP17 = DCC Signal (confirmed in pico_dcc.cpp)
  GP18 = Track Enable
  GP26 = Current Sense (ADC0)
  GP16 = ??? (you claimed this, but not in code) ⚠️

PROGRAMMING TRACK:
  GP20 = DCC Signal (confirmed in pico_dcc.cpp)
  GP21 = Track Enable
  GP27 = Current Sense (ADC1)
  GP19 = ??? (you claimed this, but not in code) ⚠️

COMMUNICATION:
  GP0 = UART0 TX (DCC-EX commands out)
  GP1 = UART0 RX (DCC-EX commands in)

LCD DISPLAY (SPI0):
  GP2  = DC (Data/Command)
  GP3  = RST (Reset)
  GP4  = SPI0 RX (unused by LCD, part of hardware block)
  GP5  = SPI0 CS
  GP6  = SPI0 SCK
  GP7  = SPI0 TX (MOSI)

LCD TOUCH (I2C0):
  GP8  = I2C0 SDA
  GP9  = I2C0 SCL
  GP10 = Touch INT
  GP11 = Touch RST

OTHER:
  GP25 = Onboard LED (debug/timing errors)
  GP23, GP24 = Debug (SWD - used during programming/debug)

AVAILABLE:
  GP12, GP13, GP14, GP15 (4 pins free)
  GP22, GP28 (not surfaced on PCB)
```

---

## 🔍 Questions to Resolve

### Q1: H-Bridge Configuration (Main Track)
**Does your main track use a dual-pin H-bridge like BTS7960?**

From your schematic symbol reference (BTS7960 Motor Driver):
- Pin 1: RPWM (Right PWM)
- Pin 3: R_EN (Right Enable)
- Likely also: LPWM (Left PWM), L_EN (Left Enable)

**If YES** (dual PWM pins):
```yaml
GP16 = Main Track RPWM (or LPWM)
GP17 = Main Track LPWM (or RPWM)
GP18 = Main Track Enable (R_EN + L_EN tied together)
```
**Action**: Update `pico_dcc.cpp` to use both GP16 and GP17

**If NO** (single-pin signal):
```yaml
GP16 = Unused/Available
GP17 = Main Track DCC Signal (current code is correct)
GP18 = Main Track Enable
```
**Action**: Update questionnaire to remove GP16 from main track

---

### Q2: H-Bridge Configuration (Programming Track)
**Does your prog track use the same dual-pin approach?**

**If YES**:
```yaml
GP19 = Prog Track RPWM
GP20 = Prog Track LPWM
GP21 = Prog Track Enable
```
**Action**: Update `pico_dcc.cpp` to use both GP19 and GP20

**If NO**:
```yaml
GP19 = Unused/Available
GP20 = Prog Track DCC Signal (current code is correct)
GP21 = Prog Track Enable
```
**Action**: Update questionnaire to remove GP19

---

### Q3: Backlight Control
**Do you want PWM backlight dimming capability?**

**Option 1: Keep it simple** (current design)
- BL tied to 3.3V
- No code needed
- No dimming/power saving

**Option 2: Add PWM control**
- Free up GP15 (PWM7B)
- Connect BL to GP15
- Requires PCB rework if already manufactured

**Your preference?**

---

## 🛠️ Recommended Next Steps

### 1. Clarify Main/Prog Track Pin Usage (High Priority)
**Check your PCB schematic**:
- Count the wires going to each H-bridge
- Are you using differential drive (2 pins per track)?
- Or single-ended (1 pin per track)?

**Update questionnaire with correct pin count**

---

### 2. Update Pin Naming for Touch (Low Priority)
**In questionnaire Section 2.1**, change:
```diff
Touch Pins (I2C):
- T_CLK (or SDA) = GP8
- T_CS  (or SCL) = GP9
- T_DIN (or INT) = GP10
- T_DO  (or RST) = GP11
+ SDA (I2C0 SDA) = GP8
+ SCL (I2C0 SCL) = GP9
+ INT (Touch Interrupt) = GP10
+ RST (Touch Reset) = GP11
```

---

### 3. Document Backlight Limitation (Low Priority)
**Add note in questionnaire**:
```
BL = Tied to 3.3V (always full brightness, no dimming control)
Note: To add PWM dimming later, would need to use GP15 and rework PCB
```

---

## 📋 Validation Checklist

Before proceeding, please confirm:

- [ ] **Main track pins**: Uses GP16+GP17 (H-bridge) OR just GP17 (single)?
- [ ] **Prog track pins**: Uses GP19+GP20 (H-bridge) OR just GP20 (single)?
- [ ] **GP4 (MISO)**: Understood it's unused by display (OK to leave unconnected)
- [ ] **Backlight**: Confirmed no dimming needed (tied to 3.3V is acceptable)
- [ ] **Touch naming**: Will update to I2C terminology (SDA/SCL not T_CLK/T_CS)

---

## 🎯 Impact on LCD Implementation

### If H-Bridge Uses 2 Pins Per Track:
- ✅ **No impact on LCD** - GP16 and GP19 are not in LCD GPIO range
- ✅ **LCD pins are safe** - No conflicts with dual-pin DCC signals
- ⚠️ **Code update needed** - `pico_dcc.cpp` and PicoDCCTrack need to handle 2-pin output

### If H-Bridge Uses 1 Pin Per Track:
- ✅ **No impact on LCD** - Current code matches hardware
- ✅ **GP16 and GP19 become available** - Could use for future features
- ✅ **No code changes needed** - Current implementation is correct

**Either way, LCD implementation can proceed once clarified.**

---

## 🟢 What's Already Good

These aspects are **confirmed correct**:
1. ✅ ST7789T3 controller (well-supported, good library availability)
2. ✅ SPI0 for display (avoids UART conflict on GP0/GP1)
3. ✅ I2C0 for touch (correct pins GP8/GP9)
4. ✅ Separate INT and RST for touch (proper CST328 implementation)
5. ✅ 240x320 resolution (standard, lots of examples)
6. ✅ 3.3V operation (native Pico voltage)
7. ✅ Available GPIOs (GP12-15) for future expansion

**You can proceed with software design while we clarify the H-bridge question!**

---

## Next: Software Architecture Decisions

Once you clarify the pin usage questions, you can confidently fill out:
- Section 3: Software Architecture (library choice, update strategy)
- Section 4: UI Design (screen layout, colors)
- Section 5: Integration Points (diagnostic logging, touch controls)

**The hardware questions don't block software planning!**

---

**Please respond with clarification on Q1 and Q2 (H-bridge pin usage), then we'll update the questionnaire and proceed to implementation planning.**
