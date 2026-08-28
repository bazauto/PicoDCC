#ifndef PICO_DCC_DIAGNOSTIC_H
#define PICO_DCC_DIAGNOSTIC_H

#include <string.h>

#ifdef TEST_BUILD
#include "../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/sem.h>
#include <pico/sync.h>
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
#define DIAG_COMPONENT_MAX_LEN 16  // e.g., "CONTROLLER"
#define DIAG_MESSAGE_MAX_LEN 64     // e.g., "Overcurrent protection activated"

typedef struct {
    diagnostic_level_t level;
    uint32_t timestamp;
    char component[DIAG_COMPONENT_MAX_LEN];  // Fixed-size buffer for thread-safety
    char message[DIAG_MESSAGE_MAX_LEN];      // Fixed-size buffer for thread-safety
} diagnostic_msg_t;

// Log buffer configuration
#define DIAG_LOG_BUFFER_SIZE 30  // Number of log entries to store
                                  // Each entry: 4 (level) + 4 (timestamp) + 16 (component) + 64 (message) = 88 bytes
                                  // Total: 30 × 88 = ~2.6KB

// Circular buffer for diagnostic log storage
typedef struct {
    diagnostic_msg_t entries[DIAG_LOG_BUFFER_SIZE];
    uint8_t head;           // Next write position
    uint8_t count;          // Number of valid entries (0 to DIAG_LOG_BUFFER_SIZE)
    bool initialized;       // Buffer initialization flag
#ifndef TEST_BUILD
    semaphore_t sem;        // Multi-core synchronization
#endif
} diagnostic_log_buffer_t;

// Global log buffer (initialized in diag_log_init)
extern diagnostic_log_buffer_t g_diag_log_buffer;

/**
 * Heap telemetry (#38)
 *
 * The DCC-EX command path allocates per received command, and the controller's
 * main_cmd_queue is a std::queue<raw_dcc_cmd_t> -- a std::deque underneath, which
 * allocates its blocks. On a target with no MMU, where the heap grows towards the
 * stack and there is no allocation-failure handling, the question is whether the
 * high-water mark is flat or creeping over a long session.
 *
 * `used` answers "is anything leaking". `arena` answers "is it fragmenting":
 * the arena is what has been claimed from the system and never returns, so an
 * arena that climbs while `used` stays flat is fragmentation, which is the
 * outcome #38 is actually worried about.
 */
typedef struct {
    uint32_t used;   // bytes currently allocated
    uint32_t bytes_free;  // bytes free inside the arena
    uint32_t arena;  // total heap claimed from the system
} heap_stats_t;

// How often diag_sample_heap() emits an entry. Ten minutes gives roughly five
// hours of trend across the 30-entry ring, which is the shape #38 asks for.
// Shorten it for a quick bench check; the ring is the limit, not the sampler.
#define DIAG_HEAP_SAMPLE_INTERVAL_MS (10u * 60u * 1000u)

/**
 * @brief Read the current heap statistics
 * @return false if the platform cannot report them, leaving out untouched
 *
 * The mechanism is platform-specific -- newlib's mallinfo() on the target, a
 * mock on the host -- so it is split on TEST_BUILD. That is hardware
 * abstraction, which rule 3 allows; no behaviour branches on it.
 */
bool diag_read_heap(heap_stats_t* out);

/**
 * @brief Emit one heap entry to the log, at most once per interval
 *
 * Rate-limits itself, so the caller does not own the timing (rule 6). Call it
 * from Core 0 only: mallinfo() walks the free list, which has no place in the
 * Core 1 DCC hot path.
 */
void diag_sample_heap(void);

// Log buffer management functions
void diag_log_init(void);
// Takes a pointer, not a value: the parameter used to be an 88-byte struct
// passed by value, which was one of three copies made of every entry (#18).
void diag_log_add(const diagnostic_msg_t* msg);
uint8_t diag_log_get_count(void);
bool diag_log_get_entry(uint8_t index, diagnostic_msg_t* out_entry);  // Thread-safe copy
void diag_log_clear(void);

/**
 * Core diagnostic logging function
 * Stores messages in circular buffer for LCD display
 * 
 * Implementation notes:
 * - The entry is built directly in the ring slot, inside the lock. It used to be
 *   assembled in a function-level static shared by both cores and then copied in,
 *   so two concurrent calls could commit a hybrid entry (#18)
 * - Byte-by-byte string copy instead of strncpy() to avoid alignment issues
 * - Non-blocking semaphore (sem_try_acquire) prevents Core 1 blocking
 * - Display reads are limited to 20 entries max with conservative buffer management
 * 
 * Thread-safety:
 * - Writer (either core) uses sem_try_acquire and DROPS the entry on contention
 * - Reader (Core 0 display) uses non-blocking semaphore
 * - Neither side can stall Core 1, so logging is never a DCC timing hazard
 *
 * The writer used to block, which contradicted the line above and made LOG_CRITICAL
 * from the Core 1 hot path a timing risk (#17). Dropping a line is the cheaper loss.
 */
void log_diagnostic(diagnostic_level_t level, const char* component, const char* message);

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
#define COMPONENT_SYSTEM     "SYSTEM"

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