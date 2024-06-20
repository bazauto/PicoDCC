#include "pico_dcctrack.h"

#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/pio.h>

#include "dcc.pio.h"

PicoDccTrack::PicoDccTrack(bool is_prog_in) {
    is_prog = is_prog_in;
}

void PicoDccTrack::setPowerCtrlPin(uint8_t pin) {
    assert(state == TRACK_UNINIT);

    power_ctrl_pin = pin;
}

void PicoDccTrack::setPowerAdcNumber(uint8_t adc) {
    assert(state == TRACK_UNINIT);

    power_adc_number = adc;
    power_adc_pin = BASE_ADC_PIN + adc;
}

void PicoDccTrack::init(track_settings_t settings) {
    assert(settings.ctrl_pin != UNUSED_PIN);
    
    setPowerCtrlPin(settings.ctrl_pin);
    if (settings.adc_num != UNUSED_PIN)
        setPowerAdcNumber(settings.adc_num);

    init();
}

void PicoDccTrack::init() {
    assert(power_ctrl_pin != UNUSED_PIN);
    assert(state == TRACK_UNINIT);

    // Setup Track power control PIN, ensuring off initially
	gpio_init(power_ctrl_pin);
	gpio_set_dir(power_ctrl_pin, GPIO_OUT);
	gpio_put(power_ctrl_pin, 0);

    // Setup for current reading
    if (canReadCurrent())
    {
        adc_init();
        adc_gpio_init( power_adc_pin);
        adc_select_input( power_adc_number);
    }

    // Setup the PIO to run the track signal
    // We use 0 for prog track and 1 for main track
    if (is_prog)
        pio = pio0;
    else
        pio = pio1;

	uint offset = pio_add_program((PIO)pio, &dcc_program);
	uint sm = pio_claim_unused_sm((PIO)pio, true);
    assert(sm != -1); // This should never happen

	dcc_program_init((PIO)pio, sm, offset, 18, (is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE));
    pio_sm_set_enabled((PIO)pio, sm, true);

    state = TRACK_IDLE;
}

void PicoDccTrack::setPower(bool power_on) {
    assert(state != TRACK_UNINIT);

    gpio_put(power_ctrl_pin, power_on);
}

void PicoDccTrack::loop() {
    // Process current reading
    if (canReadCurrent()) {
        if (current_cnt++ >= TRACK_POWER_CURRENT_SAMPLES) {
            average_current_reading = current_sum / current_cnt;
            current_cnt = 0;
            current_sum = 0;
        } else {
            current_sum += adc_read();
        }
    }

    // Process any incoming messages to be sent to the track

}

void PicoDccTrack::processCmd(raw_dcc_cmd_t *cmd)
{
    // build the data and checksum to send to the PIO
    uint64_t cmd_data = (uint64_t)0;
    cmd_data |= ((uint64_t)(is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE)) << 56;
    cmd_data |= ((uint64_t)cmd->length + 1) << 48;
    uint8_t cmd_xor = 0x0;
    for (uint8_t i = 0; i < cmd->length; i++) {
        uint8_t shift = (DCC_MAX_DATA_BYTES - 1) - i;
        cmd_data |= ((uint64_t)cmd->data[i] << (shift * 8));
        cmd_xor ^= cmd->data[i];
    }
    // Add the checksum
    cmd_data |= ((uint64_t)cmd_xor << (((DCC_MAX_DATA_BYTES - 1) - cmd->length) * 8));

    pio_sm_put_blocking((PIO)pio, 0, (cmd_data >> 32) & 0xFFFFFFFF);
    if (cmd->length + 1 > 2) {
        pio_sm_put_blocking((PIO)pio, 0, cmd_data & 0xFFFFFFFF);
    }
}

void PicoDccTrack::sendIdle()
{
    // This is the Idle packet
    raw_dcc_cmd_t idle_cmd = {is_prog, 0x03, { 0xFF, 0x00, 0xFF }};
    processCmd(&idle_cmd);
}

void PicoDccTrack::sendLocoSpeed(uint16_t addr, uint8_t speed, bool forward)
{
    // Build the raw command 
    raw_dcc_cmd_t idle_cmd = {is_prog, 0x03, { 0xFF, 0x00, 0xFF }};
    processCmd(&idle_cmd);
}
