# LCD Landscape Orientation Configuration

**Date**: October 19, 2025  
**Component**: PicoDCCDisplay  
**Change**: Switched from portrait to landscape orientation  
**Reason**: Better fit for physical LCD mounting

---

## Configuration Summary

### Display Orientation

**Previous (Portrait)**:
- Resolution: 240 (W) × 320 (H) pixels
- Orientation: 0° (default)
- MADCTL register: `0x00`

**Current (Landscape)**:
- Resolution: 320 (W) × 240 (H) pixels
- Orientation: 90° clockwise rotation
- MADCTL register: `0x60` (MX=0, MY=1, MV=1, RGB=0)

---

## Implementation Changes

### 1. ST7789T3 LCD Driver (`lcd_driver.cpp`)

**MADCTL Register Configuration**:
```cpp
writeCommand(ST7789_MADCTL);   // Memory access control
writeData(0x60);               // Landscape mode: MX=0, MY=1, MV=1, RGB=0
```

**MADCTL Bit Meanings**:
- **MV (bit 5)**: Row/Column Exchange = 1 (swap X and Y)
- **MY (bit 6)**: Row Address Order = 1 (bottom to top)
- **MX (bit 7)**: Column Address Order = 0 (left to right)
- **RGB (bit 3)**: RGB/BGR Order = 0 (RGB mode)

**Result**: Display rotated 90° clockwise, origin at top-left

---

### 2. LVGL Configuration (`lv_conf.h`)

**Resolution Update**:
```c
#define LV_HOR_RES_MAX 320         // Horizontal resolution (landscape)
#define LV_VER_RES_MAX 240         // Vertical resolution (landscape)
```

**Memory Impact**:
- Framebuffer size: 320 × 240 × 2 bytes = **153,600 bytes (150KB)**
- LVGL buffer: 320 × 20 × 2 bytes = **12,800 bytes** (20-line buffer)
- Total LCD RAM: ~165KB (increased from ~100KB in portrait)

---

### 3. LVGL Driver Initialization (`pico_dcc_display.cpp`)

**Display Driver Setup**:
```cpp
bool PicoDCCDisplay::initLVGL() {
    lv_init();
    
    // Initialize display buffer (20 lines)
    lv_disp_draw_buf_init(&disp_buf_, buf1_, nullptr, LV_HOR_RES_MAX * 20);
    
    // Initialize display driver (landscape: 320x240)
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = 320;
    disp_drv_.ver_res = 240;
    disp_drv_.flush_cb = flushCallback;
    disp_drv_.draw_buf = &disp_buf_;
    lv_disp_drv_register(&disp_drv_);
    
    return true;
}
```

---

### 4. Test Pattern (`displayTestPattern()`)

**Previous (Horizontal bars)**:
```cpp
// 8 horizontal bars, each 40 pixels tall (240 height ÷ 8)
for (int i = 0; i < 8; i++) {
    lcd_.setWindow(0, i * 40, 239, (i + 1) * 40 - 1);
    // Fill 240 wide × 40 tall = 9,600 pixels
}
```

**Current (Vertical bars)**:
```cpp
// 8 vertical bars, each 40 pixels wide (320 width ÷ 8)
for (int i = 0; i < 8; i++) {
    lcd_.setWindow(i * 40, 0, (i + 1) * 40 - 1, 239);
    // Fill 40 wide × 240 tall = 9,600 pixels
}
```

**Colors**: RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, BLACK (left to right)

---

### 5. Diagnostic Screen Layout (`createDiagnosticScreen()`)

**Layout Design**:
```
┌──────────────────────────────────────┐
│        PicoDCC Status (centered)     │ ← Title (16pt)
├──────────────────────────────────────┤
│                                      │
│  Main: ON        Prog: OFF           │ ← Track power (left/center columns)
│  123.4 mA        0.0 mA               │ ← Current readings
│                                      │
│                                      │
├──────────────────────────────────────┤
│ Packets: 12345 (678 idle)  Locos: 3 │ ← Bottom status bar (12pt)
└──────────────────────────────────────┘
```

**Label Positioning** (LVGL alignment):
- **Title**: `LV_ALIGN_TOP_MID` (centered, Y=5)
- **Main Track Power**: `LV_ALIGN_TOP_LEFT` (X=10, Y=35)
- **Main Track Current**: `LV_ALIGN_TOP_LEFT` (X=10, Y=60)
- **Prog Track Power**: `LV_ALIGN_TOP_MID` (centered, Y=35)
- **Prog Track Current**: `LV_ALIGN_TOP_MID` (centered, Y=60)
- **Packets**: `LV_ALIGN_BOTTOM_LEFT` (X=10, Y=-10)
- **Locos**: `LV_ALIGN_BOTTOM_RIGHT` (X=-10, Y=-10)

---

## Design Rationale

### Why Landscape?

1. **Physical Mounting**: Landscape fits better with horizontal bench/control panel layout
2. **Content Layout**: Wider screen allows side-by-side track status (Main | Prog)
3. **Text Readability**: Wider lines for diagnostic messages in future scrolling log
4. **Touch Buttons**: More horizontal space for button rows in Phase 4

### Trade-offs

**Advantages**:
- Better use of horizontal space for multi-column layouts
- More natural for button grids (e.g., 3×2 vs 2×3)
- Easier to read longer diagnostic messages

**Disadvantages**:
- None identified for this application

---

## Future Phase Guidelines

### Phase 3: Diagnostic Message Log

**Scrolling Text Area** (planned):
```
┌──────────────────────────────────────┐
│        PicoDCC Diagnostics           │
├──────────────────────────────────────┤
│ [12:34:56] Main track powered ON     │ ← Scrolling log area
│ [12:34:58] Loco 3 added at addr 3    │   (wider = more text per line)
│ [12:35:01] Current: 234.5 mA         │
│ [12:35:05] Overcurrent trip: Main    │
│ [12:35:07] Main track powered OFF    │
└──────────────────────────────────────┘
```

**Benefit**: 320-pixel width allows ~53 characters per line (6px/char) vs 40 chars in portrait

---

### Phase 4: Touch Button Layout

**Button Grid** (landscape-optimized):
```
┌──────────────────────────────────────┐
│  [MAIN PWR]  [PROG PWR]  [RESET]     │ ← Row 1: Power controls
│  [DIAG LOG]  [CALIB]    [SETTINGS]   │ ← Row 2: Utility screens
└──────────────────────────────────────┘
```

**Button Dimensions**:
- Width: ~100 pixels each (3 across)
- Height: ~60 pixels (touchable area)
- Spacing: 10 pixels between buttons

---

### Phase 5: Calibration Screen

**Horizontal Slider Layout**:
```
┌──────────────────────────────────────┐
│     Current Sensor Calibration       │
├──────────────────────────────────────┤
│                                      │
│  Main Track Current Limit:           │
│  ┌────────●──────────────┐  2.5 A    │ ← Horizontal slider (landscape-friendly)
│                                      │
│  Prog Track Current Limit:           │
│  ┌────●──────────────────┐  1.0 A    │
│                                      │
│         [SAVE]    [CANCEL]           │
└──────────────────────────────────────┘
```

---

## Testing Checklist

### Hardware Validation

- [x] Test pattern displays 8 vertical color bars (RED→BLACK, left to right)
- [x] Diagnostic screen shows title centered at top
- [x] Main track status displays in left column
- [x] Prog track status displays in center column
- [x] Packet count displays at bottom-left
- [x] Loco count displays at bottom-right
- [x] Power status updates (green ON, red OFF)
- [x] Current readings update correctly
- [x] Screen refresh works at 10Hz

### Future Phase Compatibility

- [ ] Phase 3: Verify scrolling log uses full 320-pixel width
- [ ] Phase 4: Test touch button layout (3 columns × 2 rows)
- [ ] Phase 5: Verify calibration sliders work horizontally

---

## Configuration Reference

### Quick Settings Summary

| Setting | Value | Location |
|---------|-------|----------|
| **MADCTL** | `0x60` | `lib/PicoDCCDisplay/lcd_driver.cpp` |
| **LV_HOR_RES_MAX** | `320` | `lib/PicoDCCDisplay/lv_conf.h` |
| **LV_VER_RES_MAX** | `240` | `lib/PicoDCCDisplay/lv_conf.h` |
| **disp_drv_.hor_res** | `320` | `lib/PicoDCCDisplay/pico_dcc_display.cpp` |
| **disp_drv_.ver_res** | `240` | `lib/PicoDCCDisplay/pico_dcc_display.cpp` |
| **Test Pattern** | 8 vertical bars | `displayTestPattern()` |
| **Layout Style** | 3-column | `createDiagnosticScreen()` |

---

## Migration Notes

### If Switching Back to Portrait

1. **lcd_driver.cpp**: Change MADCTL to `0x00`
2. **lv_conf.h**: Swap resolution to 240×320
3. **pico_dcc_display.cpp**: Update driver to `hor_res=240, ver_res=320`
4. **Test pattern**: Change to horizontal bars (`setWindow(0, i*40, 239, (i+1)*40-1)`)
5. **Diagnostic layout**: Stack labels vertically instead of columns

### For Different Rotations

**MADCTL Values**:
- `0x00`: 0° (portrait, default)
- `0x60`: 90° clockwise (landscape, current)
- `0xC0`: 180° (portrait, upside-down)
- `0xA0`: 270° clockwise (landscape, reversed)

**Note**: Always update resolution defines to match orientation.

---

## Version History

- **2025-10-19**: Initial landscape configuration (Phase 2)
- Display rotated 90° clockwise from portrait default
- All coordinates updated for 320×240 layout
- Diagnostic screen redesigned for multi-column layout

---

**Status**: ✅ Implemented and validated in hardware  
**Next Steps**: Phase 3 (diagnostic message integration) will use landscape layout
