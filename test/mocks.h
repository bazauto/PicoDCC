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
void mock_set_ack_pulse(bool enabled, uint32_t start_us, uint32_t duration_us, uint32_t spike_reading);

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
    int mock;
} semaphore_t;
void sem_init(semaphore_t *sem, int count, int);
void sem_acquire(semaphore_t *sem);
void sem_acquire_blocking(semaphore_t *sem);
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