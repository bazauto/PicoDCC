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

    // Power-fault latch (#4, #42). Boot is not a fault: the LED is set off just
    // above, and both flags start clear so nothing is announced until something
    // actually cuts power.
    power_fault_latched = false;
    power_fault_unannounced = false;
    
    // Initialize operation mode and configuration
    operation_mode = OperationMode::NORMAL;
    config_storage.load();  // Load configuration from flash
    
    LOG_INFO(COMPONENT_SYSTEM, "PicoDCCController initialized in NORMAL mode");
}

// This is the Core 0 loop
void PicoDccController::dccexLoop()
{
    // #4: report a power cutoff on the wire. The cutoff itself happens on Core 1
    // (or inside emergencyPowerCutoff()); this is the only place that writes it
    // to the UART, because a blocking uart_puts() on Core 1 would stall the DCC
    // hot path that the timing monitor exists to protect. <p0 MAIN> / <p0 PROG>
    // are the standard DCC-EX power notifications, which any host already
    // parses -- the orchestrator has handled them, including unsolicited ones,
    // since bazauto/layout-orchestration#148.
    //
    // Drained once per cutoff, not once per pass: the latch is what persists,
    // and re-announcing at 100Hz for as long as the fault held would be its own
    // kind of noise.
    if (power_fault_unannounced)
    {
        power_fault_unannounced = false;
        // Each track's ACTUAL state, read here rather than assumed to be off.
        // The timing, PIO and heartbeat cutoffs take both tracks down, so this
        // is <p0 MAIN> <p0 PROG> for all three. An overcurrent trip need not be:
        // the programming track can trip on its own while the main track is
        // still live, and announcing <p0 MAIN> there would be a lie about the
        // one track that matters most.
        DCCEX_RESPONSE(main_track->getPower() ? "<p1 MAIN>" : "<p0 MAIN>");
        DCCEX_RESPONSE(prog_track->getPower() ? "<p1 PROG>" : "<p0 PROG>");
    }

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
                // Emergency stop is a broadcast to every decoder on the layout.
                cmd.is_prog = false;  // Emergency stop goes to main track
                cmd.length = 2;
                cmd.data[0] = 0x00;   // Broadcast address
                cmd.data[1] = 0x41;   // Emergency stop instruction

                // It used to be sent exactly once, against 3 for an ordinary
                // throttle change (#3). DCC is an unacknowledged broadcast over
                // a dirty rail joint, and this is the packet that most needs to
                // arrive.
                cmd.repeats = DCC_ESTOP_BROADCAST_REPEATS;
                
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
                
                // Hold every known loco at speed 0, direction preserved (#3).
                //
                // This used to call forgetAllLocos(), which emptied the table --
                // so Core 1's reminder generator had nothing left to repeat. A
                // loco that missed the single broadcast kept its previous speed
                // and nothing would ever contradict it: the reminders that would
                // have re-asserted "stopped" went out with the entries. The
                // train ran on while the station believed everything had stopped.
                //
                // Forgetting a loco is the right response to "this loco is
                // gone", not to "stop everything".
                pico_locos->stopAllLocos();
                
                // Status responses are sent after the stop, so each one reports
                // the speed the loco is actually being held at rather than the
                // speed it had a moment ago.
                pico_locos->sendEmergencyStopResponses();
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

        // The entry's own timestamp is the measurement: it says when Core 1
        // actually began running, which is the baseline the command-gap check
        // below measures against before the first packet goes out. Without it,
        // a violation timestamp on its own cannot be turned into a gap (#80).
        LOG_INFO(COMPONENT_CORE, "Core 1 loop started");
    }
    
    // Check command timing if it's been more than 10ms
    if (current_time - last_command_check >= 10)
    {
        // Before the first command has ever been sent, getLastCommandTime() is 0,
        // which would make the gap "everything since boot" and always look fatal.
        // Measure from when Core 1 started instead -- so a Core 1 that starts and
        // then sends nothing still trips the same 100ms limit, but the boot
        // transient does not.
        // `last_cmd == 0` was the test for "nothing has ever been sent", and it
        // is wrong: dcc_millis() returns 0 for the whole first millisecond after
        // boot, so a packet transmitted during it looks identical to no packet
        // at all (#80). Core 1 starting promptly is precisely when that happens,
        // and the gap then silently switched to measuring from core1_start_ms
        // while the transmitter was in fact stalled -- reporting the wrong cause
        // for a real fault. Ask the track directly.
        bool ever_sent = main_track->hasSentCommand();
        uint32_t last_cmd = main_track->getLastCommandTime();
        uint32_t main_gap = ever_sent ? (current_time - last_cmd)
                                      : (current_time - core1_start_ms);
        
        // Check PIO health on both tracks
        bool main_pio_healthy = main_track->isPIOHealthy();
        bool prog_pio_healthy = prog_track->isPIOHealthy();
        
        // If we have a dangerous gap in commands (> 100ms) OR PIO failure
        if (main_gap >= 100 || !main_pio_healthy || !prog_pio_healthy)
        {
            // Cutting power is unconditional and repeated: it is the safe
            // action, it is idempotent, and re-asserting it costs a GPIO write.
            main_track->setPower(false); // Cut power to main track
            prog_track->setPower(false); // Cut power to prog track

            // Everything that is a REPORT rather than an action happens once per
            // fault. Logging every 10ms pass would fill the 30-entry buffer in
            // 300ms and erase the history that explains the fault -- the LCD log
            // being the only surviving evidence is precisely what made #32 hard
            // to diagnose.
            if (!power_fault_latched)
            {
                raisePowerFault();

                if (main_gap >= 100)
                {
                    // Carry the numbers. "DCC timing violation detected" alone
                    // cannot distinguish a transmitter that stalled from one
                    // that never started, and those want opposite fixes -- which
                    // is exactly where #80 stalled. The gap and whether any
                    // packet has ever been sent are the two facts that separate
                    // them.
                    //
                    // Static, not a stack buffer: this is the Core 1 hot path
                    // (rule 4). It is written only here, only on Core 1, and
                    // only once per fault because the latch guards it -- so the
                    // shared-static hazard of #18 does not apply.
                    static char gap_msg[DIAG_MESSAGE_MAX_LEN] __attribute__((aligned(8)));
                    snprintf(gap_msg, sizeof(gap_msg),
                             "DCC timing violation: gap %ums%s",
                             (unsigned)main_gap,
                             ever_sent ? "" : " (no packet sent yet)");
                    LOG_CRITICAL(COMPONENT_CONTROLLER, gap_msg);
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
        }
        // #42: deliberately NO `else` clearing the LED. Cutting power stops
        // commands being sent, so the very next pass sees a small gap and a
        // healthy PIO, and the old `else` branch turned the LED off while the
        // power stayed off -- a dead layout with no indication, and an error LED
        // essentially never observed lit. The LED follows the latch, and the
        // latch is cleared only by a deliberate power restore, below.
        
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

    // Overcurrent and the fault-latch bookkeeping, AFTER the track loops above
    // rather than before them -- an overcurrent trip happens inside
    // PicoDccTrack::loop(), so checking first would always observe it one pass
    // late.
    //
    // A trip is the same class of event as the cutoffs at the top of this
    // function: the layout goes dark and the wire says nothing. It is also the
    // cutoff most likely to actually happen in service, a derailment shorting
    // the rails. So it raises the same latch and draws the same report (#4).
    if (main_track->isTripped() || prog_track->isTripped())
    {
        if (!power_fault_latched)
        {
            raisePowerFault();
            LOG_CRITICAL(COMPONENT_POWER, "Overcurrent trip - power cut");
        }
    }

    // The one way out of a latched fault: power deliberately restored, by <1> or
    // from the LCD. Detected by observing the tracks rather than by hooking each
    // command path, so the LCD's direct setPower() call is covered without
    // lvgl_renderer needing to know this latch exists.
    //
    // Both `isTripped()` terms are load-bearing. Without them, a
    // programming-track trip while the main track is still powered would raise
    // the latch above and clear it here on the same pass, then re-raise it on
    // the next -- announcing a fault at the loop rate for as long as the short
    // lasted. `tripped` is cleared by PicoDccTrack::setPower(true), so it is the
    // same "deliberately restored" signal, read per track.
    //
    // If the underlying fault is still present, the timing check re-trips on the
    // next pass and the host is told again -- the correct answer to "restore
    // power into a station that is still faulty". There is deliberately no
    // automatic restore: a decoder that loses the DCC signal falls back to DC,
    // and DC on a powered main track is full speed.
    if (power_fault_latched && main_track->getPower()
        && !main_track->isTripped() && !prog_track->isTripped())
    {
        power_fault_latched = false;
        gpio_put(timing_error_led_pin, 0);
        LOG_INFO(COMPONENT_POWER, "Power restored - fault indication cleared");
    }
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
    
    // Hold every loco at speed 0 rather than forgetting them, for the same
    // reason as the <!> broadcast above (#3). The track is dead, so nothing
    // reaches the rails right now -- but an empty table means that when an
    // operator restores power the station asserts nothing at all, while the
    // decoders still hold the speed they were last given. They would simply
    // resume. Holding them at a stop means the reminder stream says "stopped"
    // from the first packet after power returns.
    pico_locos->stopAllLocos();
    
    // Latch the fault: lights the LED, and makes Core 0 tell the host (#4, #42).
    // Called from Core 0 (the heartbeat check in dccexLoop), so the report goes
    // out on this same pass or the next.
    raisePowerFault();
    
    // Log emergency event
    LOG_CRITICAL(COMPONENT_POWER, "Emergency power cutoff activated");
}

/**
 * Latches a power fault: LED on, and one <p0 ...> owed to the host (#4, #42).
 *
 * Safe to call from either core. It writes two volatile bools and one GPIO --
 * no allocation, no blocking, nothing that can stall the DCC hot path. The UART
 * write it implies is deferred to Core 0's dccexLoop().
 *
 * Callers inside a loop must check `power_fault_latched` first, so that logging
 * and announcing happen once per fault rather than once per pass.
 */
void PicoDccController::raisePowerFault()
{
    power_fault_latched = true;
    power_fault_unannounced = true;
    gpio_put(timing_error_led_pin, 1);
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

        // <D SPEED28|SPEED128 [cab]> (#8)
        if (subcommand == DCCEX_CONFIG_SPEED) {
            handleSpeedStepCommand(packet->getSpeedStepMode(), packet->getSpeedStepCab());
            return true;
        }

        // Check for ACK subcommand (subcommand == 1)
        if (subcommand != DCCEX_CONFIG_ACK) {
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

void PicoDccController::handleSpeedStepCommand(int steps, int cab)
{
    // No cab: the station-wide default, which every loco that has not been
    // named individually follows.
    if (cab == 0) {
        if (!pico_locos->setStationSpeedSteps((uint8_t)steps)) {
            DCCEX_RESPONSE("<X>");
            return;
        }

        char response[24];
        snprintf(response, sizeof(response), "<D SPEED%d>", steps);
        DCCEX_RESPONSE(response);
        LOG_INFO(COMPONENT_DCCEX, "Station speed step mode updated");
        return;
    }

    // A cab out of 1..10239, or a collection with no room for a new entry.
    // Answering <X> rather than dropping it is the point of #4: a host that
    // gets silence cannot tell a rejected command from an applied one, and
    // would go on believing the loco is encoded the way it asked.
    if (!dcc_is_valid_loco_address(cab)
        || !pico_locos->setLocoSpeedSteps((uint16_t)cab, (uint8_t)steps)) {
        DCCEX_RESPONSE("<X>");
        LOG_WARNING(COMPONENT_DCCEX, "Speed step command rejected");
        return;
    }

    char response[24];
    snprintf(response, sizeof(response), "<D SPEED%d %d>", steps, cab);
    DCCEX_RESPONSE(response);
    LOG_INFO(COMPONENT_DCCEX, "Loco speed step mode updated");
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

