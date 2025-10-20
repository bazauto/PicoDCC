/* lib/pico_diagnostic.cpp */
#include "pico_diagnostic.h"
#include <string.h>
#ifndef TEST_BUILD
#include "pico/time.h"  // For time_us_32()
#endif

// Global log buffer instance - initialized explicitly to avoid C++11 initialization issues
diagnostic_log_buffer_t g_diag_log_buffer;

/**
 * @brief Initialize the diagnostic log buffer
 * Should be called once during system startup
 */
void diag_log_init(void) {
    g_diag_log_buffer.head = 0;
    g_diag_log_buffer.count = 0;
    g_diag_log_buffer.initialized = true;
    
#ifndef TEST_BUILD
    // Initialize semaphore for multi-core access
    sem_init(&g_diag_log_buffer.sem, 1, 1);
#endif
    
    // Clear all entries
    memset(g_diag_log_buffer.entries, 0, sizeof(g_diag_log_buffer.entries));
}

/**
 * @brief Add a diagnostic message to the circular buffer
 * @param msg Diagnostic message to store
 * 
 * Implements circular buffer logic:
 * - Newest messages overwrite oldest when buffer is full
 * - Thread-safe for multi-core access (semaphore protected)
 * - No dynamic memory allocation
 */
void diag_log_add(diagnostic_msg_t msg) {
    if (!g_diag_log_buffer.initialized) {
        return;  // Buffer not initialized, ignore
    }
    
#ifndef TEST_BUILD
    // Acquire semaphore for thread-safe access
    sem_acquire_blocking(&g_diag_log_buffer.sem);
#endif
    
    // CRITICAL: Bounds check head position to prevent buffer overflow
    if (g_diag_log_buffer.head >= DIAG_LOG_BUFFER_SIZE) {
        g_diag_log_buffer.head = 0;  // Reset to valid range
    }
    
    // Add message at head position
    g_diag_log_buffer.entries[g_diag_log_buffer.head] = msg;
    
    // Advance head (circular wrap-around)
    g_diag_log_buffer.head = (g_diag_log_buffer.head + 1) % DIAG_LOG_BUFFER_SIZE;
    
    // Update count (saturate at buffer size)
    if (g_diag_log_buffer.count < DIAG_LOG_BUFFER_SIZE) {
        g_diag_log_buffer.count++;
    }
    
#ifndef TEST_BUILD
    // Release semaphore
    sem_release(&g_diag_log_buffer.sem);
#endif
}

/**
 * @brief Get the number of valid log entries in the buffer
 * @return Number of entries (0 to DIAG_LOG_BUFFER_SIZE)
 */
uint8_t diag_log_get_count(void) {
    if (!g_diag_log_buffer.initialized) {
        return 0;
    }
    return g_diag_log_buffer.count;
}

/**
 * @brief Retrieve a log entry by index (thread-safe copy)
 * @param index Entry index (0 = oldest, count-1 = newest)
 * @param out_entry Pointer to output structure to receive copied entry
 * @return true if entry was copied successfully, false if invalid index
 * 
 * Index mapping:
 * - index 0 = oldest entry (first to be overwritten)
 * - index (count-1) = newest entry (most recently added)
 * 
 * Thread-safety:
 * - Makes a local copy of the entry under semaphore protection
 * - Caller receives a copy, not a pointer to shared data
 */
bool diag_log_get_entry(uint8_t index, diagnostic_msg_t* out_entry) {
    if (!g_diag_log_buffer.initialized || !out_entry) {
        return false;  // Invalid parameters
    }
    
#ifndef TEST_BUILD
    // Try to acquire semaphore WITHOUT BLOCKING
    // If Core 1 is writing, skip this read to avoid blocking Core 0
    if (!sem_try_acquire(&g_diag_log_buffer.sem)) {
        return false;  // Buffer busy, skip this entry
    }
#endif
    
    // Check index validity while holding semaphore
    if (index >= g_diag_log_buffer.count) {
#ifndef TEST_BUILD
        sem_release(&g_diag_log_buffer.sem);
#endif
        return false;  // Invalid index
    }
    
    // Calculate actual position in circular buffer
    // If buffer is not full: entries start at 0
    // If buffer is full: oldest entry is at head position
    uint8_t start_pos;
    if (g_diag_log_buffer.count < DIAG_LOG_BUFFER_SIZE) {
        // Buffer not full, oldest entry is at position 0
        start_pos = 0;
    } else {
        // Buffer full, oldest entry is at head position (about to be overwritten)
        start_pos = g_diag_log_buffer.head;
    }
    
    // Calculate position with wrap-around
    uint8_t position = (start_pos + index) % DIAG_LOG_BUFFER_SIZE;
    
    // CRITICAL: Bounds check to prevent buffer overflow
    if (position >= DIAG_LOG_BUFFER_SIZE) {
#ifndef TEST_BUILD
        sem_release(&g_diag_log_buffer.sem);
#endif
        return false;  // Invalid calculated position
    }
    
    // Copy the entry using memcpy to ensure safe byte-by-byte copy
    // Avoid struct assignment which might use unaligned word operations
    memcpy(out_entry, &g_diag_log_buffer.entries[position], sizeof(diagnostic_msg_t));
    
#ifndef TEST_BUILD
    // Release semaphore
    sem_release(&g_diag_log_buffer.sem);
#endif
    
    return true;
}

/**
 * @brief Clear all log entries from the buffer
 * Resets the buffer to empty state without de-initializing
 */
void diag_log_clear(void) {
    if (!g_diag_log_buffer.initialized) {
        return;
    }
    
#ifndef TEST_BUILD
    // Acquire semaphore for thread-safe access
    sem_acquire_blocking(&g_diag_log_buffer.sem);
#endif
    
    g_diag_log_buffer.head = 0;
    g_diag_log_buffer.count = 0;
    
    // Optional: zero out entries for security/debugging
    memset(g_diag_log_buffer.entries, 0, sizeof(g_diag_log_buffer.entries));
    
#ifndef TEST_BUILD
    // Release semaphore
    sem_release(&g_diag_log_buffer.sem);
#endif
}

/**
 * @brief Core diagnostic logging function
 * Stores messages in circular buffer for LCD display
 * Moved to .cpp to avoid inline function stack allocation issues
 * 
 * @param level Severity level of the diagnostic message
 * @param component Component identifier string
 * @param message Diagnostic message text
 * 
 * Thread-safety:
 * - Uses static allocation with 8-byte alignment to prevent UNALIGNED faults
 * - Copies strings byte-by-byte into fixed buffers for multicore safety
 * - time_us_32() is atomic (single register read, multicore-safe)
 * - Non-blocking read operations prevent Core 1 blocking during display updates
 */
void log_diagnostic(diagnostic_level_t level, const char* component, const char* message) {
    // Validate input pointers to prevent crash from NULL or invalid pointers
    if (!component || !message || !g_diag_log_buffer.initialized) {
        return;  // Silently ignore invalid calls
    }
    
    // Static allocation to avoid stack issues, 8-byte aligned for ARM Cortex-M safety
    static diagnostic_msg_t msg __attribute__((aligned(8)));
    
    msg.level = level;
#ifdef TEST_BUILD
    msg.timestamp = mock_time_ms;  // Use mock time in test mode
#else
    msg.timestamp = time_us_32() / 1000;  // Convert to milliseconds
#endif
    
    // Use safer string copy with explicit length check
    size_t comp_len = 0;
    size_t msg_len = 0;
    
    // Count component string length (with safety limit)
    for (size_t i = 0; i < DIAG_COMPONENT_MAX_LEN && component[i] != '\0'; i++) {
        comp_len = i + 1;
    }
    
    // Count message string length (with safety limit)
    for (size_t i = 0; i < DIAG_MESSAGE_MAX_LEN && message[i] != '\0'; i++) {
        msg_len = i + 1;
    }
    
    // Copy strings byte-by-byte instead of using strncpy
    for (size_t i = 0; i < DIAG_COMPONENT_MAX_LEN - 1; i++) {
        if (i < comp_len) {
            msg.component[i] = component[i];
        } else {
            msg.component[i] = '\0';
        }
    }
    msg.component[DIAG_COMPONENT_MAX_LEN - 1] = '\0';
    
    for (size_t i = 0; i < DIAG_MESSAGE_MAX_LEN - 1; i++) {
        if (i < msg_len) {
            msg.message[i] = message[i];
        } else {
            msg.message[i] = '\0';
        }
    }
    msg.message[DIAG_MESSAGE_MAX_LEN - 1] = '\0';
    
    diag_log_add(msg);
    
#ifdef TEST_BUILD
    // For testing: Output critical messages to UART for test validation
    if (level >= DIAG_CRITICAL) {
        // Format: "CRITICAL:<COMPONENT>:<MESSAGE>"
        char diagnostic_output[256];
        snprintf(diagnostic_output, sizeof(diagnostic_output),
                 "CRITICAL:%s:%s\n", component, message);
        uart_puts(uart0, diagnostic_output);
    }
#endif
}
