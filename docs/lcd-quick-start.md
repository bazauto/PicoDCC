# LCD Integration Quick Start Guide

**Date**: October 19, 2025  
**Branch**: `feature/lcd-display`  
**Full Plan**: `lcd-implementation-plan.md`

---

## 📋 What You Have Now

✅ **Hardware Confirmed**:
- Waveshare WAV-27579 LCD (ST7789T3, 240x320, RGB565)
- CST328 capacitive touch (I2C0 with interrupt)
- GPIO pins assigned and verified (no conflicts)
- Backlight tied to 3.3V (always on)

✅ **Design Decisions Made**:
- LVGL graphics library (professional UI)
- 16-bit framebuffer (76KB RAM)
- Polling updates in main loop (10Hz)
- Interrupt-driven touch (GP10 INT pin)
- Diagnostic focus UI with scrolling message log
- Black background, DCC-EX color scheme (green/white/yellow/red)
- Direct touch control with DCC-EX echo-back

✅ **Documentation Complete**:
- Design questionnaire fully answered
- GPIO pinout reference created
- Implementation plan generated (this!)

---

## 🚀 Implementation Phases

### **Phase 1: Hardware Bring-Up** (Week 1, ~4 hours)
**Goal**: Get LCD showing test pattern

**Tasks**:
1. Add LVGL as git submodule
2. Create `lib/PicoDCCDisplay/` structure
3. Implement ST7789T3 SPI driver
4. Display 8-color test pattern
5. Configure CMake for both build modes

**Milestone**: LCD shows color bars on boot ✅

---

### **Phase 2: LVGL Integration** (Week 2, ~5 hours)
**Goal**: Create diagnostic screen UI

**Tasks**:
1. Connect LVGL to LCD hardware
2. Allocate 76KB framebuffer
3. Create scrolling message list screen
4. Add severity-based color coding
5. Implement "CLEAR LOG" button

**Milestone**: LCD shows formatted diagnostic messages ✅

---

### **Phase 3: Diagnostic Integration** (Week 2-3, ~3 hours)
**Goal**: Connect real diagnostics to LCD

**Tasks**:
1. Add callback to `pico_diagnostic.h`
2. Register display callback in `main()`
3. Test with real error conditions
4. Add optional filtering (WARNING+)

**Milestone**: All system logs appear on LCD automatically ✅

---

### **Phase 4: Touch Input** (Week 3-4, ~4 hours)
**Goal**: Add interactive touch buttons

**Tasks**:
1. Implement CST328 I2C touch driver
2. Register touch with LVGL
3. Add MAIN PWR / PROG PWR buttons
4. Connect to track power control
5. Add visual feedback (green=ON)

**Milestone**: Touch buttons control track power ✅

---

### **Phase 5: Advanced UI** (Week 4+, ~6 hours, OPTIONAL)
**Goal**: Add calibration screen and navigation

**Tasks**:
1. Create calibration screen
2. Implement screen switching
3. Show live ADC values during calibration
4. Add settings/loco screens (future)

**Milestone**: Full multi-screen UI with calibration ✅

---

## 📦 Quick Reference

### File Structure
```
lib/PicoDCCDisplay/
├── CMakeLists.txt              # Build config
├── lv_conf.h                   # LVGL settings
├── pico_dcc_display.h/cpp      # Main API
├── lcd_driver.h/cpp            # ST7789T3 driver
├── touch_driver.h/cpp          # CST328 driver
├── ui/
│   ├── ui_screens.h/cpp        # Screen manager
│   ├── ui_diagnostic_screen.*  # Message log
│   └── ui_calibration_screen.* # Calibration UI
└── mocks/                      # TEST_BUILD stubs
```

### GPIO Pinout
```
Display (SPI0):  GP2(DC), GP3(RST), GP4-7(SPI)
Touch (I2C0):    GP8(SDA), GP9(SCL), GP10(INT), GP11(RST)
Backlight:       Tied to 3.3V (no control)
Available:       GP12-15 (4 spare pins)
```

### Key Integration Points
```cpp
// In src/pico_dcc.cpp
#include "pico_dcc_display.h"
#include "pico_diagnostic.h"

PicoDCCDisplay display;

void diagnostic_callback(DiagnosticLevel level, 
                         const char* component, 
                         const char* message) {
    display.logMessage(level, message);
}

int main() {
    display.init();
    diagnostic_register_display(diagnostic_callback);
    
    while (true) {
        controller.loop();
        display.update();  // 10Hz, non-blocking
    }
}
```

---

## 🧪 Testing Strategy

**After Each Phase**:
1. Compile in TEST_BUILD mode (Windows tests)
2. Compile in hardware mode (ARM GCC)
3. Flash to Pico and verify functionality
4. Run dual-mode validation script
5. Commit working code

**Use Validation Script**:
```powershell
.\scripts\Validate-DualMode.ps1
```

---

## 🎯 Success Criteria

**Minimum Viable Product (Phases 1-3)**:
- ✅ LCD displays diagnostic messages
- ✅ Messages show correct severity colors
- ✅ Scrolling works smoothly
- ✅ No DCC signal degradation
- ✅ Compiles in both build modes

**Full Feature Set (Phases 1-4)**:
- ✅ Above + interactive touch buttons
- ✅ Power control via touch
- ✅ DCC-EX bidirectional echo
- ✅ Visual feedback (button states)

**Advanced Features (Phase 5)**:
- ✅ Calibration screen with live ADC
- ✅ Multi-screen navigation
- ✅ Settings/loco list screens

---

## ⏱️ Time Estimates

| Phase | Description | Core Tasks | Optional | Total |
|-------|-------------|------------|----------|-------|
| 1 | Hardware Bring-Up | 3-4 hrs | - | 4 hrs |
| 2 | LVGL Integration | 4-5 hrs | - | 5 hrs |
| 3 | Diagnostics | 2-3 hrs | +1 hr | 3 hrs |
| 4 | Touch Input | 3-4 hrs | - | 4 hrs |
| 5 | Advanced UI | 4-6 hrs | +5-7 hrs | 6 hrs |
| **TOTAL** | **All Phases** | **16-22 hrs** | **+6-8 hrs** | **22 hrs** |

**MVP (Phases 1-3)**: ~12 hours  
**Full Interactive (Phases 1-4)**: ~16 hours  
**Complete System (All phases)**: ~22 hours

---

## 🚦 Getting Started

### **Step 1**: Set Up Environment
```bash
cd /e/Development/PicoDCC
git checkout feature/lcd-display

# Add LVGL submodule
git submodule add https://github.com/lvgl/lvgl.git lib/external/lvgl
cd lib/external/lvgl
git checkout release/v8.3
cd ../../..
```

### **Step 2**: Create Component Structure
```bash
mkdir -p lib/PicoDCCDisplay/ui
mkdir -p lib/PicoDCCDisplay/mocks

# Create initial files (Phase 1, Task 1.2)
touch lib/PicoDCCDisplay/CMakeLists.txt
touch lib/PicoDCCDisplay/lv_conf.h
# ... (see implementation plan for full list)
```

### **Step 3**: Follow Phase 1 Tasks
Open `lcd-implementation-plan.md` and work through Phase 1, Task 1.1 onwards.

---

## 📚 Key Documents

1. **lcd-implementation-plan.md** - Complete detailed plan (YOU ARE HERE)
2. **lcd-design-questionnaire.md** - All design decisions documented
3. **gpio-pinout-reference.md** - Complete GPIO allocation table
4. **lcd-hardware-confirmed.md** - Hardware specifications verified
5. **architecture.md** - Update after completion with LCD component

---

## 🆘 Need Help?

### Common Issues:
- **Display blank**: Check SPI connections, try slower clock speed
- **Touch not working**: Verify I2C address (0x1A), check INT pin
- **Slow updates**: Profile with time_us_32(), optimize LVGL config
- **DCC degraded**: Verify Core 1 timing unaffected, check oscilloscope

### Troubleshooting:
See **lcd-implementation-plan.md** → "Troubleshooting Guide" section

### Resources:
- LVGL Docs: https://docs.lvgl.io/
- Pico SDK: https://raspberrypi.github.io/pico-sdk-doxygen/
- ST7789 Examples: Search GitHub for "pico st7789"
- CST328 I2C: Search GitHub for "cst328 i2c"

---

## 🎉 Ready to Begin!

You have everything you need:
- ✅ Hardware identified and verified
- ✅ Design decisions made
- ✅ GPIO pins assigned (no conflicts)
- ✅ Complete implementation plan
- ✅ Testing strategy defined

**Next Action**: Start Phase 1, Task 1.1 (Add LVGL submodule)

**Good luck with your LCD integration!** 🚀

---

**Questions?** Review the full implementation plan or check the troubleshooting guide.
