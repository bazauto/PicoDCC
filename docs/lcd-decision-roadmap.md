# LCD Integration - Design Decision Roadmap

**Branch**: `feature/lcd-display`  
**Date**: October 19, 2025  
**Status**: Design Phase

---

## 🎯 Your Mission

Answer the design questions to generate your implementation plan. This roadmap shows you **what to do next** based on what you know.

---

## 🚦 Decision Flow

```
START HERE
    ↓
┌───────────────────────────────────┐
│ Do you have the LCD physically?   │
├───────────────────────────────────┤
│ ☐ Yes, it's here                  │──→ Go to PATH A (Hardware First)
│ ☐ No, on order                    │──→ Go to PATH B (Software First)
│ ☐ Unsure/need to find it          │──→ Check your parts bin!
└───────────────────────────────────┘
```

---

## PATH A: Hardware-First Approach 🔌
*You have the LCD module in hand*

### Step 1: Identify Your Hardware (15 minutes)
📁 **Use**: `docs/lcd-identification-guide.md`

**Quick Tasks**:
- [ ] Take photo of LCD front
- [ ] Take photo of LCD back (PCB with chips)
- [ ] Count pins on connector (8? 13? 14?)
- [ ] Look for IC markings (ILI9341, ST7789, XPT2046, etc.)
- [ ] Check if you have the original product page/datasheet

**Fill in** `docs/lcd-design-questionnaire.md` **Section 1** (Hardware Identification)

---

### Step 2: Check Your PCB (10 minutes)
**Question**: Have you designed the PCB connector for this LCD yet?

- [ ] **Yes, PCB is designed** → Note which GPIO pins go to which LCD signals
- [ ] **No, PCB is in progress** → We'll suggest optimal GPIO mapping
- [ ] **Using breadboard/jumpers** → We'll use flexible GPIO assignments

**Fill in** `docs/lcd-design-questionnaire.md` **Section 2** (GPIO Mapping)

---

### Step 3: Pick Your Software Stack (5 minutes)
**Recommended for quick start**: TFT_eSPI library

**Decision Tree**:
```
Do you want to get LCD working FAST?
   ↓ YES
   Use TFT_eSPI (Option A)
   
Do you want a BEAUTIFUL UI with animations?
   ↓ YES
   Use LVGL (Option C)
   
Do you want MAXIMUM CONTROL?
   ↓ YES
   Raw Pico SDK (Option D)
   
Not sure?
   ↓
   Use TFT_eSPI (safest choice)
```

**Fill in** `docs/lcd-design-questionnaire.md` **Section 3** (Software Architecture)

---

### Step 4: Define Your UI (10 minutes)
**What do you want to SEE on the screen?**

Look at the three mockups in Section 4.1:
- Option A: Status Dashboard (detailed)
- Option B: Diagnostic Focus (messages)
- Option C: Simplified (minimal)

Pick one, or sketch your own!

**Fill in** `docs/lcd-design-questionnaire.md` **Section 4** (UI Design)

---

### Step 5: Choose Integration Points (5 minutes)
**How should LCD interact with existing code?**

Key decisions:
- When to show diagnostic messages? (all? errors only?)
- Should calibration workflow show on LCD?
- Touch buttons: direct control or confirmation required?

**Fill in** `docs/lcd-design-questionnaire.md` **Section 5** (Integration Points)

---

### ✅ PATH A Complete!
**Next Step**: Review your answers, then generate implementation plan

**Command**:
```bash
# Commit your questionnaire answers
git add docs/lcd-design-questionnaire.md
git commit -m "LCD design decisions - hardware identified, UI planned"

# Ask for implementation plan generation
```

---

## PATH B: Software-First Approach 💻
*LCD is on order, or you want to plan software architecture first*

### Step 1: Use Safe Defaults (2 minutes)
Copy the "Quick Start Path" defaults from `lcd-design-questionnaire.md`:

```yaml
LCD Controller: ILI9341  # Most common
Resolution: 240x320
Touch: Resistive 4-wire
Interface: SPI 4-wire
SPI Instance: SPI1 (GP8-14)
Graphics Library: TFT_eSPI
```

These work for 90% of Waveshare 2.7" displays.

**Fill in** `docs/lcd-design-questionnaire.md` **Section 1 & 2** with defaults

---

### Step 2: Design Your UI (15 minutes)
**Sketch your ideal screen layout**

Even without hardware, you can decide:
- What information to display?
- What buttons do you need?
- What colors/theme?

**Fill in** `docs/lcd-design-questionnaire.md` **Section 4** (UI Design)

---

### Step 3: Plan Software Architecture (10 minutes)
**Decide how code will be structured**

Key decisions:
- Polling or timer-based updates?
- Framebuffer or direct draw?
- Which library? (TFT_eSPI recommended)

**Fill in** `docs/lcd-design-questionnaire.md` **Section 3** (Software Architecture)

---

### Step 4: Define Test Strategy (5 minutes)
**How will you test without hardware?**

Options:
- Mock LCD in TEST_BUILD mode (recommended)
- Wait for hardware, test then
- Use simulator (if library supports it)

**Fill in** `docs/lcd-design-questionnaire.md` **Section 7** (Testing Strategy)

---

### Step 5: Write Mock Implementation (30 minutes)
**Create stub code structure**

We can generate:
```
lib/PicoDCCDisplay/
├── pico_dcc_display.h       # Interface (real + mock)
├── pico_dcc_display.cpp     # Implementation (compiles but does nothing in TEST_BUILD)
└── CMakeLists.txt           # Build config
```

This lets you:
- Continue programming branch work
- Integrate display calls in controller
- Compile everything without errors
- Swap in real LCD code when hardware arrives

---

### ✅ PATH B Complete!
**Next Step**: Generate mock implementation, integrate with controller

**When LCD arrives**: Update Section 1 & 2 in questionnaire, swap mock for real driver

---

## 🎨 UI Design Shortcuts

### Don't want to design UI from scratch?

**Option 1: Copy DCC-EX**
- Use DCC-EX CommandStation LCD layout as reference
- GitHub: DCC-EX/CommandStation-EX (look for LCD code)

**Option 2: Minimal Status**
- Just show: Track power (on/off), Current (mA), Loco count
- 3-4 lines of text, no fancy graphics

**Option 3: Wait for Prototype**
- Implement basic text display first
- Refine UI after seeing it on real hardware

---

## 📊 Decision Tracking

### Critical Decisions (Must Make Before Implementation):
- [ ] **Q1.1**: LCD controller IC (or use ILI9341 default)
- [ ] **Q2.2**: Which SPI instance? (SPI0 or SPI1)
- [ ] **Q3.3**: Which graphics library? (TFT_eSPI recommended)
- [ ] **Q4.1**: What to display on main screen?

### Important (Should Decide Soon):
- [ ] **Q2.1**: GPIO pin mapping
- [ ] **Q3.1**: Update strategy (polling recommended)
- [ ] **Q3.2**: Framebuffer? (no for simple UI)
- [ ] **Q5.1**: Diagnostic levels to show

### Optional (Can Decide Later):
- [ ] **Q4.2**: Touch button layout (can skip touch initially)
- [ ] **Q4.3**: Color scheme (can change anytime)
- [ ] **Q8.1**: Refresh rate (can tune after testing)
- [ ] **Q8.2**: Backlight control (can add later)

---

## 🚀 Fast-Track Option

**Want to skip the questionnaire and just START?**

Use these **sensible defaults** and start coding:

```yaml
# Assumed Hardware
Controller: ILI9341
Resolution: 240x320
Touch: None (display only for Phase 1)
Interface: SPI 4-wire via SPI1

# GPIO Mapping (suggested)
GP10 = SPI1 SCK   → LCD SCK
GP11 = SPI1 TX    → LCD MOSI
GP9  = SPI1 CS    → LCD CS
GP12 = GPIO       → LCD DC
GP13 = GPIO       → LCD RST
GP14 = GPIO/PWM7A → LCD BL

# Software
Library: TFT_eSPI (forked/adapted for Pico SDK)
Framebuffer: No (direct draw)
Update: Polling in main loop (10 Hz)

# UI
Layout: Status Dashboard (Option A from questionnaire)
Colors: Black background, white text, green=OK, red=error
Buttons: None (Phase 1 is display only)
```

**Generate starter code with these defaults**, refine later!

---

## 📁 Files to Work With

| File | Purpose | Priority |
|------|---------|----------|
| `lcd-design-questionnaire.md` | **Answer design questions** | 🔴 High |
| `lcd-identification-guide.md` | Help identify hardware specs | 🟡 Medium |
| `lcd-integration-analysis.md` | Architecture analysis (read only) | 🟢 Reference |
| `lcd-implementation-plan.md` | **Generated after questionnaire** | ⏳ Next |

---

## ⏭️ What Happens Next?

### After Questionnaire is Complete:

1. **We generate**: `lcd-implementation-plan.md`
   - Detailed task breakdown
   - Code structure
   - File-by-file implementation guide
   
2. **We create**: Component skeleton
   ```
   lib/PicoDCCDisplay/
   ├── pico_dcc_display.h
   ├── pico_dcc_display.cpp
   ├── lcd_driver.h
   ├── lcd_driver.cpp
   └── CMakeLists.txt
   ```

3. **We integrate**: Hook into existing code
   - Modify `pico_diagnostic.h`
   - Add display calls to `PicoDccController`
   - Create test infrastructure

4. **You test**: Flash to Pico, verify display works!

---

## 🆘 Stuck or Unsure?

### Common Questions:

**"I don't know which controller IC I have"**
→ Use ILI9341 (most common), test when hardware arrives

**"I haven't designed the PCB yet"**
→ Use suggested GPIO mapping, we'll generate pinout diagram

**"I don't know what UI I want"**
→ Start with Status Dashboard (Option A), iterate later

**"I want to keep it simple"**
→ Use fast-track defaults, skip touch, minimal UI

**"I want it to look professional"**
→ Use LVGL library, plan for multi-screen UI

---

## 📝 Questionnaire Completion Status

Track your progress:

```
☐ Section 1: Hardware Identification (Q1.1-Q1.4)
☐ Section 2: PCB GPIO Mapping (Q2.1-Q2.2)
☐ Section 3: Software Architecture (Q3.1-Q3.4)
☐ Section 4: UI Design (Q4.1-Q4.3)
☐ Section 5: Integration Points (Q5.1-Q5.3)
☐ Section 6: Development Phases (Q6.1)
☐ Section 7: Testing Strategy (Q7.1-Q7.2)
☐ Section 8: Performance & Power (Q8.1-Q8.2)
☐ Section 9: Dependencies (Q9.1)
☐ Section 10: Documentation (Q10.1)
```

**Minimum to proceed**: Sections 1, 2, 3, 4 (critical decisions)

---

## 🎯 Your Next Action

**Choose One**:

1. **[ ] Fill out questionnaire completely** (~30 min)
   - Most thorough approach
   - Best for complex UI plans
   
2. **[ ] Fill out critical sections only** (~15 min)
   - Sections 1-4 (hardware + basic UI)
   - Skip optional features for now
   
3. **[ ] Use fast-track defaults** (~5 min)
   - Accept suggested configuration
   - Start coding immediately
   
4. **[ ] Ask specific questions** 
   - Not sure about something?
   - Need clarification on options?

**Reply with**: "I choose option [1/2/3/4]" or ask questions!

---

**Ready to make decisions? Open**: `docs/lcd-design-questionnaire.md`
