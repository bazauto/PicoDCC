#include <algorithm>
#include <queue>
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "pico_dcccontroller.h"
#include "../dccex_communication.h"
#include "../pico_diagnostic.h"
#include "version.h"

#ifndef TEST_BUILD
#include <hardware/adc.h>
#endif

PicoDccController::PicoDccController(track_settings_t main_track_s, track_settings_t prog_track_s, uint8_t timing_led_pin)
    : programmer(nullptr, nullptr)  // Temporary initialization, will be set after prog_track is created
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

    // Initialize ADC hardware once (shared by both tracks)
    if (main_track_s.adc_num != UNUSED_PIN || prog_track_s.adc_num != UNUSED_PIN)
    {
        #ifndef TEST_BUILD
        adc_init();
        #endif
    }

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
    last_core1_check_us = 0;
    last_core1_heartbeat_value = 0;
    
    // Initialize operation mode and configuration
    operation_mode = OperationMode::NORMAL;
    config_storage.load();  // Load configuration from flash
    
    // Initialize configuration/calibration command handler
    dccex_config = new PicoDccExConfig(&config_storage);
    pico_dccex->setConfigHandler(dccex_config);  // Wire up the config handler
    
    // Initialize CV programmer with programming track and config storage (after they exist)
    programmer = PicoDccProgrammer(prog_track, &config_storage);
    
    LOG_INFO(COMPONENT_SYSTEM, "PicoDCCController initialized in NORMAL mode");
}

// This is the Core 0 loop
void PicoDccController::dccexLoop()
{
    // Core 1 health monitoring - check every 50ms
    uint32_t current_time_us = time_us_32();
    if (current_time_us - last_core1_check_us >= 50000) {
        if (core1_heartbeat == last_core1_heartbeat_value) {
            // Core 1 appears dead - emergency cutoff
            emergencyPowerCutoff();
            LOG_CRITICAL(COMPONENT_CORE, "Core 1 heartbeat failure detected");
        }
        last_core1_heartbeat_value = core1_heartbeat;
        last_core1_check_us = current_time_us;
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
            
            // Handle CV read address command (R)
            if (packet.isReadAddressCommand()) {
                handleReadAddressCommand();
                return;
            }
            
            // Handle CV verify command (V)
            if (packet.isVerifyCommand()) {
                handleVerifyCommand(&packet);
                return;
            }
            
            // Handle CV write command (W)
            if (packet.isWriteCommand()) {
                handleWriteCommand(&packet);
                return;
            }
            
            // Reject unsupported commands (T, S, Z) with <X>
            if (packet.isUnsupportedCommand()) {
                DCCEX_RESPONSE("<X>");
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

            if (packet.isForgetCommand())
            {
                pico_locos->forgetLoco(packet.getCab());
                // NOTE: no response required for forget command
                return;
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
                    
                    // Send locomotive status acknowledgment with current function states
                    char status[128];
                    pico_locos->getLocoStatusOrCreate(packet.getCab(), status, sizeof(status));
                    DCCEX_RESPONSE(status);
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
    static uint32_t last_command_check_us = 0;
    static uint32_t heartbeat_counter = 0;
    uint32_t current_time_us = time_us_32();  // Multicore-safe timer
    
    // Update Core 1 heartbeat for health monitoring
    core1_heartbeat = ++heartbeat_counter;
    
    // Check command timing if it's been more than 10ms
    if (current_time_us - last_command_check_us >= 10000)
    {
        uint32_t main_gap = 0;
        uint32_t last_cmd_us = main_track->getLastCommandTimeUs();
        if (last_cmd_us != 0)
        {
            main_gap = (current_time_us - last_cmd_us) / 1000;
        }
        
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
                track_queue_metrics_t queue_metrics = {};
                main_track->getQueueMetrics(&queue_metrics);

                uint32_t gap_history[TRACK_COMMAND_GAP_HISTORY] = {0};
                size_t gap_count = 0;
                main_track->getCommandGapHistory(gap_history, TRACK_COMMAND_GAP_HISTORY, &gap_count);

                const char *idle_state = main_track->getIdlePacketsEnabled() ? "ON" : "OFF";

                char timing_msg[64];
                snprintf(timing_msg, sizeof(timing_msg), "Timing gap %lums idle=%s", (unsigned long)main_gap, idle_state);
                LOG_CRITICAL(COMPONENT_CONTROLLER, timing_msg);

                char queue_msg[64];
                snprintf(queue_msg, sizeof(queue_msg), "Queue %u/%u high %u wait %lu/%luus",
                         queue_metrics.current_level,
                         queue_metrics.capacity,
                         queue_metrics.high_water_level,
                         (unsigned long)queue_metrics.last_wait_us,
                         (unsigned long)queue_metrics.max_wait_us);
                LOG_INFO(COMPONENT_QUEUE, queue_msg);

                if (gap_count > 0)
                {
                    char gap_msg[64];
                    size_t written = snprintf(gap_msg, sizeof(gap_msg), "Gap hist(ms):");
                    size_t limit = gap_count > 5 ? 5 : gap_count;
                    for (size_t i = 0; i < limit && written < sizeof(gap_msg); ++i)
                    {
                        written += snprintf(gap_msg + written, sizeof(gap_msg) - written, " %lu", (unsigned long)gap_history[i]);
                    }
                    LOG_INFO(COMPONENT_CONTROLLER, gap_msg);
                }
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
        
        last_command_check_us = current_time_us;
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

void PicoDccController::handleACKLimitCommand(int value)
{
    config_storage.setACKThreshold(value);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK LIMIT %d>", value);
    DCCEX_RESPONSE(response);
    
    LOG_INFO(COMPONENT_SYSTEM, "ACK threshold updated (runtime)");
}

void PicoDccController::handleACKMinCommand(int value)
{
    // Convert microseconds to milliseconds for internal storage
    config_storage.setACKMinDuration(value / 1000.0f);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK MIN %d>", value);
    DCCEX_RESPONSE(response);
    
    LOG_INFO(COMPONENT_SYSTEM, "ACK min duration updated (runtime)");
}

void PicoDccController::handleACKMaxCommand(int value)
{
    // Convert microseconds to milliseconds for internal storage
    config_storage.setACKMaxDuration(value / 1000.0f);
    
    // Send acknowledgment
    char response[32];
    snprintf(response, sizeof(response), "<D ACK MAX %d>", value);
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
    // Send version info (standard DCC-EX format) - git hash is auto-generated at build time
    // Regex to be matched: i(DCC-EX) V-([\\d\\.]*).*G-(.*)
    DCCEX_RESPONSE(PICODCC_VERSION_STRING);
    
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

void PicoDccController::handleReadAddressCommand()
{
    // Check if programming track is powered
    if (!prog_track->getPower()) {
        DCCEX_RESPONSE("<r -1>");
        LOG_WARNING(COMPONENT_PROGRAMMER, "Read address failed: programming track not powered");
        return;
    }
    
    LOG_INFO(COMPONENT_PROGRAMMER, "Starting decoder address read...");
    
    // Try reading short address first (CV1)
    int short_addr = programmer.readShortAddress();
    if (short_addr >= 1 && short_addr <= 127) {
        // Valid short address found
        char response[32];
        snprintf(response, sizeof(response), "<r %d>", short_addr);
        DCCEX_RESPONSE(response);
        LOG_INFO(COMPONENT_PROGRAMMER, "Short address read successfully");
        return;
    } else if (short_addr >= 0) {
        char log_msg[DIAG_MESSAGE_MAX_LEN];
        snprintf(log_msg, sizeof(log_msg), "Short address out of range (%d)", short_addr);
        LOG_WARNING(COMPONENT_PROGRAMMER, log_msg);
    }
    
    // Try reading long address (CV17/18)
    int long_addr = programmer.readLongAddress();
    if (long_addr >= 128 && long_addr <= 10239) {
        // Valid long address found
        char response[32];
        snprintf(response, sizeof(response), "<r %d>", long_addr);
        DCCEX_RESPONSE(response);
        LOG_INFO(COMPONENT_PROGRAMMER, "Long address read successfully");
        return;
    } else if (long_addr >= 0) {
        char log_msg[DIAG_MESSAGE_MAX_LEN];
        snprintf(log_msg, sizeof(log_msg), "Long address out of range (%d)", long_addr);
        LOG_WARNING(COMPONENT_PROGRAMMER, log_msg);
    }
    
    // No valid address found
    DCCEX_RESPONSE("<r -1>");
    LOG_ERROR(COMPONENT_PROGRAMMER, "Failed to read decoder address");
}

void PicoDccController::handleWriteCommand(PicoDccExPacket* packet)
{
    int cv = packet->getCVNumber();
    int value = packet->getCVValue();
    int form = packet->getWriteForm();
    
    // Check if programming track is powered
    if (!prog_track->getPower()) {
        DCCEX_RESPONSE("<w -1>");
        LOG_WARNING(COMPONENT_PROGRAMMER, "Write CV failed: programming track not powered");
        return;
    }
    
    LOG_INFO(COMPONENT_PROGRAMMER, "Writing CV value...");
    
    // Write CV value (includes verification per NMRA standard)
    bool success = programmer.writeCV(cv, value);
    
    if (success) {
        // Write and verify successful
        if (form == 1) {
            // <W addr> form - respond with <w addr>
            char response[16];
            snprintf(response, sizeof(response), "<w %d>", value);
            DCCEX_RESPONSE(response);
        } else {
            // <W cv value> form - respond with <r cv value>
            char response[32];
            snprintf(response, sizeof(response), "<r %d %d>", cv, value);
            DCCEX_RESPONSE(response);
        }
        LOG_INFO(COMPONENT_PROGRAMMER, "CV write and verify successful");
    } else {
        // Write or verify failed
        if (form == 1) {
            // <W addr> form failure
            DCCEX_RESPONSE("<w -1>");
        } else {
            // <W cv value> form failure
            char response[32];
            snprintf(response, sizeof(response), "<r %d -1>", cv);
            DCCEX_RESPONSE(response);
        }
        LOG_ERROR(COMPONENT_PROGRAMMER, "CV write/verify failed");
    }
}

void PicoDccController::handleVerifyCommand(PicoDccExPacket* packet)
{
    int cv = packet->getCVNumber();
    int value = packet->getCVValue();
    
    // Check if programming track is powered
    if (!prog_track->getPower()) {
        char response[32];
        snprintf(response, sizeof(response), "<v %d -1>", cv);
        DCCEX_RESPONSE(response);
        LOG_WARNING(COMPONENT_PROGRAMMER, "Verify CV failed: programming track not powered");
        return;
    }
    
    {
        char log_msg[DIAG_MESSAGE_MAX_LEN];
        snprintf(log_msg, sizeof(log_msg), "Verifying CV %d value %d", cv, value);
        LOG_INFO(COMPONENT_PROGRAMMER, log_msg);
    }
    
    // Verify CV value
    bool verified = programmer.verifyCV(cv, value);
    
    if (verified) {
        // ACK received - value matches
        char response[32];
        snprintf(response, sizeof(response), "<v %d %d>", cv, value);
        DCCEX_RESPONSE(response);
        LOG_INFO(COMPONENT_PROGRAMMER, "CV verify successful");
    } else {
        // No ACK - value doesn't match or no decoder
        char response[32];
        snprintf(response, sizeof(response), "<v %d -1>", cv);
        DCCEX_RESPONSE(response);
        LOG_INFO(COMPONENT_PROGRAMMER, "CV verify failed - no ACK");
    }
}

