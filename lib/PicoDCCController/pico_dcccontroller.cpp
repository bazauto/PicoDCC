#include <algorithm>
#include <queue>
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "pico_dcccontroller.h"
#include "../dccex_communication.h"
#include "../pico_diagnostic.h"

PicoDccController::PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s, uint8_t timing_led_pin)
{
    // Some things that should never be
    assert(main_track_s.signal_pin != prog_track_s.signal_pin);
    assert(main_track_s.signal_pin != UNUSED_PIN);
    assert(main_track_s.ctrl_pin != prog_track_s.ctrl_pin);
    assert(main_track_s.ctrl_pin != UNUSED_PIN);
    assert(main_track_s.adc_num != prog_track_s.adc_num);
    assert(prog_track_s.ctrl_pin != UNUSED_PIN);

    // Setup the queue to transfer track commands from Core 0 to Core 1
    queue_init(&track_cmd_queue, sizeof(raw_dcc_cmd_t), CMD_QUEUE_LENGTH);

    // Setup our loco store first (needed by tracks for reminder generation)
    pico_locos = new PicoDccLocos();

    // Setup the tracks
    // Main track gets reference to locos collection for reminder generation on Core 1
    // Programming track doesn't need reminders (nullptr)
    main_track = new PicoDccTrack(false, main_track_s, pico_locos);
    prog_track = new PicoDccTrack(true, prog_track_s, nullptr);

    // Setup timing error LED
    timing_error_led_pin = timing_led_pin;
    gpio_init(timing_error_led_pin);
    gpio_set_dir(timing_error_led_pin, GPIO_OUT);
    gpio_put(timing_error_led_pin, 0); // Start with LED off

    // Setup DCCEX Packate processing
    pico_dccex = new PicoDccEx(MAX_LOCO);
    
    // Initialize core health monitoring
    core1_heartbeat = 0;
    last_core1_check = 0;
    last_core1_heartbeat_value = 0;
    
    // Initialize operation mode and configuration
    operation_mode = OperationMode::NORMAL;
    config_storage.load();  // Load configuration from flash
    
    LOG_INFO(COMPONENT_SYSTEM, "PicoDCCController initialized in NORMAL mode");
}

// This is the Core 0 loop
void PicoDccController::dccexLoop()
{
    // Core 1 health monitoring - check every 50ms
    uint32_t current_time = time_us_32() / 1000;  // Multicore-safe timer
    if (current_time - last_core1_check >= 50) {
        if (core1_heartbeat == last_core1_heartbeat_value) {
            // Core 1 appears dead - emergency cutoff
            emergencyPowerCutoff();
            LOG_CRITICAL(COMPONENT_CORE, "Core 1 heartbeat failure detected");
        }
        last_core1_heartbeat_value = core1_heartbeat;
        last_core1_check = current_time;
    }
    
    pico_dccex_packet packetData;
    bool hasCommand = pico_dccex->processCommand(&packetData);

    if (hasCommand)
    {
        PicoDccExPacket packet(packetData);
        if (packet.isValid())
        {
            raw_dcc_cmd_t cmd = {};
            
            if (packet.isPowerCommand())
            {
                // Programming track power always controllable
                if (packet.getTrack() == DCCEX_TRACK_ALL || packet.getTrack() == DCCEX_TRACK_PROG)
                    prog_track->setPower(packet.getPowerOn());

                // Main track power lockout in maintenance mode
                if (packet.getTrack() == DCCEX_TRACK_ALL || packet.getTrack() == DCCEX_TRACK_MAIN) {
                    if (operation_mode == OperationMode::LAYOUT_MAINTENANCE && packet.getPowerOn()) {
                        // Reject main track power-on in maintenance mode
                        DCCEX_RESPONSE("<X>");
                        LOG_WARNING(COMPONENT_POWER, "Main track power-on rejected: LAYOUT_MAINTENANCE mode active");
                    } else {
                        main_track->setPower(packet.getPowerOn());
                        // Send power status acknowledgment
                        DCCEX_RESPONSE(packet.getDccExPowerUpdate());
                    }
                } else {
                    // Programming track only - send acknowledgment
                    DCCEX_RESPONSE(packet.getDccExPowerUpdate());
                }
            }

            if (packet.isEmergencyStopCommand())
            {
                // Emergency stop is a broadcast command - send once and clear everything
                cmd.is_prog = false;  // Emergency stop goes to main track
                cmd.length = 2;
                cmd.data[0] = 0x00;   // Broadcast address
                cmd.data[1] = 0x41;   // Emergency stop instruction
                cmd.repeats = 0;      // Send once only
                
                // Clear the main command queue to stop all pending commands
                while (!main_cmd_queue.empty()) {
                    main_cmd_queue.pop();
                }
                
                // Clear the hardware queue
                raw_dcc_cmd_t dummy;
                while (queue_try_remove(&track_cmd_queue, &dummy)) {
                    // Remove all queued commands
                }
                
                // Send the emergency stop command immediately
                main_cmd_queue.push(cmd);
                
                // Send locomotive status responses for each loco (DCC-EX spec requirement)
                // Must be done BEFORE clearing the locomotive collection
                pico_locos->sendEmergencyStopResponses();
                
                // Clear all locos to prevent further reminders
                pico_locos->forgetAllLocos();
            }

            if (packet.isThrottleCommand() || packet.isFunctionCommand())
            {
                // Silently reject throttle/function commands in maintenance mode
                if (operation_mode == OperationMode::LAYOUT_MAINTENANCE) {
                    // Do nothing - command ignored
                } else {
                    // Try to update existing loco, or add new one if not found
                    if (!pico_locos->updateLocoThrottle(packet.getCab(), &packet, cmd))
                    {
                        // Loco not found in collection, add it
                        pico_locos->addLoco(&packet, cmd);
                    }
                    
                    if (cmd.length > 0) // Only queue if we have a valid command
                    {
                        main_cmd_queue.push(cmd);
                    }
                    
                    // Send locomotive status acknowledgment
                    DCCEX_RESPONSE(packet.getDccExCabUpdate());
                }
            }

            if (packet.isAccesoryCommand())
            {
                // Silently reject accessory commands in maintenance mode
                if (operation_mode == OperationMode::LAYOUT_MAINTENANCE) {
                    // Do nothing - command ignored
                } else {
                    cmd = *packet.getRawDccAccessoryCmd();
                    main_cmd_queue.push(cmd);
                    
                    // Send accessory acknowledgment
                    DCCEX_RESPONSE("<O>");
                }
            }

        }
    }
    else
    {
        // No command processing needed when idle
        // Reminders are now handled on Core 1 in PicoDccTrack::loop()
    }

    // Repeat/interleaving logic: process one command per loop
    if (!main_cmd_queue.empty()) {
        raw_dcc_cmd_t cmd = main_cmd_queue.front();
        
        // Try to add to hardware queue - if full, wait briefly and try again
        // Queue full is normal when Core 0 generates commands faster than Core 1 can transmit
        // We retry in a short loop to avoid blocking other operations (UART, heartbeat)
        absolute_time_t timeout = make_timeout_time_ms(5);  // 5ms timeout
        bool added = false;
        while (!time_reached(timeout)) {
            if (queue_try_add(&track_cmd_queue, &cmd)) {
                added = true;
                break;
            }
            // Brief yield to allow other operations
            sleep_us(100);  // 100 microseconds
        }
        
        if (added) {
            // Successfully sent, remove from queue
            main_cmd_queue.pop();
            
            // Handle repeats
            if (cmd.repeats > 1) {
                cmd.repeats--;
                main_cmd_queue.push(cmd);
            }
        }
        // If not added, leave command at front of queue and try again next loop iteration
    }
}

// This is the Core 1 loop
void PicoDccController::dccLoop()
{
    static uint32_t last_command_check = 0;
    static uint32_t heartbeat_counter = 0;
    uint32_t current_time = time_us_32() / 1000;  // Multicore-safe timer
    
    // Update Core 1 heartbeat for health monitoring
    core1_heartbeat = ++heartbeat_counter;
    
    // Check command timing if it's been more than 10ms
    if (current_time - last_command_check >= 10)
    {
        uint32_t main_gap = current_time - main_track->getLastCommandTime();
        
        // Check PIO health on both tracks
        bool main_pio_healthy = main_track->isPIOHealthy();
        bool prog_pio_healthy = prog_track->isPIOHealthy();
        
        // If we have a dangerous gap in commands (> 100ms) OR PIO failure
        if (main_gap >= 100 || !main_pio_healthy || !prog_pio_healthy)
        {
            // Emergency safety measures
            main_track->setPower(false); // Cut power to main track
            prog_track->setPower(false); // Cut power to prog track
            gpio_put(timing_error_led_pin, 1); // Turn on error LED
            
            if (main_gap >= 100)
            {
                LOG_CRITICAL(COMPONENT_CONTROLLER, "DCC timing violation detected");
            }
            if (!main_pio_healthy)
            {
                LOG_CRITICAL(COMPONENT_CONTROLLER, "Main track PIO failure - emergency cutoff");
            }
            if (!prog_pio_healthy)
            {
                LOG_CRITICAL(COMPONENT_CONTROLLER, "Programming track PIO failure - emergency cutoff");
            }
        }
        else
        {
            gpio_put(timing_error_led_pin, 0); // Turn off error LED if timing is good
        }
        
        last_command_check = current_time;
    }

    // Transfer commands from inter-core queue to appropriate track queue
    raw_dcc_cmd_t cmd;
    if (queue_try_remove(&track_cmd_queue, &cmd))
    {
        // Route command to the appropriate track based on is_prog flag
        if (cmd.is_prog)
        {
            prog_track->queueCommand(&cmd);
        }
        else
        {
            main_track->queueCommand(&cmd);
        }
    }

    // Command dequeue/send is now handled in main_track->loop().
    main_track->loop();
    prog_track->loop();
}

void PicoDccController::emergencyPowerCutoff()
{
    // Immediately cut power to both tracks
    main_track->setPower(false);
    prog_track->setPower(false);
    
    // Clear all queues to prevent further command processing
    while (!main_cmd_queue.empty()) {
        main_cmd_queue.pop();
    }
    
    // Clear the hardware queue
    raw_dcc_cmd_t dummy;
    while (queue_try_remove(&track_cmd_queue, &dummy)) {
        // Remove all queued commands
    }
    
    // Clear all locos to prevent reminders
    pico_locos->forgetAllLocos();
    
    // Turn on error LED to indicate emergency state
    gpio_put(timing_error_led_pin, 1);
    
    // Log emergency event
    LOG_CRITICAL(COMPONENT_POWER, "Emergency power cutoff activated");
}

// Layout Maintenance Mode management
bool PicoDccController::canEnterMaintenanceMode() const
{
    // Can only enter maintenance mode if main track power is OFF
    return !main_track->getPower();
}

void PicoDccController::enterMaintenanceMode()
{
    if (!canEnterMaintenanceMode()) {
        LOG_ERROR(COMPONENT_SYSTEM, "Cannot enter maintenance mode: main track power is ON");
        return;
    }
    
    operation_mode = OperationMode::LAYOUT_MAINTENANCE;
    LOG_INFO(COMPONENT_SYSTEM, "Entered LAYOUT_MAINTENANCE mode");
}

void PicoDccController::exitMaintenanceMode()
{
    operation_mode = OperationMode::NORMAL;
    // Note: Main track power stays OFF after exit (user must explicitly re-enable)
    LOG_INFO(COMPONENT_SYSTEM, "Exited to NORMAL mode (main track power remains OFF)");
}

