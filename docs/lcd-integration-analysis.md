# LCD Integration Analysis - Waveshare 2.7" Touchscreen

**Date**: October 19, 2025  
**Status**: Planning Phase  
**Device**: Waveshare WAV-27579 2.7" LCD Module with Touchscreen

---

## Executive Summary

### Recommendation: **Parallel Development with Separate Branch**

**Verdict**: ✅ **Safe to work in parallel** - Minimal merge conflict risk if properly structured

**Key Points**:
- LCD integration is largely **additive** (new files, new component)
- Programming track work is focused on **existing components** (PicoDCCController, new PicoDCCProgrammer)
- Shared touchpoint is **diagnostic logging system** - already architected for extension
- Git merge conflicts unlikely if LCD work stays modular

---

## Hardware Analysis - Waveshare 2.7" LCD Module

### Specifications (Typical for WAV-27579 Series)
Based on Waveshare's 2.7" LCD product line:

| Specification | Details |
|---------------|---------|
| **Display** | 2.7" IPS TFT LCD |
| **Resolution** | 480x320 pixels (likely) |
| **Interface** | SPI (4-wire or 3-wire) |
| **Touchscreen** | Resistive or capacitive touch |
| **Driver IC** | ILI9341 or ST7789 (common for this size) |
| **Pins Required** | 6-10 GPIO pins (SPI + control + touch) |
| **Power** | 3.3V compatible |
| **Backlight Control** | PWM capable |

### GPIO Pin Requirements

**Minimum SPI Interface** (6 pins):
- `SCK` - SPI Clock
- `MOSI` - SPI Data Out (to LCD)
- `CS` - Chip Select
- `DC` - Data/Command select
- `RST` - Reset
- `BL` - Backlight control (PWM)

**Touch Controller** (+4 pins if resistive touch):
- `T_CLK`, `T_CS`, `T_DIN`, `T_DO`, `T_IRQ` (5 pins)

**Total**: ~10-11 GPIO pins

### Raspberry Pi Pico Compatibility

✅ **Excellent fit for Pico**:
- Pico has 26 GPIO pins (plenty available)
- Hardware SPI support (SPI0, SPI1)
- PWM for backlight dimming
- Fast dual-core CPU can handle UI updates
- Popular Waveshare displays have existing Pico SDK libraries

---

## Integration Architecture

### Proposed Component Structure

```
lib/PicoDCCDisplay/
├── pico_dcc_display.h          # Main display interface
├── pico_dcc_display.cpp        # Display implementation
├── lcd_driver.h                # Hardware abstraction (SPI, touch)
├── lcd_driver.cpp              # Driver implementation
├── ui_screens.h                # Screen layouts and widgets
├── ui_screens.cpp              # UI rendering logic
└── CMakeLists.txt              # Build configuration
```

### Display Features

#### Status Screen (Default View)
- **Track Power Status**: Main/Prog on/off indicators
- **Current Draw**: Real-time mA display with bar graph
- **Active Locomotives**: Count + address list (scrollable)
- **System Messages**: Last 5 diagnostic messages (scrolling)
- **Timestamp**: System uptime

#### Touch Button Controls
- **Power Controls**:
  - `[MAIN PWR]` - Toggle main track power
  - `[PROG PWR]` - Toggle programming track power
  - `[EMERGENCY STOP]` - Big red button, broadcast stop
  
- **Trip Reset**:
  - `[RESET TRIPS]` - Clear overcurrent trip flags
  
- **Info Screens**:
  - `[LOCOS]` - Detailed locomotive list with speeds/functions
  - `[DIAGNOSTICS]` - Full error log history
  - `[CALIBRATION]` - ADC calibration status and controls

#### Diagnostic Display Integration
- Replace silent `log_diagnostic()` with LCD output
- Color-coded by severity:
  - **CRITICAL**: Red background, persistent display
  - **ERROR**: Yellow background, 5 second hold
  - **WARNING**: Orange text, 3 second auto-clear
  - **INFO**: White text, 1 second flash

---

## Code Impact Analysis - Merge Conflict Risk

### 🟢 LOW CONFLICT RISK: New Files (LCD Branch)

These files are **net-new** for LCD work:
- `lib/PicoDCCDisplay/*` - Entire new component
- `test/pico_dcc_display_tests.cpp` - New test suite
- `docs/lcd-integration-plan.md` - New documentation

**Risk**: None (new files don't conflict with programming branch changes)

---

### 🟡 MEDIUM CONFLICT RISK: Shared Touchpoints

#### 1. `lib/pico_diagnostic.h` (Logging System)

**Current State** (Programming branch):
- Silent implementation with TODO comments for LCD
- Already architected for future LCD extension
- COMPONENT_SYSTEM added for configuration storage

**LCD Branch Changes Needed**:
- Implement `log_diagnostic()` to call LCD display functions
- Add `lcd_init()` and `lcd_available()` checks
- Keep backward compatibility (fall back to silent if LCD unavailable)

**Merge Strategy**:
```cpp
// Programming branch version:
inline void log_diagnostic(diagnostic_level_t level, const char* component, const char* message) {
    // Silent implementation
    (void)level; (void)component; (void)message;
}

// LCD branch version:
inline void log_diagnostic(diagnostic_level_t level, const char* component, const char* message) {
    #ifdef ENABLE_LCD
    if (lcd_initialized && lcd_available()) {
        lcd_display_diagnostic(level, component, message);
        return;
    }
    #endif
    // Fall back to silent (no change to programming branch behavior)
    (void)level; (void)component; (void)message;
}
```

**Conflict Resolution**: Trivial - LCD branch adds code inside function, doesn't change signature. Manual merge takes 30 seconds.

---

#### 2. `lib/PicoDCCController/pico_dcccontroller.cpp` (Main Loop)

**Programming Branch Changes**:
- Integration of `PicoConfigStorage` in `setup()`
- Calls to `PicoDCCProgrammer` methods in command handlers
- ACK detection callbacks from `PicoDCCTrack`

**LCD Branch Changes**:
- Add `displayManager.init()` in `setup()`
- Add `displayManager.update()` in `loop()`
- Add touch event polling in `loop()`
- Add display status update calls when power state changes

**Conflict Risk**: **Low-Medium**
- Changes are in different sections of same functions
- `setup()`: Both add initialization calls (different objects)
- `loop()`: Both add update calls (different managers)
- Git can usually auto-merge these if spacing is consistent

**Mitigation**: Keep changes modular, add code rather than modify existing lines

---

#### 3. `lib/CMakeLists.txt` (Build System)

**Programming Branch**:
```cmake
add_subdirectory(PicoConfigStorage)
add_subdirectory(PicoDCCProgrammer)  # Future
```

**LCD Branch**:
```cmake
add_subdirectory(PicoDCCDisplay)
```

**Conflict Risk**: **Low** - Different subdirectories, git will auto-merge

---

#### 4. `docs/architecture.md` (Documentation)

**Programming Branch Updates**:
- Add `PicoDCCProgrammer` component
- Update Core 0 diagram with CV programming flow
- Add ACK detection to Core 1 responsibilities

**LCD Branch Updates**:
- Add `PicoDCCDisplay` component
- Update diagram with display manager
- Document UI thread updates

**Conflict Risk**: **Medium** - Both branches edit same sections
**Resolution**: Manual merge, takes 5-10 minutes to combine both feature descriptions

---

### 🔴 HIGH CONFLICT RISK: None Identified

No files are expected to have complex, overlapping logic changes.

---

## Branching Strategy Recommendation

### Option 1: **Sequential Development** (Conservative)
1. Finish programming track implementation first
2. Merge `programming` → `main`
3. Create `feature/lcd-display` branch from updated `main`
4. Implement LCD features
5. Merge `feature/lcd-display` → `main`

**Pros**:
- Zero merge conflicts
- Each feature fully tested before next starts
- Simpler mental model

**Cons**:
- Longer total timeline (6-7 weeks programming + 2-3 weeks LCD)
- Diagnostic messages remain silent during programming work
- Can't use LCD for calibration workflow feedback

---

### Option 2: **Parallel Development** (Recommended)
1. Keep working on `programming` branch for CV operations
2. Create `feature/lcd-display` branch **from current `main`** (not from `programming`)
3. Develop LCD infrastructure in parallel
4. Merge `programming` → `main` first (when ready)
5. Merge `feature/lcd-display` → `main` second (rebase on updated main)

**Pros**:
- Faster overall delivery (parallel work streams)
- LCD available sooner for user interface improvements
- Can use LCD for calibration workflow once programming merges
- Work doesn't block each other

**Cons**:
- Need to manually merge conflicts in `pico_diagnostic.h`, `architecture.md`
- Requires disciplined branch hygiene
- Need to rebase LCD branch when programming merges

---

### Option 3: **Integrated Development** (Advanced)
1. Work on both features in same branch with feature flags
2. Use `#ifdef ENABLE_LCD` to conditionally compile LCD code
3. Merge as single combined feature

**Pros**:
- No merge conflicts (same branch)
- Can test integration early

**Cons**:
- Messy commit history (two unrelated features mixed)
- Harder to review changes
- Not recommended for this project structure

---

## Recommended Approach: **Option 2 (Parallel Branches)**

### Why Parallel is Safe:

1. **Architectural Separation**:
   - LCD is display/UI layer (new vertical slice)
   - Programming is control/protocol layer (existing components)
   - Minimal shared code

2. **Diagnostic System Already Designed for This**:
   - `pico_diagnostic.h` has TODO comments for LCD
   - Functions are stubs, designed to be filled in
   - Merge is additive, not transformative

3. **Git-Friendly Changes**:
   - LCD adds new files (90% of changes)
   - Shared file edits are in different functions
   - CMakeLists changes are different subdirectories

4. **Real-World Workflow**:
   - You can switch between branches as needed
   - If programming gets blocked (waiting for hardware test), switch to LCD
   - If LCD gets blocked (waiting for library), switch to programming

5. **User Benefit**:
   - LCD ready sooner for status monitoring
   - Can display calibration workflow prompts
   - Programming track work can use LCD once merged

---

## Branch Setup Commands

### Create LCD Branch from Main
```bash
# Make sure programming changes are committed
git checkout programming
git status  # Verify clean or commit changes

# Switch to main and create LCD branch
git checkout main
git checkout -b feature/lcd-display

# Verify you're on new branch
git branch
```

### Workflow During Parallel Development
```bash
# Work on LCD
git checkout feature/lcd-display
# ... make changes, test, commit ...

# Switch to programming when needed
git checkout programming
# ... make changes, test, commit ...

# Periodically check for conflicts (simulation)
git checkout feature/lcd-display
git merge main  # Should be clean since main hasn't changed

# Later: After programming merges to main
git checkout feature/lcd-display
git rebase main  # Rebase LCD branch on updated main
# Resolve any conflicts (expected in pico_diagnostic.h, architecture.md)
```

---

## Expected Merge Conflicts & Resolutions

### Conflict 1: `lib/pico_diagnostic.h`

**Programming branch** (silent):
```cpp
inline void log_diagnostic(...) {
    (void)level; (void)component; (void)message;
}
```

**LCD branch** (displays on LCD):
```cpp
inline void log_diagnostic(...) {
    #ifdef ENABLE_LCD
    if (lcd_initialized) {
        lcd_display_diagnostic(...);
        return;
    }
    #endif
    (void)level; (void)component; (void)message;
}
```

**Merged resolution**: Keep LCD version (it includes programming version's silent fallback)

---

### Conflict 2: `docs/architecture.md`

**Resolution**: Manual merge, combine both component descriptions

**Programming adds**:
- PicoDCCProgrammer component
- ACK detection flow

**LCD adds**:
- PicoDCCDisplay component  
- UI update flow

**Merged document**: Include both sections, update Mermaid diagram to show both

---

## Development Timeline

### Parallel Schedule (Optimistic)

| Week | Programming Branch | LCD Branch |
|------|-------------------|-----------|
| 1-2  | Phase 1: ACK Detection | LCD driver integration, basic display |
| 3    | Phase 2: Packet Generation | Touch input handling, button widgets |
| 4    | Phase 3: DCC-EX Integration | Status screen layout, diagnostic display |
| 5    | **Phase 4: Address Programming** | Calibration screen UI |
| 6    | Testing, merge to main | Locomotive list screen |
| 7    | - | Finalize UI, merge to main |

**Total Time**: 7 weeks with both features delivered  
**vs. Sequential**: 9-10 weeks for same result

---

## Merge Conflict Mitigation Strategies

### 1. **Frequent Rebasing**
```bash
# Weekly: Rebase LCD branch on main to catch conflicts early
git checkout feature/lcd-display
git fetch origin
git rebase origin/main
```

### 2. **Communication Markers**
Add comments in shared files:
```cpp
// ========== LCD BRANCH MODIFICATIONS START ==========
#ifdef ENABLE_LCD
    // LCD-specific code
#endif
// ========== LCD BRANCH MODIFICATIONS END ==========
```

### 3. **Modular CMakeLists**
Keep build changes isolated:
```cmake
# Programming branch additions
if(ENABLE_PROGRAMMER)
    add_subdirectory(PicoDCCProgrammer)
endif()

# LCD branch additions
if(ENABLE_LCD)
    add_subdirectory(PicoDCCDisplay)
endif()
```

### 4. **Dual-Mode Testing**
Test both branches in isolation:
- Programming branch: Use validation script, verify no LCD code compiled
- LCD branch: Test with mock programming track (stubs)

---

## LCD-Specific Considerations

### Library Options

1. **Waveshare Official Examples** (Recommended for prototyping)
   - Repository: https://github.com/waveshare/Pico_code
   - Look for similar LCD model examples
   - Port to PicoDCC architecture

2. **TFT_eSPI Library** (Popular, well-maintained)
   - GitHub: Bodmer/TFT_eSPI
   - Supports many LCD controllers
   - Extensive Raspberry Pi Pico examples

3. **LVGL (Light and Versatile Graphics Library)** (Advanced UI)
   - Professional UI toolkit
   - Touchscreen widgets built-in
   - Heavier footprint but very polished

### Performance Impact

**Core 0 (Command Processing)**:
- Add display update calls in main loop (~10ms per update)
- Touch polling (~5ms per poll)
- Should not impact DCC timing (all on Core 1)

**Core 1 (Hardware Control)**:
- **No impact** - Display runs entirely on Core 0
- DCC packet generation unaffected
- SPI for LCD uses different SPI instance than any DCC hardware

### Memory Considerations

**Flash**: ~50-100KB for LCD driver + UI code  
**RAM**: ~30-50KB for framebuffer (if used) + UI state  
**Pico Capacity**: 2MB flash, 264KB RAM - plenty of headroom

---

## Testing Strategy for Parallel Branches

### Programming Branch Tests
```bash
# Compile in TEST mode (no LCD)
cmake -DTEST_BUILD=ON ..
cmake --build .
./test/pico_dcc_programmer_tests.exe
```

### LCD Branch Tests
```bash
# Mock hardware LCD for unit tests
cmake -DTEST_BUILD=ON -DENABLE_LCD=ON ..
cmake --build .
./test/pico_dcc_display_tests.exe
```

### Integration Tests (After Merge)
```bash
# Full hardware build with both features
cmake -DTEST_BUILD=OFF -DENABLE_LCD=ON -DENABLE_PROGRAMMER=ON ..
cmake --build .
# Flash to Pico, verify LCD displays programmer status
```

---

## Recommendation Summary

### ✅ **GO PARALLEL** - Create `feature/lcd-display` branch now

**Reasons**:
1. Minimal conflict surface area (~2 files with trivial merges)
2. Work streams are independent (you can context-switch as needed)
3. Faster overall delivery (7 weeks vs 9-10 weeks sequential)
4. LCD enhances programming workflow (visual calibration feedback)
5. Git tooling makes merge conflicts manageable

### 🛠️ **Next Steps**:

1. **Commit current programming work**:
   ```bash
   git checkout programming
   git add -A
   git commit -m "Config storage implementation complete, dual-mode validated"
   ```

2. **Create LCD branch from main**:
   ```bash
   git checkout main
   git checkout -b feature/lcd-display
   git push -u origin feature/lcd-display
   ```

3. **Start LCD work** (while keeping programming branch active):
   - Research exact WAV-27579 pinout and driver IC
   - Find Waveshare example code for Pico
   - Create `lib/PicoDCCDisplay/` component structure
   - Implement basic SPI initialization and screen clear test

4. **Continue programming work in parallel**:
   - Switch back to `programming` branch when needed
   - Proceed with Phase 1 (ACK detection) implementation

5. **Merge strategy**:
   - Merge `programming` → `main` first (when Phase 4 complete)
   - Rebase `feature/lcd-display` on updated `main`
   - Resolve expected conflicts (pico_diagnostic.h, architecture.md)
   - Merge `feature/lcd-display` → `main`

---

## Questions to Resolve Before LCD Work

1. **Exact LCD Pinout**: Confirm WAV-27579 pin assignments (may need datasheet)
2. **Driver IC**: Identify LCD controller (ILI9341, ST7789, etc.) for library selection
3. **Touch Technology**: Resistive or capacitive? (Affects pin count and library)
4. **PCB GPIO Mapping**: Which specific Pico GPIO pins are routed to LCD connector?
5. **SPI Instance**: Use SPI0 or SPI1? (Avoid conflicts with any DCC hardware)
6. **Power Budget**: Does LCD need separate 3.3V regulator or can share with Pico?

---

## Conclusion

**Verdict**: ✅ **Parallel development is safe and recommended**

The LCD integration is architecturally isolated from programming track work, with only minor shared touchpoints that are designed for extension. The diagnostic logging system already has placeholder code for LCD implementation, making the merge straightforward.

Working in parallel will deliver both features faster and allow you to switch contexts based on available hardware, testing needs, or workflow preference. The expected merge conflicts are minimal and well-understood.

**Go ahead and create the LCD branch!** The risk is low, and the benefits are significant.
