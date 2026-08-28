#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include "../lib/PicoDCCEX/pico_dccex.h"
#include "../lib/PicoDCCController/pico_dcccontroller.h"
#include "../lib/PicoDCCDisplay/pico_dcc_display.h"
#include "../lib/PicoDCCDisplay/lcd_driver.h"
#include "../lib/PicoDCCDisplay/touch_driver.h"
#include "../lib/PicoDCCDisplay/lvgl_renderer.h"
#include "../lib/pico_diagnostic.h"

#define TRACK_MAIN_SHORT_LED 16
#define TRACK_MAIN_SIGNAL_PIN 17
#define TRACK_MAIN_POWER_CTRL_PIN 18
#define TRACK_MAIN_POWER_ADC_NUM 0

#define TRACK_PROG_SHORT_LED 19
#define TRACK_PROG_SIGNAL_PIN 20
#define TRACK_PROG_POWER_CTRL_PIN 21
#define TRACK_PROG_POWER_ADC_NUM 1

#define TIMING_ERROR_LED_PIN 25  // Built-in LED on Pico

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) // Last sector

// Our controller instance. Constructed inside main(), after diag_log_init(),
// and pointed at here so main_core1() can reach it -- see the note in main().
static PicoDccController *pico_controller = nullptr;

static void main_core1()
{
	// This thread will pause during our Flash write operation
	multicore_lockout_victim_init();

	while(true)
	{
		pico_controller->dccLoop();
	}
}

int main() {

	// FIRST, before anything else touches shared state.
	//
	// multicore_launch_core1() documents that "core 1 must previously have been
	// reset either as a result of a system reset or by calling
	// multicore_reset_core1". Relying on the system reset alone is not safe: a
	// reset that restarts Core 0 does not always stop Core 1, which then keeps
	// running the *previous* image -- its own loop, calling dccLoop() and driving
	// the PIO -- against BSS that Core 0 is in the middle of re-zeroing.
	//
	// It has to be here rather than beside multicore_launch_core1(). By the time
	// main() reaches the launch, a stale Core 1 has already been running for the
	// whole of display.init(), which is 546ms of ST7789 reset delays. Resetting
	// it there is far too late; the damage is done during the window.
	//
	// The symptom was a "DCC timing violation" on roughly half of all boots (#80):
	// the stale Core 1 logged and ran, the launch then reset and restarted it, and
	// the gap across that restart -- measured with statics that survive in BSS --
	// looked exactly like Core 1 having stalled for 546ms. Six consecutive boots
	// were clean with this call in place, against about a one-in-two failure rate
	// without it.
	multicore_reset_core1();

	stdio_init_all();
	
	// Initialize diagnostic log buffer
	diag_log_init();

	// Constructed here rather than at file scope, so that it is built *after*
	// diag_log_init() (#46).
	//
	// As a file-scope static its constructor ran during dynamic initialisation,
	// before main(). log_diagnostic() returns early while the buffer is not
	// initialised, and diag_log_init() then memsets the entry array, so an entry
	// logged that early would have been wiped even if the flag had been set.
	// Its own "PicoDCCController initialized in NORMAL mode" banner, and any
	// diagnostic PicoConfigStorage::load() emits, were dropped on every boot.
	//
	// That cost more than one line. Nothing on the happy path logs, so an empty
	// diagnostic screen was the normal state and could not be told apart from a
	// log buffer or LCD log view that was not working -- "no entries" was not
	// evidence of a healthy system, it was an absence of evidence either way.
	// The banner now lands, so an empty log is unambiguously a fault in the
	// logging.
	//
	// Ordering is explicit here rather than dependent on when dynamic
	// initialisation happens to run, which across translation units is
	// unspecified.
	static PicoDccController controller(
		(track_settings_t){TRACK_MAIN_SIGNAL_PIN, TRACK_MAIN_POWER_CTRL_PIN, TRACK_MAIN_POWER_ADC_NUM, TRACK_MAIN_SHORT_LED},
		(track_settings_t){TRACK_PROG_SIGNAL_PIN, TRACK_PROG_POWER_CTRL_PIN, TRACK_PROG_POWER_ADC_NUM, TRACK_PROG_SHORT_LED},
		TIMING_ERROR_LED_PIN
		);
	pico_controller = &controller;

	// Initialize LCD display with dependency injection (hardware mode only)
	LcdDriver lcd;
	TouchDriver touch;
	LvglRenderer renderer(lcd, touch);
	PicoDCCDisplay display(lcd, renderer);
	
	if (!display.init()) {
		printf("ERROR: LCD initialization failed\n");
	} else {
		display.runBootSequence();
	}

	// Start our core 1 loop
    multicore_launch_core1(main_core1);

	// This is our core 0 loop
	while (true)
	{
		pico_controller->dccexLoop();
		display.loop(pico_controller);
	}
}

