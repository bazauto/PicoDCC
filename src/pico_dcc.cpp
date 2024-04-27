#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

#include "dcc_track_queue.h"
#include "../lib/PicoDCCEX/pico_dccex.h"

#define HIGHEST_SHORT_ADDR 127

#define TRACK_POWER_CTRL_PIN 22
#define TRACK_POWER_ADC_NUM 0
#define TRACK_POWER_ADC_PIN (26 + TRACK_POWER_ADC_NUM)
#define TRACK_POWER_ADC_VREF 3.3
#define TRACK_POWER_ADC_RANGE (1 << 12)
#define TRACK_POWER_ADC_CONVERT (TRACK_POWER_ADC_VREF / (TRACK_POWER_ADC_RANGE - 1))

static float measuredCurrent = 0.0;

static PicoDccExPacket pico_dccex(MAX_LOCO);
static queue_cmd_t working_cmd;
static int sleep_time = 10000;

bool timer_callback( repeating_timer_t *rt ) {
	char a[15];
	snprintf(a, sizeof(a), "%.2f\n", pico_dccex.getCab(), (measuredCurrent * TRACK_POWER_ADC_CONVERT) * 85);
	uart_puts(uart0, a);

	return true;
}

int main() {

	static repeating_timer_t timer;

	stdio_init_all();
	setup_default_uart();

	// Setup Track power control PIN, ensuring off initially
	gpio_init(TRACK_POWER_CTRL_PIN);
	gpio_set_dir(TRACK_POWER_CTRL_PIN, GPIO_OUT);
	gpio_put(TRACK_POWER_CTRL_PIN, 0);

	// Setup for current reading
	adc_init();
	adc_gpio_init( TRACK_POWER_ADC_PIN);
	adc_select_input( TRACK_POWER_ADC_NUM);

	queue_t cmd_queue;
	dcc_track_init(&cmd_queue, false);

	uart_puts(uart0, "<iDCC-EX V-4.0.1 / MEGA / STANDARD_MOTOR_SHIELD / G-9db6d36>\n");
	add_repeating_timer_ms( 5000, &timer_callback, NULL, &timer );
	
	uint cnt = 0;
	uint avgCurrent = 0;
	uint avgCnt = 0;
	while (true) {

		if (avgCnt++ >= 4000) {
			measuredCurrent = avgCurrent / avgCnt;
			avgCnt = 0;
			avgCurrent = 0;
		} else {
			avgCurrent += adc_read();
		}

		// Check for incoming DCCEX character and process
		if (uart_is_readable(uart0)) {
			char newChar = uart_getc(uart0);
			pico_dccex.processInput(newChar);
		}

		// Check if that completed a packet that needs processing
		if (pico_dccex.getProcessState() == pico_dccex_state::PACKET_WAITING) {
			switch(pico_dccex.getOpcode())
			{
				case('0'):
					gpio_put(TRACK_POWER_CTRL_PIN, 0);
					uart_puts(uart0, "<p0>");
					break;

				case('1'):
					gpio_put(TRACK_POWER_CTRL_PIN, 1);
					uart_puts(uart0, "<p1>");
					break;

				case('t'):
					int addr = pico_dccex.getCab();
					queue_cmd_t cmd{0x0, {}};
					if (addr > HIGHEST_SHORT_ADDR) {
						cmd.data[cmd.length++] = (addr >> 8) | 0xc0;
					}
					cmd.data[cmd.length++] = addr & 0xff;
					
					uint8_t speed128 = (pico_dccex.getSpeed() & 0x7f);
					uint8_t speed28 = (speed128*10+36)/46;
					uint8_t code28 = ((speed28+3)/2) | ( (speed28 & 1) ? 0 : 16 );
					cmd.data[cmd.length++] = 64 | code28 | (pico_dccex.getDirection() * 32);

					{
						queue_add_blocking(&cmd_queue, &cmd);

						int8_t responseSpeed = 0;
						if (speed128 == 1)
							responseSpeed = -1;

						if (speed128 > 1)
							speed128 = speed128 - 1;

						speed128 = speed128 | (pico_dccex.getDirection() * 128);

						char s[15];
						snprintf(s, sizeof(s), "<l %d 0 %d 0>", pico_dccex.getCab(), speed128);
						uart_puts(uart0, s);
					}
					break;

				//case ('F'): 
				//	break;
			}
			// Notify that we have processed the packet
			pico_dccex.reset();
		}
	}
}
