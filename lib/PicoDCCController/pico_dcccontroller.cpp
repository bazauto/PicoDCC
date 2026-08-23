#include <algorithm>
#include <queue>
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "pico_dcccontroller.h"
#include "../dccex_communication.h"
#include "../dcc_time.h"
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
    
    // Initialize core health monitoring. The baselines are deliberately left
    // unarmed here and taken on the first loop pass instead -- see dccexLoop().
    core1_heartbeat = 0;
    last_core1_check = 0;
    last_core1_heartbeat_value = 0;
    monitors_armed = false;
    monitors_armed_ms = 0;
    core1_seen_alive = false;
    core1_loop_started = false;
    core1_start_ms = 0;
    
    // Initialize operation mode and configuration
    operation_mode = OperationMode::NORMAL;
    config_storage.load();  // Load configuration from flash
    
    LOG_INFO(COMPONENT_SYSTEM, "PicoDCCController initialized in NORMAL mode");
}

// This is the Core 0 loop
void PicoDccController::dccexLoop()
{
    // Core 1 health monitoring - check every 50ms
    uint32_t current_time = dcc_millis();

    // Arm the monitor on the first pass. Construction happens before LCD init,
    // the boot sequence and multicore_launch_core1(), so a baseline of 0 is
    // already tens of milliseconds stale by now and the first comparison below
    // would fire before Core 1 has had any chance to tick.
    if (!monitors_armed) {
        last_core1_check = current_time;
        monitors_armed_ms = current_time;
        monitors_armed = true;
    }

    if (current_time - last_core1_check >= 50) {
        if (!core1_seen_alive) {
            // Core 1 has never ticked. Briefly that is just startup, but a Core 1
            // that never starts is a genuine failure -- so this is a deadline,
            // not an exemption from monitoring.
            if (core1_heartbeat != 0) {
                core1_seen_alive = true;
            } else if (current_time - monitors_armed_ms >= CORE1_STARTUP_GRACE_MS) {
                emergencyPowerCutoff();
                LOG_CRITICAL(COMPONENT_CORE, "Core 1 failed to start");
            }
        } else if (core1_heartbeat == last_core1_heartbeat_value) {
            // Core 1 was alive and has stopped ticking - emergency cutoff
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
            // Handle configuration commands (D, E, s, #)
            if (packet.isConfigCommand() || packet.isVersionCommand() || packet.isNumCabsCommand()) {
                handleConfigCommand(&packet);
                return;
            }
            
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
                if (operation_mode == OperationMode::LAYOUT_MAINTENANCE) {
                    // Reject, and say so. A headless host has no way to see the
                    // LCD, and a discarded throttle command that draws no reply
                    // is indistinguishable from one that worked (#4).
                    DCCEX_RESPONSE("<X>");
                    LOG_WARNING(COMPONENT_CONTROLLER, "Throttle/function rejected: LAYOUT_MAINTENANCE mode active");
                } else {
                    // Try to update existing loco, or add new one if not found
                    bool accepted = true;
                    if (!pico_locos->updateLocoThrottle(packet.getCab(), &packet, cmd))
                    {
                        // Loco not found in collection, add it. Rejection here
                        // means the collection is full -- the address and speed
                        // already passed validatePacket().
                        if (!pico_locos->addLoco(&packet, cmd))
                        {
                            accepted = false;
                            LOG_WARNING(COMPONENT_CONTROLLER, "Throttle command rejected");
                        }
                    }

                    if (cmd.length > 0) // Only queue if we have a valid command
                    {
                        main_cmd_queue.push(cmd);
                    }

                    if (!accepted)
                    {
                        // The command was thrown away, so it must not draw an
                        // affirmative reply. getDccExCabUpdate() is built from
                        // the *packet*, not from loco state, so sending it here
                        // reported the requested speed for a command that never
                        // reached the rails -- worse than silence, because the
                        // host is actively told it worked (#4).
                        DCCEX_RESPONSE("<X>");
                    }
                    // Send locomotive status acknowledgment for throttle commands
                    // only (D5): <F> has no way to report a function map or the
                    // loco's real speed, and reporting the function number as a
                    // speed (the previous behaviour) is worse than no reply.
                    else if (packet.isThrottleCommand())
                    {
                        DCCEX_RESPONSE(packet.getDccExCabUpdate());
                    }
                }
            }

            if (packet.isAccesoryCommand())
            {
                if (operation_mode == OperationMode::LAYOUT_MAINTENANCE) {
                    // Same rule as throttle/function above: rejected commands
                    // answer <X>, never silence (#4).
                    DCCEX_RESPONSE("<X>");
                    LOG_WARNING(COMPONENT_CONTROLLER, "Accessory rejected: LAYOUT_MAINTENANCE mode active");
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
    uint32_t current_time = dcc_millis();
    
    // Update Core 1 heartbeat for health monitoring
    core1_heartbeat = ++heartbeat_counter;

    // Remember when Core 1 actually began running, so the command-gap check below
    // has something real to measure against before the first command is sent.
    if (!core1_loop_started) {
        core1_start_ms = current_time;
        core1_loop_started = true;
    }
    
    // Check command timing if it's been more than 10ms
    if (current_time - last_command_check >= 10)
    {
        // Before the first command has ever been sent, getLastCommandTime() is 0,
        // which would make the gap "everything since boot" and always look fatal.
        // Measure from when Core 1 started instead -- so a Core 1 that starts and
        // then sends nothing still trips the same 100ms limit, but the boot
        // transient does not.
        uint32_t last_cmd = main_track->getLastCommandTime();
        uint32_t main_gap = (last_cmd == 0) ? (current_time - core1_start_ms)
                                            : (current_time - last_cmd);
        
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
    //
    // Core 1 is paced by the main track: its loop() blocks for room in the PIO
    // TX FIFO, and that block is what stops this loop outrunning the hardware.
    // The programming track must not also block, or the main track is refilled
    // at the programming track's slower rate -- six more preamble bits per
    // packet -- while draining at its own faster one. Its FIFO then trends
    // empty, and an empty FIFO parks the signal pin high (#34) rather than going
    // quiet, so the coupling put DC on the main track between packets (#35).
    //
    // The programming track cannot starve under this arrangement: the loop now
    // runs at the main track's faster rate, so prog is offered a packet more
    // often than it can transmit one.
    main_track->loop(PicoDccTrack::Pacing::Blocking);
    prog_track->loop(PicoDccTrack::Pacing::NonBlocking);
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

// Configuration command handlers
bool PicoDccController::handleConfigCommand(PicoDccExPacket* packet)
{
    char opcode = packet->getOpcode();
    
    // Handle <D ACK ...> commands
    if (opcode == 'D') {
        int subcommand = packet->getConfigSubcommand();
        int param_type = packet->getConfigParamType();
        int value = packet->getConfigValue();
        
        // Check for ACK subcommand (subcommand == 1)
        if (subcommand != 1) {
            return false;  // Unknown subcommand
        }
        
        // Route to appropriate handler based on param_type
        if (param_type == 1) {  // LIMIT
            handleACKLimitCommand(value);
        } else if (param_type == 2) {  // MIN
            handleACKMinCommand(value);
        } else if (param_type == 3) {  // MAX
            handleACKMaxCommand(value);
        } else {
            return false;  // Unknown parameter type
        }
        
        return true;
    }
    
    // Handle <E> save command
    if (opcode == 'E') {
        handleSaveCommand();
        return true;
    }
    
    // Handle <s> status command
    if (opcode == 's') {
        handleStatusCommand();
        return true;
    }
    
    // Handle <#> capacity command
    if (opcode == '#') {
        char response[16];
        snprintf(response, sizeof(response), "<# %d>", MAX_LOCO);
        DCCEX_RESPONSE(response);
        return true;
    }
    
    return false;  // Not a config command
}

void PicoDccController::handleACKLimitCommand(float value)
{
    config_storage.setACKThreshold(value);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK LIMIT %.0f>", value);
    DCCEX_RESPONSE(response);
    
    LOG_INFO(COMPONENT_SYSTEM, "ACK threshold updated (runtime)");
}

void PicoDccController::handleACKMinCommand(float value)
{
    // Convert microseconds to milliseconds for internal storage
    config_storage.setACKMinDuration(value / 1000.0f);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK MIN %.0f>", value);
    DCCEX_RESPONSE(response);
    
    LOG_INFO(COMPONENT_SYSTEM, "ACK min duration updated (runtime)");
}

void PicoDccController::handleACKMaxCommand(float value)
{
    // Convert microseconds to milliseconds for internal storage
    config_storage.setACKMaxDuration(value / 1000.0f);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK MAX %.0f>", value);
    DCCEX_RESPONSE(response);
    
    LOG_INFO(COMPONENT_SYSTEM, "ACK max duration updated (runtime)");
}

void PicoDccController::handleSaveCommand()
{
    // Check if in maintenance mode
    if (operation_mode != OperationMode::LAYOUT_MAINTENANCE) {
        DCCEX_RESPONSE("<X>");
        LOG_WARNING(COMPONENT_SYSTEM, "Save command rejected: not in LAYOUT_MAINTENANCE mode");
        return;
    }
    
    // Save configuration to flash
    bool success = config_storage.save();
    
    if (success) {
        DCCEX_RESPONSE("<e SAVED>");
        LOG_INFO(COMPONENT_SYSTEM, "Configuration saved to flash (410ms blocking)");
    } else {
        DCCEX_RESPONSE("<e FAILED>");
        LOG_ERROR(COMPONENT_SYSTEM, "Configuration save failed");
    }
}

void PicoDccController::handleStatusCommand()
{
    // Send version info (standard DCC-EX format)
    DCCEX_RESPONSE(PICODCC_IDENTITY);
    
    // Send main track power status (standard DCC-EX format)
    if (main_track->getPower()) {
        DCCEX_RESPONSE("<p1 MAIN>");
    } else {
        DCCEX_RESPONSE("<p0 MAIN>");
    }
    
    // Send programming track power status (standard DCC-EX format)
    if (prog_track->getPower()) {
        DCCEX_RESPONSE("<p1 PROG>");
    } else {
        DCCEX_RESPONSE("<p0 PROG>");
    }
    
    // Note: Mode and unsaved changes displayed on LCD only (not in DCC-EX protocol)
    // This avoids polluting JMRI logs and doesn't require Java driver modifications
}

