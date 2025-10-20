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

// Log buffer configuration
#define DIAG_LOG_BUFFER_SIZE 30  // Number of log entries to store (30 × ~64 bytes = ~2KB)

// Circular buffer for diagnostic log storage
typedef struct {
    diagnostic_msg_t entries[DIAG_LOG_BUFFER_SIZE];
    uint8_t head;           // Next write position
    uint8_t count;          // Number of valid entries (0 to DIAG_LOG_BUFFER_SIZE)
    bool initialized;       // Buffer initialization flag
} diagnostic_log_buffer_t;

// Global log buffer (initialized in diag_log_init)
extern diagnostic_log_buffer_t g_diag_log_buffer;

// Log buffer management functions
void diag_log_init(void);
void diag_log_add(diagnostic_msg_t msg);
uint8_t diag_log_get_count(void);
diagnostic_msg_t* diag_log_get_entry(uint8_t index);
void diag_log_clear(void);

/**
 * Core diagnostic logging function
 * Now stores messages in circular buffer for LCD display
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
    // Store message in circular buffer for LCD display
    if (g_diag_log_buffer.initialized) {
        diagnostic_msg_t msg;
        msg.level = level;
#ifdef TEST_BUILD
        msg.timestamp = mock_time_ms;  // Use mock time in test mode
#else
        msg.timestamp = time_us_32() / 1000;  // Convert to milliseconds
#endif
        msg.component = component;
        msg.message = message;
        diag_log_add(msg);
    }
    
#ifdef TEST_BUILD
    // For testing: Output critical messages to UART for test validation
    if (level >= DIAG_CRITICAL) {
        // Format: "CRITICAL:<COMPONENT>:<MESSAGE>"
        char diagnostic_output[256];
        snprintf(diagnostic_output, sizeof(diagnostic_output), "CRITICAL:%s:%s", component, message);
        uart_puts(uart0, diagnostic_output);
    }
#endif
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