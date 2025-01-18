#include "pico_dcctrack.h"

#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/pio.h>

#include "dcc.pio.h"

PicoDccTrack::PicoDccTrack(bool is_prog_in, track_settings_t settings)
{
    is_prog = is_prog_in;
    power_signal_pin = settings.signal_pin;
    power_ctrl_pin = settings.ctrl_pin;
    if (settings.adc_num != UNUSED_PIN)
    {
        power_adc_number = settings.adc_num;
        power_adc_pin = BASE_ADC_PIN + settings.adc_num;
    }

    // This is the queue of commands to be sent to the track
    queue_init(&cmd_queue, sizeof(raw_dcc_cmd_t), CMD_QUEUE_LENGTH);

    // Setup Track power control PIN, ensuring off initially
    gpio_init(power_ctrl_pin);
    gpio_set_dir(power_ctrl_pin, GPIO_OUT);
    gpio_put(power_ctrl_pin, 0);

    // Setup for current reading
    if (canReadCurrent())
    {
        adc_init();
        adc_gpio_init(power_adc_pin);
        adc_select_input(power_adc_number);
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

    dcc_program_init((PIO)pio, sm, offset, power_signal_pin, (is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE));
    pio_sm_set_enabled((PIO)pio, sm, true);
}

void PicoDccTrack::setPower(bool power_on)
{
    gpio_put(power_ctrl_pin, power_on);
}

void PicoDccTrack::loop()
{
    // Process current reading
    if (canReadCurrent())
    {
        if (current_cnt++ >= TRACK_POWER_CURRENT_SAMPLES)
        {
            average_current_reading = current_sum / current_cnt;
            current_cnt = 0;
            current_sum = 0;
        }
        else
        {
            current_sum += adc_read();
        }
    }

    // Process any incoming messages to be sent to the track
    raw_dcc_cmd_t cmd;
    if (queue_try_remove(&cmd_queue, &cmd))
    {
        sendCommand(&cmd);

        if (cmd.repeats > 0)
        {
            cmd.repeats--;
            queue_add_blocking(&cmd_queue, &cmd);
        }
    }
}

void PicoDccTrack::queueCommand(raw_dcc_cmd_t *cmd)
{
    queue_add_blocking(&cmd_queue, cmd);
}

void PicoDccTrack::sendCommand(raw_dcc_cmd_t *cmd)
{
    if (cmd->cmd_data == 0)
    {
        // build the data and checksum to send to the PIO
        cmd->cmd_data |= ((uint64_t)(is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE)) << 56;
        cmd->cmd_data |= ((uint64_t)cmd->length + 1) << 48;
        uint8_t cmd_xor = 0x0;
        for (uint8_t i = 0; i < cmd->length; i++)
        {
            uint8_t shift = (DCC_MAX_DATA_BYTES - 1) - i;
            cmd->cmd_data |= ((uint64_t)cmd->data[i] << (shift * 8));
            cmd_xor ^= cmd->data[i];
        }
        // Add the checksum
        cmd->cmd_data |= ((uint64_t)cmd_xor << (((DCC_MAX_DATA_BYTES - 1) - cmd->length) * 8));
    }

    pio_sm_put_blocking((PIO)pio, 0, (cmd->cmd_data >> 32) & 0xFFFFFFFF);
    if (cmd->length > 1)
    {
        pio_sm_put_blocking((PIO)pio, 0, cmd->cmd_data & 0xFFFFFFFF);
    }
}

void PicoDccTrack::sendIdle()
{
    // This is the Idle packet
    raw_dcc_cmd_t idle_cmd = {is_prog, 0x03, {0xFF, 0x00, 0xFF}};
    sendCommand(&idle_cmd);
}
