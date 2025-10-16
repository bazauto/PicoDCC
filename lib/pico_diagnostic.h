#ifndef PICO_DCC_DIAGNOSTIC_H
#define PICO_DCC_DIAGNOSTIC_H

#ifdef TEST_BUILD
#include "../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/time.h>
#endif

/**
 * PicoDCC Diagnostic Logging Infrastructure
 * 
 * This provides a clean abstraction for error and diagnostic reporting
 * that can be easily extended for different output methods (LCD, UART, etc.)
 * without affecting business logic.
 * 
 * Current implementation: Silent mode (no output)
 * Future extensions: LCD display, UART debug output, etc.
 */

// Diagnostic severity levels
typedef enum {
    DIAG_INFO = 0,      // Informational messages
    DIAG_WARNING = 1,   // Warning conditions
    DIAG_ERROR = 2,     // Error conditions
    DIAG_CRITICAL = 3   // Critical system failures
} diagnostic_level_t;

// Diagnostic message structure (for future LCD/storage use)
typedef struct {
    diagnostic_level_t level;
    uint32_t timestamp;
    const char* component;
    const char* message;
} diagnostic_msg_t;

/**
 * Core diagnostic logging function
 * Currently silent, but provides extension point for future output methods
 * 
 * FUTURE LCD IMPLEMENTATION GUIDE:
 * ================================
 * To add LCD display support:
 * 1. Add LCD library includes at top of this file
 * 2. Add LCD initialization function to be called from main()
 * 3. Replace the silent implementation below with LCD output calls
 * 4. Consider adding error history/scrolling for multiple messages
 * 5. Use severity levels for different display styles (colors, icons, etc.)
 * 
 * Example LCD integration:
 * - CRITICAL: Red background, immediate display, beep
 * - ERROR: Yellow background, hold for 5 seconds  
 * - WARNING: Normal display, clear after 3 seconds
 * - INFO: Brief flash, clear after 1 second
 */
inline void log_diagnostic(diagnostic_level_t level, const char* component, const char* message) {
    // Current implementation: Silent operation for safety-first approach
    // This ensures no interference with DCC-EX protocol compliance
    
#ifdef TEST_BUILD
    // For testing: Output critical messages to UART for test validation
    if (level >= DIAG_CRITICAL) {
        // Format: "CRITICAL:<COMPONENT>:<MESSAGE>"
        char diagnostic_output[256];
        snprintf(diagnostic_output, sizeof(diagnostic_output), "CRITICAL:%s:%s", component, message);
        uart_puts(uart0, diagnostic_output);
    }
#endif
    
    // TODO: Future LCD implementation goes here
    // Example structure:
    // if (lcd_initialized && lcd_available()) {
    //     lcd_display_error(level, component, message);
    //     if (level >= DIAG_CRITICAL) {
    //         lcd_set_backlight_color(LCD_RED);
    //         buzzer_alert();
    //     }
    // }
    
    (void)level;      // Suppress unused parameter warnings  
    (void)component;
    (void)message;
}

// Convenience macros for different severity levels
#define LOG_INFO(component, message)     log_diagnostic(DIAG_INFO, component, message)
#define LOG_WARNING(component, message)  log_diagnostic(DIAG_WARNING, component, message)
#define LOG_ERROR(component, message)    log_diagnostic(DIAG_ERROR, component, message)
#define LOG_CRITICAL(component, message) log_diagnostic(DIAG_CRITICAL, component, message)

// Component identifiers for consistent logging
#define COMPONENT_CONTROLLER "CONTROLLER"
#define COMPONENT_TRACK      "TRACK"
#define COMPONENT_POWER      "POWER"
#define COMPONENT_QUEUE      "QUEUE"
#define COMPONENT_CORE       "CORE"
#define COMPONENT_DCCEX      "DCCEX"

/**
 * Current Error Conditions Logged:
 * ================================
 * CRITICAL/CORE: "Core 1 heartbeat failure detected"
 *   - Triggered when Core 1 stops responding for >50ms
 *   - Indicates dual-core system failure
 *   - Results in immediate emergency power cutoff
 * 
 * CRITICAL/QUEUE: "Hardware command queue overflow"  
 *   - Triggered when inter-core command queue is full
 *   - Indicates Core 1 is too slow or dead
 *   - Results in immediate emergency power cutoff
 * 
 * CRITICAL/POWER: "Emergency power cutoff activated"
 *   - Triggered during any emergency shutdown procedure
 *   - Indicates safety system activation
 *   - All power cut, all queues cleared
 * 
 * CRITICAL/TRACK: "Overcurrent protection activated"
 *   - Triggered when track current exceeds 90% of maximum
 *   - Indicates short circuit or overload condition
 *   - Results in immediate track power cutoff
 * 
 * CRITICAL/TRACK: "DCC timing violation detected"
 *   - Triggered when DCC command gap exceeds 100ms
 *   - Indicates timing system failure
 *   - Results in immediate track power cutoff
 * 
 * Future error conditions to consider:
 * - Communication timeout errors
 * - Invalid DCC packet detection
 * - Hardware initialization failures
 * - Memory allocation failures
 */

#endif // PICO_DCC_DIAGNOSTIC_H