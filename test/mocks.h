#ifndef PICO_MOCKS_H
#define PICO_MOCKS_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <mutex>
#include <array>
#include <vector>

#include "dcc_types.h"

#define GPIO_OUT 1

// GPIO state tracking
extern std::array<bool, 30> gpio_states;

typedef uint32_t absolute_time_t;

// Mock queue handle. Each queue_t owns its own ring buffer, so a test can tell
// the inter-core queue apart from the per-track queues, and so a queue can
// actually reach max_items and refuse an add.
typedef struct
{
    uint8_t *data;
    size_t item_size;
    size_t max_items;
    size_t head;
    size_t tail;
    size_t count;
} queue_t;

typedef unsigned int uint;

#ifdef __cplusplus
// External variables to track
extern bool track_power_states[2];
extern std::vector<raw_dcc_cmd_t> queued_commands;
extern std::vector<uint64_t> sent_track_packets;
extern uint32_t mock_adc_reading;
extern uint32_t mock_time_ms;
extern std::vector<std::string> uart_output_log;

extern "C" {
#endif

// C-compatible function declarations
void queue_init(queue_t *queue, size_t item_size, size_t max_items);
bool queue_add_blocking(queue_t *queue, const void *item);
bool queue_try_add(queue_t *queue, const void *item);
bool queue_try_remove(queue_t *queue, void *item);

void adc_init();
void adc_gpio_init(uint8_t gpio);
void adc_select_input(uint8_t adc_num);
uint adc_read();

extern int uart0_data;
extern void* uart0;

void setup_default_uart();
void uart_puts(void *uart, const char *str);
void uart_test_write(const char* str);
char uart_getc(void *uart);
bool uart_is_writable(void *uart);
bool uart_is_readable(void *uart);

void gpio_put(uint8_t gpio, bool value);
void gpio_set_dir(uint8_t gpio, bool out);
void gpio_init(uint8_t gpio);

typedef struct
{
    int count;
    int held;
} semaphore_t;
void sem_init(semaphore_t *sem, int count, int);
void sem_acquire(semaphore_t *sem);
void sem_acquire_blocking(semaphore_t *sem);
bool sem_try_acquire(semaphore_t *sem);
void sem_release(semaphore_t *sem);

void stdio_init_all();
void assert(bool);

// Time functions
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t t);
void sleep_us(uint64_t us);
uint32_t time_us_32(void);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Test-only mock controls and observations.
//
// These exist so that behaviour the firmware depends on -- which ADC channel is
// selected, whether a lock was contended, whether an assert fired -- is
// observable from a test rather than silently discarded.
// ---------------------------------------------------------------------------

// ADC: per-channel readings.
//
// adc_read() returns the value for whichever channel adc_select_input() last
// selected, so a test can give the main and programming tracks different
// currents. Channels default to mock_adc_reading, preserving the behaviour of
// tests that only set that single global.
void mock_adc_set_channel(uint8_t adc_num, uint32_t reading);
void mock_adc_clear_channels(void);
uint8_t mock_adc_selected_channel(void);
uint32_t mock_adc_select_count(void);

// GPIO: power control pins are registered by the track that owns them, so
// track_power_states[] follows the pins actually passed in rather than a pair
// of hardcoded pin numbers.
void mock_register_power_pin(uint8_t gpio, int track_index);
void mock_clear_power_pins(void);

// assert(): the firmware asserts on conditions that must never hold (pin
// collisions, PIO claim failure). Record them rather than discarding them.
extern uint32_t mock_assert_failures;
void mock_reset_asserts(void);

// Semaphores: modelled as real counting semaphores. The harness is
// single-threaded, so an acquire that would have blocked cannot actually block
// -- it is recorded instead. That makes "this call blocks Core 1" an assertable
// property (see issue #17).
extern uint32_t mock_sem_would_block;
void mock_reset_sem_stats(void);

// PIO: the raw 32-bit words pushed to the state machine, in order, alongside
// the assembled 64-bit packets. Word pairing is derived from the length field
// in the packet header rather than an alternating toggle, so a short packet
// cannot swallow the next packet's first word.
extern std::vector<uint32_t> sent_track_words;
extern std::vector<uint> sent_track_sm;
void mock_reset_pio(void);

// C++ constructs
class PIO
{
private:
    uint sm_mask;
    bool sm_enabled[4];
    void *program;
public:
    PIO();
    PIO(void *);
};

extern PIO pio0_instance;
extern PIO pio1_instance;
extern int prog_data;

extern void* pio0;
extern void* pio1;
extern void* dcc_program;

uint pio_claim_unused_sm(PIO pio, bool require_unused);
int pio_add_program(PIO pio, void *program);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data);
void dcc_program_init(PIO pio, uint sm, uint offset, uint signal_pin, uint preamble);

extern std::mutex mock_queue_mutex; // Declare the mutex as extern

#endif
