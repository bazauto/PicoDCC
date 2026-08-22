/* lib/PicoDCCDisplay/lv_conf.h */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16          // 16-bit RGB565
#define LV_COLOR_16_SWAP 1         // ST7789T3 over SPI wants big-endian RGB565

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0            // Use LVGL's built-in allocator
#define LV_MEM_SIZE (30U * 1024U)  // 30KB for LVGL heap (widgets, styles)

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_HOR_RES_MAX 320         // Horizontal resolution (landscape)
#define LV_VER_RES_MAX 240         // Vertical resolution (landscape)
#define LV_DPI_DEF 100             // DPI (affects text rendering)

/*====================
   FRAMEBUFFER
 *====================*/
#define LV_USE_GPU_RP2040_RENDER 0 // No GPU acceleration (RP2040 has none)
#define LV_DISP_DEF_REFR_PERIOD 100 // 100ms = 10Hz refresh (adjust in runtime)

/*====================
   INPUT DEVICE SETTINGS
 *====================*/
#define LV_INDEV_DEF_READ_PERIOD 10 // 10ms touch polling (when active)

/*====================
   FEATURE USAGE
 *====================*/
#define LV_USE_ANIMATION 1         // Enable animations (smooth transitions)
#define LV_USE_SHADOW 0            // Disable shadows (save RAM)
#define LV_USE_BLEND_MODES 0       // Disable blend modes (save CPU)
#define LV_USE_OPA_SCALE 1         // Enable opacity scaling
#define LV_USE_IMG_TRANSFORM 0     // Disable image rotation (save CPU)

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1    // Small text (status labels)
#define LV_FONT_MONTSERRAT_14 1    // Normal text (diagnostics)
#define LV_FONT_MONTSERRAT_16 1    // Medium text (buttons)
#define LV_FONT_MONTSERRAT_20 0    // Large text (disabled)
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGET USAGE
 *====================*/
#define LV_USE_BTN 1               // Buttons (for touch controls)
#define LV_USE_LABEL 1             // Text labels
#define LV_USE_LIST 1              // Scrollable list (for diagnostics)
#define LV_USE_TEXTAREA 1          // Text input (required by keyboard/spinbox)
#define LV_USE_CANVAS 0            // Drawing canvas (not needed)
#define LV_USE_CHART 0             // Charts (future: current graphs)
#define LV_USE_TABLE 0             // Tables (not needed)
#define LV_USE_KEYBOARD 0          // Virtual keyboard (not needed)
#define LV_USE_SPINBOX 0           // Spinbox widget (not needed)

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT 1     // Default theme
#define LV_THEME_DEFAULT_DARK 1    // Dark mode (black background)

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN  // Only warnings and errors
#define LV_LOG_PRINTF 1            // Use printf() for LVGL logs

#endif /* LV_CONF_H */
