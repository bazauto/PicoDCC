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

#ifndef TEST_BUILD
	// Initialize display
	PicoDCCDisplay display;
	if (!display.init()) {
		printf("ERROR: LCD initialization failed\n");
	} else {
		// Phase 1 test: Show color test pattern for 2 seconds
		display.displayTestPattern();
		sleep_ms(2000);
		
		// Phase 2: Show diagnostic screen
		display.showDiagnosticScreen();
	}
#endif

	// Start our core 1 loop
    multicore_launch_core1(main_core1);

#ifndef TEST_BUILD
	// Track last display update time
	uint32_t last_display_update = 0;
	const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100;  // 10Hz refresh
#endif

	// This is our core 0 loop
	while (true)
	{
		pico_controller.dccexLoop();
		
#ifndef TEST_BUILD
		// Update display at 10Hz
		uint32_t now = time_us_32() / 1000;
		if ((now - last_display_update) >= DISPLAY_UPDATE_INTERVAL_MS) {
			// Gather track status
			TrackStatus status;
			status.main_power_on = pico_controller.isTrackPowerOn(false);
			status.main_current_ma = 0.0f;  // TODO: Get from track
			status.prog_power_on = pico_controller.isTrackPowerOn(true);
			status.prog_current_ma = 0.0f;  // TODO: Get from track
			status.packets_sent = 0;        // TODO: Get from track
			status.idle_packets_sent = 0;   // TODO: Get from track
			status.loco_count = pico_controller.getLocoCount();
			
			display.updateTrackStatus(status);
			display.update();
			
			last_display_update = now;
		}
#endif
	}
}

