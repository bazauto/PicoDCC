
#include <vector>
#include <string>
#include <cstdint>
// For test validation: record all 64-bit packets sent to the track
extern std::vector<uint64_t> sent_track_packets;
#include <queue>
#include <mutex>
#include <cstring>  // For memcpy
#include <cstdlib> // For malloc and free
#include <chrono>  // For std::chrono::milliseconds
#include <thread>  // For std::this_thread::sleep_for

#include "mocks.h"

std::queue<void *> mock_queue;
std::mutex mock_queue_mutex;
std::queue<char> uart_buffer; // UART buffer for testing
std::array<bool, 30> gpio_states = {false}; // Initialize all GPIO pins to 0
int uart0_data = 0;
void* uart0 = &uart0_data;

// Additional mocks for track testing
extern uint32_t mock_adc_reading;
extern uint32_t mock_time_ms;

// UART output tracking for acknowledgment testing
extern std::vector<std::string> uart_output_log;

extern "C" {

void queue_init(queue_t *queue, size_t item_size, size_t max_items) {
    // Clear the mock queue on initialization
    std::lock_guard<std::mutex> lock(mock_queue_mutex);
    while (!mock_queue.empty()) {
        void* item = mock_queue.front();
        free(item);
        mock_queue.pop();
    }

    queue->item_size = item_size;
    queue->max_items = max_items;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

bool queue_add_blocking(queue_t *queue, const void *item) {
    std::lock_guard<std::mutex> lock(mock_queue_mutex);
    raw_dcc_cmd_t* cmd = (raw_dcc_cmd_t*)malloc(sizeof(raw_dcc_cmd_t));
    if (cmd == nullptr) {
        return false;
    }
    memcpy(cmd, item, sizeof(raw_dcc_cmd_t));
    mock_queue.push(cmd);
    // Also push to test-observable vector
    extern std::vector<raw_dcc_cmd_t> queued_commands;
    queued_commands.push_back(*cmd);
    return true;
}

bool queue_try_add(queue_t *queue, const void *item) {
    return queue_add_blocking(queue, item);
}

bool queue_try_remove(queue_t *queue, void *item) {
    std::lock_guard<std::mutex> lock(mock_queue_mutex);
    if (mock_queue.empty()) {
        return false;
    }
    void *cmd = mock_queue.front();
    memcpy(item, cmd, sizeof(raw_dcc_cmd_t));
    free(cmd);
    mock_queue.pop();
    // Also pop from test-observable vector if not empty
    extern std::vector<raw_dcc_cmd_t> queued_commands;
    if (!queued_commands.empty()) {
        queued_commands.erase(queued_commands.begin());
    }
    return true;
}

void adc_init() {}
void adc_gpio_init(uint8_t gpio) {}
void adc_select_input(uint8_t adc_num) {}
uint adc_read() { return mock_adc_reading; }

void setup_default_uart() {}

void uart_puts(void *uart, const char *str) {
    // Log UART output for testing acknowledgments
    uart_output_log.push_back(std::string(str));
}

char uart_getc(void *uart) { 
    if (!uart_buffer.empty()) {
        char c = uart_buffer.front();
        uart_buffer.pop();
        return c;
    }
    return '\0';
}

void uart_test_write(const char* str) {
    while (*str) {
        uart_buffer.push(*str);
        str++;
    }
}

bool uart_is_writable(void *uart) { return true; }

bool uart_is_readable(void *uart) {
    bool readable = !uart_buffer.empty();
    return readable;
}

void gpio_put(uint8_t gpio, bool value) {
    if (gpio < gpio_states.size()) {
        gpio_states[gpio] = value;
    }
    // Track power state for test assertions (22=main, 21=prog)
    extern bool track_power_states[2];
    if (gpio == 22) track_power_states[0] = value; // main track
    if (gpio == 21) track_power_states[1] = value; // prog track
}
void gpio_set_dir(uint8_t gpio, bool out) {}
void gpio_init(uint8_t gpio) {}

void sem_init(semaphore_t *sem, int count, int) {}
void sem_acquire(semaphore_t *sem) {}
void sem_acquire_blocking(semaphore_t *sem) {}
void sem_release(semaphore_t *sem) {}

void stdio_init_all() {}
void assert(bool) {}

absolute_time_t get_absolute_time(void) {
    return static_cast<absolute_time_t>(mock_time_ms);
}

uint32_t to_ms_since_boot(absolute_time_t t) {
    return static_cast<uint32_t>(t);
}

absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return static_cast<absolute_time_t>(mock_time_ms + ms);
}

bool time_reached(absolute_time_t t) {
    return mock_time_ms >= static_cast<uint32_t>(t);
}

void sleep_us(uint64_t us) {
    // In test mode, just advance mock time instead of actual sleep
    // This prevents tests from running slowly
    mock_time_ms += static_cast<uint32_t>(us / 1000);
}

} // extern "C"

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
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data) {
    // Track packets can be sent as either single or double 32-bit words
    static uint32_t high_word = 0;
    static bool waiting_for_low_word = false;
    static int call_count = 0;
    
    if (!waiting_for_low_word) {
        high_word = data;
        waiting_for_low_word = true;
        call_count = 1;
    } else {
        uint32_t low_word = data;
        uint64_t full_packet = ((uint64_t)high_word << 32) | low_word;
        sent_track_packets.push_back(full_packet);
        waiting_for_low_word = false;
        call_count = 0;
    }
}

// Helper function to simulate single-word packet completion
void pio_complete_single_word_packet() {
    static uint32_t high_word = 0;
    static bool waiting_for_low_word = false;
    
    if (waiting_for_low_word) {
        // Complete with just the high word (single-word packet)
        uint64_t single_packet = ((uint64_t)high_word << 32);
        sent_track_packets.push_back(single_packet);
        waiting_for_low_word = false;
    }
}
void dcc_program_init(PIO pio, uint sm, uint offset, uint signal_pin, uint preamble) {}