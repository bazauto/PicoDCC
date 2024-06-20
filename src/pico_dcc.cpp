#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "pico/multicore.h"

#include "../lib/PicoDCCEX/pico_dccex.h"
#include "../lib/PicoDCCController/pico_dcccontroller.h"

#define TRACK_MAIN_POWER_CTRL_PIN 22
#define TRACK_MAIN_POWER_ADC_NUM 0

#define TRACK_PROG_POWER_CTRL_PIN 22
#define TRACK_PROG_POWER_ADC_NUM 1


// Our controller instance
static PicoDccController pico_controller(
	(track_settings_t){TRACK_MAIN_POWER_CTRL_PIN, TRACK_MAIN_POWER_ADC_NUM}, 
	(track_settings_t){TRACK_PROG_POWER_CTRL_PIN, TRACK_PROG_POWER_ADC_NUM}
	);

static void main_core1()
{
	while(true)
	{
		pico_controller.dccLoop();
	}
}

int main() {

	stdio_init_all();

	// Start our core 1 loop
    multicore_launch_core1(main_core1);

	// This is our core 0 loop
	while (true)
	{
		pico_controller.dccexLoop();
	}
}

