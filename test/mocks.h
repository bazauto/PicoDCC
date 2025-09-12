#ifndef PICO_MOCKS_H
#define PICO_MOCKS_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GPIO_OUT 1

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
char uart_getc(void *uart);
bool uart_is_writable(void *uart);
bool uart_is_readable(void *uart);

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

#endif