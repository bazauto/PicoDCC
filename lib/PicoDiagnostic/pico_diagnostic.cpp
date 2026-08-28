/* lib/pico_diagnostic.cpp */
#include "pico_diagnostic.h"
#include <string.h>
#include <stdio.h>
#include "../dcc_time.h"
#ifndef TEST_BUILD
#include <malloc.h>   // mallinfo(), for the heap telemetry in #38
#endif

// Global log buffer instance - initialized explicitly to avoid C++11 initialization issues
diagnostic_log_buffer_t g_diag_log_buffer;

// Heap sampler state (#38). File scope rather than function-local statics so
// that diag_log_init() owns its lifetime: re-initialising the diagnostic
// subsystem should reset the sampler with it, and a test that clears the log
// should not inherit a rate limit from the test before it.
static uint32_t g_heap_last_sample_ms = 0;
static bool g_heap_sampled_once = false;

/**
 * @brief Initialize the diagnostic log buffer
 * Should be called once during system startup
 */
void diag_log_init(void) {
    g_diag_log_buffer.head = 0;
    g_diag_log_buffer.count = 0;
    g_diag_log_buffer.initialized = true;

    // The heap sampler is part of this subsystem; reset it too.
    g_heap_last_sample_ms = 0;
    g_heap_sampled_once = false;
    
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
/**
 * @brief Claim the slot the next entry will be written into
 * @return Pointer to the slot. Only valid while the buffer lock is held.
 *
 * Must be called with the lock held. Bounds-checks head first, so a corrupted
 * head cannot walk off the array.
 */
static diagnostic_msg_t *diag_slot_for_write(void) {
    if (g_diag_log_buffer.head >= DIAG_LOG_BUFFER_SIZE) {
        g_diag_log_buffer.head = 0;  // Reset to valid range
    }
    return &g_diag_log_buffer.entries[g_diag_log_buffer.head];
}

/**
 * @brief Publish the slot claimed by diag_slot_for_write()
 *
 * Must be called with the lock held, after the slot has been filled. Advancing
 * head is what makes the entry visible to a reader, so it happens last.
 */
static void diag_slot_written(void) {
    g_diag_log_buffer.head = (g_diag_log_buffer.head + 1) % DIAG_LOG_BUFFER_SIZE;

    if (g_diag_log_buffer.count < DIAG_LOG_BUFFER_SIZE) {
        g_diag_log_buffer.count++;
    }
}

/**
 * @brief Copy a string into a fixed-size log field
 *
 * Byte by byte, never strncpy() -- rule 4 in CLAUDE.md, which has already cost
 * an UNALIGNED fault on hardware. The tail is zero-filled rather than merely
 * NUL-terminated: slots are reused as the buffer wraps, so bytes past the
 * terminator would otherwise still hold the previous entry's text.
 */
static void diag_copy_field(char *dest, const char *src, size_t dest_size) {
    size_t i = 0;
    for (; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < dest_size; i++) {
        dest[i] = '\0';
    }
}

void diag_log_add(const diagnostic_msg_t* msg) {
    if (!g_diag_log_buffer.initialized || !msg) {
        return;  // Buffer not initialized or nothing to add, ignore
    }

#ifndef TEST_BUILD
    // Try, never wait. LOG_CRITICAL reaches here from PicoDccTrack::checkPIOHealth() and
    // PicoDccController::dccLoop(), both on Core 1 in the DCC hot path, so a blocking
    // acquire here makes *logging itself* a timing hazard (rule 4) -- and that penalises
    // exactly the diagnostics the design wants most, at exactly the moment a fault is
    // being reported.
    //
    // On contention the entry is dropped. That is the deliberate trade: the only other
    // party holding this lock is the Core 0 display reader, which holds it for a bounded
    // copy, so a drop means one log line lost to a 10 Hz display refresh. A stalled DCC
    // signal costs a great deal more than a log line -- see rule 1.
    if (!sem_try_acquire(&g_diag_log_buffer.sem)) {
        return;
    }
#endif
    
    // memcpy, not struct assignment -- the same rule the read path in
    // diag_log_get_entry() documents and follows (rule 4).
    memcpy(diag_slot_for_write(), msg, sizeof(diagnostic_msg_t));
    diag_slot_written();

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
 * - The entry is built directly in the ring slot, inside the lock, so there is
 *   no intermediate buffer for the other core to interleave with (#18)
 * - Copies strings byte-by-byte into fixed buffers for multicore safety
 * - dcc_millis() is a latched read of the hardware timer, multicore-safe
 * - Non-blocking read operations prevent Core 1 blocking during display updates
 */
void log_diagnostic(diagnostic_level_t level, const char* component, const char* message) {
    // Validate input pointers to prevent crash from NULL or invalid pointers
    if (!component || !message || !g_diag_log_buffer.initialized) {
        return;  // Silently ignore invalid calls
    }

#ifndef TEST_BUILD
    // Try, never wait -- for the reasons diag_log_add() sets out at length. On
    // contention the entry is dropped rather than stalling Core 1.
    if (!sem_try_acquire(&g_diag_log_buffer.sem)) {
        return;
    }
#endif

    // Built directly into the slot, under the lock.
    //
    // This used to be assembled in a function-level `static diagnostic_msg_t`
    // shared by both cores, *before* the lock was taken, and then copied into
    // the ring (#18). Core 0 and Core 1 both log, so a Core 1 call landing
    // midway through Core 0's copy loops committed a hybrid entry: one core's
    // level and timestamp against the other's component or message. That is a
    // record of an event that never happened, and it was least trustworthy
    // exactly when the log matters most -- a timing violation logs CRITICAL
    // from Core 1 every 10ms while Core 0 logs its response to the same fault.
    //
    // Building in place also removes two of the three copies the old path made
    // of an 88-byte struct: into the static, into diag_log_add's by-value
    // parameter, then into the ring.
    diagnostic_msg_t *entry = diag_slot_for_write();

    entry->level = level;
    entry->timestamp = dcc_millis();
    diag_copy_field(entry->component, component, DIAG_COMPONENT_MAX_LEN);
    diag_copy_field(entry->message, message, DIAG_MESSAGE_MAX_LEN);

    diag_slot_written();

#ifndef TEST_BUILD
    sem_release(&g_diag_log_buffer.sem);
#endif

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

/**
 * @brief Read the current heap statistics
 *
 * Split on TEST_BUILD because the mechanism is platform-specific, not because
 * the behaviour differs: newlib's mallinfo() exists on the target, and the host
 * build runs against UCRT, which has no equivalent. Rule 3 allows exactly this
 * -- hardware abstraction -- and nothing downstream branches on the build.
 */
bool diag_read_heap(heap_stats_t* out) {
    if (!out) {
        return false;
    }

#ifdef TEST_BUILD
    return mock_read_heap(&out->used, &out->bytes_free, &out->arena);
#else
    struct mallinfo mi = mallinfo();
    out->used = (uint32_t)mi.uordblks;
    out->bytes_free = (uint32_t)mi.fordblks;
    out->arena = (uint32_t)mi.arena;
    return true;
#endif
}

void diag_sample_heap(void) {
    const uint32_t now = dcc_millis();

    // The first call always emits, so a bench session sees the baseline
    // immediately rather than waiting out a whole interval to learn whether the
    // sampler works at all. Unsigned delta, never an absolute comparison
    // (rule 7).
    if (g_heap_sampled_once &&
        (now - g_heap_last_sample_ms) < DIAG_HEAP_SAMPLE_INTERVAL_MS) {
        return;
    }

    heap_stats_t heap;
    if (!diag_read_heap(&heap)) {
        return;  // Platform cannot report it; say nothing rather than log zeroes
    }

    g_heap_sampled_once = true;
    g_heap_last_sample_ms = now;

    // Static, not a stack buffer, per rule 4. Core 0 only, and only once per
    // interval, so there is no sharing hazard of the kind #18 describes.
    static char heap_msg[DIAG_MESSAGE_MAX_LEN] __attribute__((aligned(8)));
    snprintf(heap_msg, sizeof(heap_msg), "heap used=%u free=%u arena=%u",
             (unsigned)heap.used, (unsigned)heap.bytes_free, (unsigned)heap.arena);
    LOG_INFO(COMPONENT_SYSTEM, heap_msg);
}
