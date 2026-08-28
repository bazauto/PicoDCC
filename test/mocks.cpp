
#include <vector>
#include <string>
#include <cstdint>
// For test validation: record all 64-bit packets sent to the track
extern std::vector<uint64_t> sent_track_packets;
#include <map>
#include <queue>
#include <mutex>
#include <cstring>  // For memcpy
#include <cstdlib> // For malloc and free
#include <chrono>  // For std::chrono::milliseconds
#include <thread>  // For std::this_thread::sleep_for

#include "mocks.h"

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

// Counters for mock observations. Defined here, outside the extern "C" block,
// to match the C++ linkage they are declared with in mocks.h.
uint32_t mock_sem_would_block = 0;
uint32_t mock_assert_failures = 0;

// ---------------------------------------------------------------------------
// Queues
//
// Every queue_t gets its own storage. Previously all queues shared one global
// std::queue, which meant the inter-core queue and both per-track queues were
// the same object: main-vs-prog routing was unobservable, capacity was
// unbounded so queue_try_add never failed, and each queue_init wiped whatever
// the previously constructed queue held.
// ---------------------------------------------------------------------------

namespace {

struct MockQueueStore {
    std::vector<uint8_t> storage;
};

// Keyed by queue_t address. Held by the mock rather than the queue_t itself so
// that queue_t stays a plain C struct matching the SDK's shape.
std::map<queue_t *, MockQueueStore> &queue_stores()
{
    static std::map<queue_t *, MockQueueStore> stores;
    return stores;
}

} // namespace

void queue_init(queue_t *queue, size_t item_size, size_t max_items)
{
    std::lock_guard<std::mutex> lock(mock_queue_mutex);

    queue->item_size = item_size;
    queue->max_items = max_items;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    MockQueueStore &store = queue_stores()[queue];
    store.storage.assign(item_size * max_items, 0);
    queue->data = store.storage.data();
}

bool queue_try_add(queue_t *queue, const void *item)
{
    std::lock_guard<std::mutex> lock(mock_queue_mutex);

    if (queue->data == nullptr || queue->count >= queue->max_items) {
        return false;  // Queue full -- the SDK's queue_try_add returns false here
    }

    memcpy(queue->data + (queue->tail * queue->item_size), item, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->max_items;
    queue->count++;

    // Test-observable mirror of everything that has been queued.
    if (queue->item_size == sizeof(raw_dcc_cmd_t)) {
        extern std::vector<raw_dcc_cmd_t> queued_commands;
        raw_dcc_cmd_t cmd;
        memcpy(&cmd, item, sizeof(raw_dcc_cmd_t));
        queued_commands.push_back(cmd);
    }
    return true;
}

bool queue_add_blocking(queue_t *queue, const void *item)
{
    // The real queue_add_blocking spins until there is room. The harness is
    // single-threaded, so nothing could ever drain the queue while we spin --
    // record the would-be block and drop the item rather than hanging the suite.
    if (!queue_try_add(queue, item)) {
        extern uint32_t mock_sem_would_block;
        mock_sem_would_block++;
        return false;
    }
    return true;
}

bool queue_try_remove(queue_t *queue, void *item)
{
    std::lock_guard<std::mutex> lock(mock_queue_mutex);

    if (queue->data == nullptr || queue->count == 0) {
        return false;
    }

    memcpy(item, queue->data + (queue->head * queue->item_size), queue->item_size);
    queue->head = (queue->head + 1) % queue->max_items;
    queue->count--;

    if (queue->item_size == sizeof(raw_dcc_cmd_t)) {
        extern std::vector<raw_dcc_cmd_t> queued_commands;
        if (!queued_commands.empty()) {
            queued_commands.erase(queued_commands.begin());
        }
    }
    return true;
}

extern "C" {

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

namespace {
uint8_t adc_selected = 0;
uint32_t adc_selects = 0;
uint32_t adc_inits = 0;
uint32_t adc_gpio_inits = 0;
bool adc_channel_set[5] = {false, false, false, false, false};
uint32_t adc_channel_value[5] = {0, 0, 0, 0, 0};
} // namespace

void adc_init() { adc_inits++; }
void adc_gpio_init(uint8_t gpio) { adc_gpio_inits++; (void)gpio; }

void adc_select_input(uint8_t adc_num)
{
    adc_selected = adc_num;
    adc_selects++;
}

uint adc_read()
{
    if (adc_selected < 5 && adc_channel_set[adc_selected]) {
        return adc_channel_value[adc_selected];
    }
    // No per-channel value configured: fall back to the single global, which is
    // what tests that do not care about channel routing set.
    return mock_adc_reading;
}

void setup_default_uart() {}

void (*mock_uart_puts_hook)(void) = nullptr;

void uart_puts(void *uart, const char *str) {
    (void)uart;
    // Log UART output for testing acknowledgments
    uart_output_log.push_back(std::string(str));

    // Fires while the write is "in progress". On hardware uart_puts blocks until the
    // bytes are clear, so this is the window in which the rest of the firmware has to
    // keep working -- it is how a test observes what a caller still holds while it
    // writes. Used by the #17 regression test.
    if (mock_uart_puts_hook) {
        mock_uart_puts_hook();
    }
}

char uart_getc(void *uart) {
    (void)uart;
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

bool uart_is_writable(void *uart) { (void)uart; return true; }

bool uart_is_readable(void *uart) {
    (void)uart;
    bool readable = !uart_buffer.empty();
    return readable;
}

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

namespace {
// Power control pin -> track index, registered by whoever owns the pin.
int power_pin_track[30];
bool power_pin_known[30];
bool power_pins_initialised = false;

void init_power_pins()
{
    if (power_pins_initialised) return;
    for (int i = 0; i < 30; i++) {
        power_pin_known[i] = false;
        power_pin_track[i] = -1;
    }
    power_pins_initialised = true;
}
} // namespace

void gpio_put(uint8_t gpio, bool value) {
    if (gpio < gpio_states.size()) {
        gpio_states[gpio] = value;
    }

    init_power_pins();
    extern bool track_power_states[2];
    if (gpio < 30 && power_pin_known[gpio]) {
        int idx = power_pin_track[gpio];
        if (idx == 0 || idx == 1) {
            track_power_states[idx] = value;
        }
    }
}

void gpio_set_dir(uint8_t gpio, bool out) { (void)gpio; (void)out; }
void gpio_init(uint8_t gpio) { (void)gpio; }

// ---------------------------------------------------------------------------
// Semaphores
//
// Modelled as real counting semaphores. The suite is single-threaded so a
// blocking acquire cannot genuinely block; instead the attempt is recorded, so
// a test can assert that a given call path *would* have blocked the other core.
// ---------------------------------------------------------------------------

void sem_init(semaphore_t *sem, int count, int) {
    if (!sem) return;
    sem->count = count;
    sem->held = 0;
}

void sem_acquire(semaphore_t *sem) { sem_acquire_blocking(sem); }

void sem_acquire_blocking(semaphore_t *sem) {
    if (!sem) return;
    if (sem->count <= 0) {
        // Would have blocked the calling core.
        mock_sem_would_block++;
        return;
    }
    sem->count--;
    sem->held++;
}

bool sem_try_acquire(semaphore_t *sem) {
    if (!sem) return false;
    if (sem->count <= 0) {
        return false;
    }
    sem->count--;
    sem->held++;
    return true;
}

void sem_release(semaphore_t *sem) {
    if (!sem) return;
    if (sem->held > 0) {
        sem->held--;
        sem->count++;
    }
}

void stdio_init_all() {}

// ---------------------------------------------------------------------------
// assert()
//
// The firmware asserts on conditions that must never hold -- pin collisions in
// PicoDccController's constructor, PIO state machine claim failure. A no-op
// mock meant none of those were exercised; record them so a test can assert
// that construction is clean, and so a genuinely failing assert is visible.
// ---------------------------------------------------------------------------

void assert(bool condition) {
    if (!condition) {
        mock_assert_failures++;
        fprintf(stderr, "MOCK ASSERT FAILED\n");
    }
}

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
    // Advance mock time instead of actually sleeping, so tests stay fast.
    //
    // Sub-millisecond sleeps must accumulate rather than truncate to zero.
    // PicoDccController's queue-full retry loop sleeps 100us per iteration
    // against a 5ms deadline; truncating each of those to 0ms meant mock time
    // never advanced and the loop never terminated.
    static uint64_t sub_ms_accumulator = 0;
    sub_ms_accumulator += us;
    mock_time_ms += static_cast<uint32_t>(sub_ms_accumulator / 1000);
    sub_ms_accumulator %= 1000;
}

uint32_t time_us_32(void) {
    // Return mock time in microseconds (mock_time_ms is in milliseconds)
    return mock_time_ms * 1000;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Test-only mock controls
// ---------------------------------------------------------------------------

void mock_adc_set_channel(uint8_t adc_num, uint32_t reading)
{
    if (adc_num < 5) {
        adc_channel_set[adc_num] = true;
        adc_channel_value[adc_num] = reading;
    }
}

void mock_adc_clear_channels(void)
{
    for (int i = 0; i < 5; i++) {
        adc_channel_set[i] = false;
        adc_channel_value[i] = 0;
    }
    adc_selected = 0;
    adc_selects = 0;
}

uint8_t mock_adc_selected_channel(void) { return adc_selected; }
uint32_t mock_adc_select_count(void) { return adc_selects; }
uint32_t mock_adc_init_count(void) { return adc_inits; }
uint32_t mock_adc_gpio_init_count(void) { return adc_gpio_inits; }

void mock_adc_reset_init_counts(void)
{
    adc_inits = 0;
    adc_gpio_inits = 0;
}

void mock_register_power_pin(uint8_t gpio, int track_index)
{
    init_power_pins();
    if (gpio < 30) {
        power_pin_known[gpio] = true;
        power_pin_track[gpio] = track_index;
    }
}

void mock_clear_power_pins(void)
{
    power_pins_initialised = false;
    init_power_pins();
}

void mock_reset_asserts(void) { mock_assert_failures = 0; }

void mock_reset_sem_stats(void) { mock_sem_would_block = 0; }

// ---------------------------------------------------------------------------
// PIO
// ---------------------------------------------------------------------------

PIO::PIO() {}
PIO::PIO(void *) {}

PIO pio0_instance;
PIO pio1_instance;
int prog_data;

void* pio0 = &pio0_instance;
void* pio1 = &pio1_instance;
void* dcc_program = &prog_data;

std::vector<uint32_t> sent_track_words;
std::vector<uint> sent_track_sm;

namespace {
uint32_t pio_high_word = 0;
bool pio_awaiting_low_word = false;
} // namespace

uint mock_pio_tx_fifo_level = 0;

uint pio_sm_get_tx_fifo_level(PIO pio, uint sm)
{
    (void)pio; (void)sm;
    return mock_pio_tx_fifo_level;
}

// Deliberately not 0. The claim is dynamic on hardware and there is no reason it
// lands on state machine 0; returning a different index is what makes a caller
// that passes a hardcoded 0 -- as sendCommand did until #35 -- observable at all.
uint pio_claim_unused_sm(PIO pio, bool require_unused) { (void)pio; (void)require_unused; return MOCK_PIO_CLAIMED_SM; }
int pio_add_program(PIO pio, void *program) { (void)pio; (void)program; return 0; }
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) { (void)pio; (void)sm; (void)enabled; }

void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data)
{
    (void)pio;

    sent_track_words.push_back(data);
    sent_track_sm.push_back(sm);

    if (!pio_awaiting_low_word) {
        pio_high_word = data;

        // The packet header carries the byte count (length + 1) at bits 16-23
        // of the high word. PicoDccTrack::sendCommand only pushes a second word
        // when length > 1, so derive the pairing from the header rather than
        // alternating -- otherwise a single-word packet swallows the first word
        // of the next packet and both are recorded as one bogus value.
        uint32_t header_len = (data >> 16) & 0xFF;
        if (header_len > 2) {
            pio_awaiting_low_word = true;
        } else {
            sent_track_packets.push_back((uint64_t)data << 32);
        }
    } else {
        uint64_t full_packet = ((uint64_t)pio_high_word << 32) | data;
        sent_track_packets.push_back(full_packet);
        pio_awaiting_low_word = false;
    }
}

void mock_reset_pio(void)
{
    pio_high_word = 0;
    pio_awaiting_low_word = false;
    mock_pio_tx_fifo_level = 0;
    sent_track_words.clear();
    sent_track_sm.clear();
}

void dcc_program_init(PIO pio, uint sm, uint offset, uint signal_pin, uint preamble)
{
    (void)pio; (void)sm; (void)offset; (void)signal_pin; (void)preamble;
}

// --- Heap telemetry (#38) ---------------------------------------------------
//
// The host build has no mallinfo(), so these stand in for it. Values are set by
// the test rather than derived from the host's real heap: the point is to
// exercise the sampler's rate limiting, formatting and refusal path, none of
// which should depend on what the host allocator happens to be doing.

static uint32_t mock_heap_used = 0;
static uint32_t mock_heap_free = 0;
static uint32_t mock_heap_arena = 0;
static bool mock_heap_available = true;

void mock_set_heap(uint32_t used, uint32_t bytes_free, uint32_t arena) {
    mock_heap_used = used;
    mock_heap_free = bytes_free;
    mock_heap_arena = arena;
}

void mock_set_heap_available(bool available) {
    mock_heap_available = available;
}

bool mock_read_heap(uint32_t* used, uint32_t* bytes_free, uint32_t* arena) {
    if (!mock_heap_available) {
        return false;
    }
    *used = mock_heap_used;
    *bytes_free = mock_heap_free;
    *arena = mock_heap_arena;
    return true;
}

void mock_reset_heap(void) {
    mock_heap_used = 0;
    mock_heap_free = 0;
    mock_heap_arena = 0;
    mock_heap_available = true;
}
