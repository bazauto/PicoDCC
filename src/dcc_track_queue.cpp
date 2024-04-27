#include <time.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <pico/multicore.h>
#include <hardware/pio.h>

#include "dcc.pio.h"
#include "dcc_track_queue.h"

static bool is_main_track = 1;
static loco_t locos[MAX_LOCO];
static uint8_t loco_count = 0;
static int8_t current_loco = -1;
static queue_t *local_cmd_queue;

clock_t clock()
{
    return (clock_t) time_us_64();
}

void dcc_track_init(queue_t *cmd_queue, bool is_main) {
    local_cmd_queue = cmd_queue;
    is_main_track = is_main;
    queue_init(cmd_queue, sizeof(queue_cmd_t), 5);
    multicore_launch_core1(dcc_track_runner);
}

void dcc_track_runner() {
	// Choose which PIO instance to use (there are two instances)
	PIO pio = pio0;
	uint offset = pio_add_program(pio, &dcc_program);
	uint sm = pio_claim_unused_sm(pio, true);
	dcc_program_init(pio, sm, offset, 18, 13); // Main line preamble length
    pio_sm_set_enabled(pio, sm, true);

	while (true) {
        
		queue_cmd_t cmd;
		bool gotCmd = queue_try_remove(local_cmd_queue, &cmd);

		if (gotCmd) {
            if (loco_count == 0) {
                // Add first loco
                locos[loco_count].id = cmd.data[0];
                locos[loco_count].dir_speed = cmd.data[1];
                loco_count++;
                current_loco = 0;   // init the pointer
            } else {
                // Look for existing loco
                for (uint8_t i = 0; i < loco_count; i++) {
                    if (locos[i].id == cmd.data[0]) {
                        // Update it
                        locos[i].dir_speed = cmd.data[1];
                    } else {
                        // we don't have it add new
                        locos[loco_count].id = cmd.data[0];
                        locos[loco_count].dir_speed = cmd.data[1];
                        loco_count++;
                    }
                }
            }
		} else {
            if (loco_count > 0) {
                queue_cmd_t loco_cmd = {0x02, { locos[current_loco].id, locos[current_loco].dir_speed}};
                cmd = loco_cmd;

                current_loco++;
                if (current_loco > loco_count - 1) {
                    current_loco = 0;
                }
            } else {
                // No locos send idle
                queue_cmd_t idle_cmd = {0x03, { 0xFF, 0x00, 0xFF }};
                cmd = idle_cmd;
            }
        }

		// build the data and checksum to send to the PIO
		uint64_t cmd_data = (uint64_t)0;
        if (is_main_track)
            cmd_data |= ((uint64_t)14) << 56;
        else
            cmd_data |= ((uint64_t)20) << 56;

        cmd_data |= ((uint64_t)cmd.length + 1) << 48;
		uint8_t cmd_xor = 0x0;
		for (uint8_t i = 0; i < cmd.length; i++) {
			uint8_t shift = 5 - i; // 5 is the max number of bytes, this is to MSB align the bytes
			cmd_data |= ((uint64_t)cmd.data[i] << (shift * 8));
			cmd_xor ^= cmd.data[i];
		}
        // Add the checksum
		cmd_data |= ((uint64_t)cmd_xor << ((5 - cmd.length) * 8));

        pio_sm_put_blocking(pio0, 0, (cmd_data >> 32) & 0xFFFFFFFF);
        if (cmd.length + 1 > 2) {
    		pio_sm_put_blocking(pio0, 0, cmd_data & 0xFFFFFFFF);
        }
	}
}