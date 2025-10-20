#include "pico_dcctrack.h"
#include "../pico_diagnostic.h"
#include "../PicoDCCLoco/pico_dcclocos.h"

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#include <vector>
#else
#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/pio.h>
#include <pico/time.h>

#include "dcc.pio.h"
#endif

PicoDccTrack::PicoDccTrack(bool is_prog_in, track_settings_t settings, PicoDccLocos *locos)
{
    is_prog = is_prog_in;
    locos_collection = locos;  // Store reference to locomotive collection
    signal_pin = settings.signal_pin;
    power_ctrl_pin = settings.ctrl_pin;
    if (settings.adc_num != UNUSED_PIN)
    {
        power_adc_number = settings.adc_num;
        power_adc_pin = BASE_ADC_PIN + settings.adc_num;
    }
    if (settings.short_pin != UNUSED_PIN)
    {
        short_led_pin = settings.short_pin;
        gpio_init(short_led_pin);
        gpio_set_dir(short_led_pin, GPIO_OUT);
        gpio_put(short_led_pin, 0);
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
    pio_sm = pio_claim_unused_sm((PIO)pio, true);
    assert(pio_sm != -1); // This should never happen

    dcc_program_init((PIO)pio, pio_sm, offset, signal_pin, (is_prog ? DCC_PROG_PREAMBLE : DCC_MAIN_PREAMBLE));
    pio_sm_set_enabled((PIO)pio, pio_sm, true);
    
    // Initialize PIO health monitoring
    pio_health.last_activity_time = to_ms_since_boot(get_absolute_time());
    pio_health.last_pio_check_time = pio_health.last_activity_time;
    pio_health.last_interrupt_time = pio_health.last_activity_time;
}

void PicoDccTrack::setPower(bool on)
{
    power_on = on;
    gpio_put(power_ctrl_pin, on);

    if (on && short_led_pin != UNUSED_PIN)
    {
        // If we have a short LED then turn it off
        gpio_put(short_led_pin, 0);
    }

}

void PicoDccTrack::loop()
{
    // Check PIO health before processing commands
    checkPIOHealth();
    
    // Process current monitoring only if available
    if (canReadCurrent())
    {
        uint reading = adc_read();
        
        // Check for overcurrent condition (short circuit protection)
        if (reading > (TRACK_POWER_ADC_RANGE / 100 * 90))   // 90% 
        {
            // If the current is too high then we need to stop the track
            setPower(false);
            LOG_CRITICAL(COMPONENT_TRACK, "Overcurrent protection activated");

            if (short_led_pin != UNUSED_PIN)
            {
                // If we have a short LED then turn it on
                gpio_put(short_led_pin, 1);
            }
        }
        
        // Update current averaging
        if (current_cnt++ >= TRACK_POWER_CURRENT_SAMPLES)
        {
            average_current_reading = (float)current_sum / current_cnt;
            current_cnt = 0;
            current_sum = 0;
        }
        else
        {
            current_sum += reading;
        }
    }

    // Process any incoming messages to be sent to the track
    // Priority: explicit commands from Core 0 > loco reminders > idle packets
    // Repeat logic is handled on Core 0 for explicit commands
    raw_dcc_cmd_t cmd;
    if (queue_try_remove(&cmd_queue, &cmd))
    {
        // Priority 1: Explicit command from Core 0
        sendCommand(&cmd);
        pio_health.commands_sent++;
        pio_health.last_activity_time = to_ms_since_boot(get_absolute_time());
    }
    else if (locos_collection != nullptr && locos_collection->getNextReminder(cmd))
    {
        // Priority 2: Locomotive reminder (main track only, when no explicit commands waiting)
        sendCommand(&cmd);
        pio_health.commands_sent++;
        pio_health.last_activity_time = to_ms_since_boot(get_absolute_time());
    }
    else
    {
        // Priority 3: Idle packet (when no commands or reminders available)
        sendIdle();
        pio_health.idle_packets_sent++;
        pio_health.last_activity_time = to_ms_since_boot(get_absolute_time());
    }
}

void PicoDccTrack::queueCommand(raw_dcc_cmd_t *cmd)
{
    queue_add_blocking(&cmd_queue, cmd);
    pio_health.commands_queued++;
}

void PicoDccTrack::sendCommand(raw_dcc_cmd_t *cmd)
{
    last_command_time = to_ms_since_boot(get_absolute_time());
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

    // Send command to PIO and track successful transmission
    pio_sm_put_blocking((PIO)pio, 0, (cmd->cmd_data >> 32) & 0xFFFFFFFF);
    if (cmd->length > 1)
    {
        pio_sm_put_blocking((PIO)pio, 0, cmd->cmd_data & 0xFFFFFFFF);
    }
    
    // Option 4: Track PIO transmission activity (simulated interrupt)
    pio_health.last_interrupt_time = to_ms_since_boot(get_absolute_time());
}

void PicoDccTrack::sendIdle()
{
    // This is the Idle packet
    raw_dcc_cmd_t idle_cmd;
    idle_cmd.is_prog = is_prog;
    idle_cmd.length = 3;
    idle_cmd.data[0] = 0xFF;  // Idle packet starts with 0xFF
    idle_cmd.data[1] = 0x00;  // No data
    idle_cmd.data[2] = 0xFF;  // Error detection byte
    idle_cmd.cmd_data = 0;    // Will be computed in sendCommand
    idle_cmd.repeats = 0;
    sendCommand(&idle_cmd);
}

// PIO Health Monitoring Implementation (Options 1, 3, 4)
void PicoDccTrack::checkPIOHealth()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool was_healthy = pio_health.is_healthy;
    

    
    // Option 3: Check transmission activity (every 50ms check)
    if (now - pio_health.last_pio_check_time >= 50)
    {
        pio_health.is_healthy = true;  // Assume healthy until proven otherwise
        // Check for transmission stalls
        if (pio_health.commands_queued > pio_health.commands_sent)
        {
            // Commands are queued but not being sent
            if (now - pio_health.last_activity_time > 100) // 100ms timeout
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO transmission stall detected");
            }
        }
        
        // Option 1: Alternative PIO health check using transmission rate
        // Monitor that we're successfully transmitting commands/idle packets
        uint32_t total_transmissions = pio_health.commands_sent + pio_health.idle_packets_sent;
        

        
        if (total_transmissions == pio_health.last_transmission_count)
        {
            // No transmissions in the last check period
            pio_health.pio_stall_count++;

            if (pio_health.pio_stall_count >= 3) // No activity for 150ms (3 * 50ms)
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO transmission completely stopped");

            }
        }
        else
        {
            pio_health.pio_stall_count = 0;
            pio_health.last_transmission_count = total_transmissions;

        }
        
        // Option 4: Check interrupt activity (if enabled)
        if (pio_health.interrupt_enabled)
        {
            if (now - pio_health.last_interrupt_time > 200) // 200ms timeout for interrupts
            {
                pio_health.is_healthy = false;
                LOG_CRITICAL(COMPONENT_TRACK, "PIO interrupt activity timeout");
            }
        }
        
        pio_health.last_pio_check_time = now;
    }
    
    // Track failure events
    if (!pio_health.is_healthy)
    {
        pio_health.failure_count++;
        if (was_healthy) // First failure detection
        {
            LOG_CRITICAL(COMPONENT_TRACK, "PIO health failure detected");
        }
    }
}

bool PicoDccTrack::isPIOHealthy()
{
    checkPIOHealth();
    return pio_health.is_healthy;
}
