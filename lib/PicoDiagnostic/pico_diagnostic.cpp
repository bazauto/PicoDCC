/* lib/pico_diagnostic.cpp */
#include "pico_diagnostic.h"
#include <string.h>

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
    
    // Clear all entries
    memset(g_diag_log_buffer.entries, 0, sizeof(g_diag_log_buffer.entries));
}

/**
 * @brief Add a diagnostic message to the circular buffer
 * @param msg Diagnostic message to store
 * 
 * Implements circular buffer logic:
 * - Newest messages overwrite oldest when buffer is full
 * - Thread-safe for single writer (can be called from either core)
 * - No dynamic memory allocation
 */
void diag_log_add(diagnostic_msg_t msg) {
    if (!g_diag_log_buffer.initialized) {
        return;  // Buffer not initialized, ignore
    }
    
    // Add message at head position
    g_diag_log_buffer.entries[g_diag_log_buffer.head] = msg;
    
    // Advance head (circular wrap-around)
    g_diag_log_buffer.head = (g_diag_log_buffer.head + 1) % DIAG_LOG_BUFFER_SIZE;
    
    // Update count (saturate at buffer size)
    if (g_diag_log_buffer.count < DIAG_LOG_BUFFER_SIZE) {
        g_diag_log_buffer.count++;
    }
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
 * @brief Retrieve a log entry by index
 * @param index Entry index (0 = oldest, count-1 = newest)
 * @return Pointer to log entry, or NULL if invalid index
 * 
 * Index mapping:
 * - index 0 = oldest entry (first to be overwritten)
 * - index (count-1) = newest entry (most recently added)
 */
diagnostic_msg_t* diag_log_get_entry(uint8_t index) {
    if (!g_diag_log_buffer.initialized || index >= g_diag_log_buffer.count) {
        return nullptr;  // Invalid index
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
    
    return &g_diag_log_buffer.entries[position];
}

/**
 * @brief Clear all log entries from the buffer
 * Resets the buffer to empty state without de-initializing
 */
void diag_log_clear(void) {
    if (!g_diag_log_buffer.initialized) {
        return;
    }
    
    g_diag_log_buffer.head = 0;
    g_diag_log_buffer.count = 0;
    
    // Optional: zero out entries for security/debugging
    memset(g_diag_log_buffer.entries, 0, sizeof(g_diag_log_buffer.entries));
}
