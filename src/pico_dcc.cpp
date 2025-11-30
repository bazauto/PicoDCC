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

// Our controller instance
static PicoDccController pico_controller(
	(track_settings_t){TRACK_MAIN_SIGNAL_PIN, TRACK_MAIN_POWER_CTRL_PIN, TRACK_MAIN_POWER_ADC_NUM, TRACK_MAIN_SHORT_LED}, 
	(track_settings_t){TRACK_PROG_SIGNAL_PIN, TRACK_PROG_POWER_CTRL_PIN, TRACK_PROG_POWER_ADC_NUM, TRACK_PROG_SHORT_LED},
	TIMING_ERROR_LED_PIN
	);

static void main_core1()
{
	// This thread will pause during our Flash write operation
	multicore_lockout_victim_init();

	while(true)
	{
		pico_controller.dccLoop();
	}
}

int main() {

	stdio_init_all();

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
	
	// Wait for system to stabilize before enabling diagnostic logging
	// This prevents false errors during startup (Core 1 heartbeat, PIO init, etc.)
	sleep_ms(100);
	
	// Initialize diagnostic log buffer AFTER system stabilization
	diag_log_init();

	// This is our core 0 loop
	while (true)
	{
		pico_controller.dccexLoop();
		display.loop(&pico_controller);
	}
}

