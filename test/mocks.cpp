extern "C" {
#include "mocks.h"
}

void queue_init(queue_t *queue, size_t item_size, size_t max_items) {}
bool queue_add_blocking(queue_t *queue, const void *item) { return true; }
bool queue_try_add(queue_t *queue, const void *item) { return true; }
bool queue_try_remove(queue_t *queue, void *item) { return true; }

void adc_init() {}
void adc_gpio_init(uint8_t gpio) {}
void adc_select_input(uint8_t adc_num) {}
uint adc_read() { return 0; }

int uart0_data;
void* uart0 = &uart0_data;

void setup_default_uart() {}
void uart_puts(void *uart, const char *str) {}
char uart_getc(void *uart) { return 'a'; }
bool uart_is_writable(void *uart) { return true; }
bool uart_is_readable(void *uart) { return true; }

PIO::PIO() {}
PIO::PIO(void *) {}

PIO pio0_instance;
PIO pio1_instance;
int prog_data;

void* pio0 = &pio0_instance;
void* pio1 = &pio1_instance;
void* dcc_program = &prog_data;

uint pio_claim_unused_sm(PIO pio, bool require_unused) { return 0; }
int pio_add_program(PIO pio, void *program) { return 0; }
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {}
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data) {}
void dcc_program_init(PIO pio, uint sm, uint offset, uint signal_pin, uint preamble) {}

void gpio_put(uint8_t gpio, bool value) {}
void gpio_set_dir(uint8_t gpio, bool out) {}
void gpio_init(uint8_t gpio) {}

void sem_init(semaphore_t *sem, int count, int) {}
void sem_acquire(semaphore_t *sem) {}
void sem_acquire_blocking(semaphore_t *sem) {}
void sem_release(semaphore_t *sem) {}

void stdio_init_all() {}
void assert(bool) {}