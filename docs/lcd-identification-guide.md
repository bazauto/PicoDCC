# How to Identify Your Waveshare WAV-27579 LCD Specifications

**Quick Reference Guide for Design Questionnaire**

---

## Method 1: Check Product Documentation 📄

### Waveshare Wiki Search
1. Go to: https://www.waveshare.com/wiki/Main_Page
2. Search for: "WAV-27579" or "2.7inch LCD"
3. Look for specifications page with:
   - Display controller IC name
   - Resolution (pixels)
   - Interface type (SPI/I2C/Parallel)
   - Touch controller (if applicable)

### Waveshare Store Page
1. Go to: https://www.waveshare.com/
2. Search product catalog for "WAV-27579" or "2.7 inch LCD"
3. Check product description for technical specs
4. Download datasheet if available (usually PDF link)

---

## Method 2: Physical Inspection 🔍

### What to Look For:

**On the LCD Module PCB**:
1. **Display Controller IC** (near ribbon cable or edge):
   - Look for chip marking: "ILI9341", "ST7789V", "ILI9488", etc.
   - May be very small text on black IC chip
   - Sometimes covered by COG (Chip-On-Glass) - can't see

2. **Touch Controller IC** (if present):
   - Separate chip, often labeled: "XPT2046", "FT6336", "TSC2046", etc.
   - Resistive touch: Look for XPT2046 or similar
   - Capacitive touch: Look for FT6x36 series

3. **Pin Headers/Connector**:
   - Count pins: 8-10 pins = SPI + touch
   - Label markings: SCK, MOSI, CS, DC, RST, etc.

**Example Photo Inspection**:
```
[LCD MODULE - Back View]
  
  [IC Marking: "ILI9341"]  ← Display controller
  
  [IC Marking: "XPT2046"]  ← Touch controller (resistive)
  
  [Pin Header]
  1. VCC    6. DC
  2. GND    7. RST  
  3. CS     8. BL
  4. SCK    9. T_CS
  5. MOSI   10. T_CLK
            11. T_DIN
            12. T_DO
            13. T_IRQ
```

---

## Method 3: Connector Pinout 📌

### Count the Pins on Your LCD Module:

**6-8 Pins** → Likely SPI display **without touch**:
- VCC, GND, CS, SCK, MOSI, DC, RST, BL

**13-14 Pins** → Likely SPI display **with resistive touch**:
- Display: VCC, GND, CS, SCK, MOSI, DC, RST, BL
- Touch: T_CS, T_CLK, T_DIN, T_DO, T_IRQ

**8-10 Pins with I2C labels** → Likely **capacitive touch**:
- Display: VCC, GND, CS, SCK, MOSI, DC, RST, BL
- Touch: SDA, SCL, INT (or RST)

### Common Waveshare 2.7" Models:

| Model Pattern | Controller | Resolution | Touch | Pins |
|---------------|-----------|-----------|-------|------|
| 2.7" IPS | ILI9341 | 240×320 | Resistive | 13-14 |
| 2.7" TFT | ST7789 | 240×320 | None | 8 |
| 2.7" Touch | ILI9488 | 480×320 | Capacitive | 10 |

---

## Method 4: GitHub Examples 💻

### Search Waveshare Pico Code Repository:

1. Visit: https://github.com/waveshare/Pico_code
2. Browse folders for similar LCD model names
3. Check example code for initialization sequences

**Example Search**:
```
Repository: waveshare/Pico_code
Path: c/lib/LCD/
Files: LCD_2inch4.c, LCD_2inch8.c, etc.

Look for initialization code like:
  LCD_COMMAND(0x11);  // Sleep out
  LCD_COMMAND(0x29);  // Display on
  
This reveals the command set, which identifies the controller.
```

### Common Waveshare Examples:
- `LCD_2inch4` → Usually ILI9341 (240×320)
- `LCD_2inch8` → Usually ILI9341 (240×320)
- Files with `ST7789` in name → ST7789 controller
- Files with `XPT2046` → Resistive touch

---

## Method 5: Multimeter Testing 🔌

**WARNING**: Only do this if comfortable with electronics!

### Test for I2C (Capacitive Touch):
1. Power off LCD
2. Check for pull-up resistors on SDA/SCL lines (4.7kΩ typical)
3. Capacitive touch usually has I2C address 0x38 or 0x48

### Test for SPI:
1. Check if CS line is active-low (pulled high when idle)
2. Confirm SCK and MOSI lines are present

---

## Method 6: Ask Waveshare Support 📧

If documentation is unclear:

1. **Submit Support Ticket**:
   - https://service.waveshare.com/
   - Provide model number: WAV-27579
   - Ask for: "Display controller IC, resolution, touch type, pinout diagram"

2. **Waveshare Forum**:
   - Search forums for "WAV-27579" discussions
   - Other users may have posted solutions

3. **Contact Distributor**:
   - If purchased from Amazon/eBay/etc., seller may have specs

---

## Method 7: Test with Arduino Libraries 🧪

**Quick Test Method** (if you have Arduino IDE):

1. Install TFT_eSPI library in Arduino IDE
2. Try initialization code for different controllers:

```cpp
// Test ILI9341
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init();
  tft.fillScreen(TFT_RED);  // If screen turns red, it's working!
}
```

3. Cycle through common controllers until display responds:
   - ILI9341
   - ST7789
   - ILI9488

**If this works**, you've confirmed the controller type!

---

## Common Waveshare 2.7" Specifications (Best Guesses)

Based on typical Waveshare 2.7" modules:

### Most Likely Configuration:
```yaml
Controller: ILI9341 (very common)
Resolution: 240×320 pixels
Interface: 4-wire SPI
Touch: XPT2046 (resistive, 4-wire)
Voltage: 3.3V
Backlight: LED backlight, PWM controllable
Pins: 13-14 (8 for display + 5 for touch)
```

### Pin Functions (Typical):
```
Display:
  VCC  - Power (3.3V)
  GND  - Ground
  CS   - Chip Select (active low)
  RST  - Reset (active low)
  DC   - Data/Command select
  SCK  - SPI Clock
  MOSI - SPI Data (Master Out Slave In)
  BL   - Backlight control (PWM or 3.3V)

Touch (Resistive):
  T_CS  - Touch CS
  T_CLK - Touch Clock
  T_DIN - Touch Data In
  T_DO  - Touch Data Out
  T_IRQ - Touch Interrupt (active low when touched)
```

---

## If You Can't Find Exact Specs...

### Use These Safe Assumptions:

1. **Start with ILI9341** - Most common 2.7" controller
2. **Assume 240×320 resolution** - Standard for this size
3. **Assume 4-wire SPI** - Most common interface
4. **Check for 13-14 pins** - Indicates resistive touch
5. **Use TFT_eSPI library** - Supports most controllers

### Validation Test:
```cpp
// Try this initialization sequence (works for ILI9341 and ST7789):
void testLCD() {
  gpio_init(LCD_CS);
  gpio_init(LCD_DC);
  gpio_init(LCD_RST);
  gpio_set_dir(LCD_CS, GPIO_OUT);
  gpio_set_dir(LCD_DC, GPIO_OUT);
  gpio_set_dir(LCD_RST, GPIO_OUT);
  
  // Reset sequence
  gpio_put(LCD_RST, 0);
  sleep_ms(10);
  gpio_put(LCD_RST, 1);
  sleep_ms(120);
  
  // Try to read display ID (works on most controllers)
  uint32_t id = lcd_read_id();  // Should return non-zero
  printf("LCD ID: 0x%08X\n", id);
}
```

If `lcd_read_id()` returns a sensible value (not 0x00000000 or 0xFFFFFFFF), you're communicating!

---

## Quick Decision Matrix

Use this to fill out the questionnaire if unsure:

| If You See... | Then Choose... |
|---------------|----------------|
| 13-14 pins total | Resistive touch + SPI display |
| 8-10 pins, labels with T_* | Resistive touch |
| 8-10 pins, labels with SDA/SCL | Capacitive touch (I2C) |
| IC chip marked "ILI9341" | ILI9341 controller, 240×320 |
| IC chip marked "ST7789" | ST7789 controller, 240×320 |
| IC chip marked "ILI9488" | ILI9488 controller, 480×320 |
| IC chip marked "XPT2046" | Resistive touch controller |
| IC chip marked "FT6x36" | Capacitive touch controller |

---

## Still Stuck?

**Post a photo** of:
1. LCD module front (screen side)
2. LCD module back (PCB side, zoom on ICs)
3. Pin header labels (close-up)

And we can help identify the exact specifications!

---

**Return to**: `docs/lcd-design-questionnaire.md` to fill in your answers.
