/*
 * PicoDCC Programmer - Service Mode (Programming Track) CV Operations
 * 
 * This component handles CV (Configuration Variable) read/write operations
 * on the programming track using NMRA Direct Mode packets and ACK detection.
 * 
 * Key Features:
 * - ACK pulse detection (60mA for 6ms within 8ms window)
 * - Baseline current measurement for accurate ACK detection
 * - Direct Mode CV read operations (NMRA S-9.2.3)
 * - Configurable ACK parameters (threshold, duration window)
 * - Integration with NV storage for persistent configuration
 * 
 * References:
 * - NMRA S-9.2.3: Service Mode for Digital Decoders
 * - DCC Wiki: https://dccwiki.com/Service_Mode_Programming
 */

#ifndef PICO_DCC_PROGRAMMER_H
#define PICO_DCC_PROGRAMMER_H

#include <cstdint>
#include "../PicoDCCTrack/pico_dcctrack.h"
#include "../PicoConfigStorage/pico_config_storage.h"
#include "../pico_diagnostic.h"

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <pico/stdlib.h>
#include <pico/time.h>
#endif

// ACK Detection Defaults (NMRA S-9.2.3 spec)
#define ACK_LIMIT_DEFAULT_MA 60          // ACK threshold above baseline (50-100mA typical)
#define ACK_MIN_DURATION_DEFAULT_US 4500 // Min ACK pulse duration (4.5ms typical)
#define ACK_MAX_DURATION_DEFAULT_US 8000 // Max ACK pulse duration (8ms spec limit)

// CV Read Configuration
#define CV_READ_RETRIES 8        // Number of verify packets per byte (NMRA spec: 6-8)
#define CV_READ_TIMEOUT_MS 20    // ACK detection timeout per packet (NMRA spec)
#define CV_MIN 1                 // Minimum CV number
#define CV_MAX 1024              // Maximum CV number (NMRA extended addressing)

// Common CV Addresses
#define CV_SHORT_ADDRESS 1       // CV1: Short address (1-127)
#define CV_LONG_ADDRESS_HIGH 17  // CV17: Long address high byte
#define CV_LONG_ADDRESS_LOW 18   // CV18: Long address low byte
#define CV_CONFIG 29             // CV29: Configuration byte

class PicoDccProgrammer {
private:
    // Component dependencies
    PicoDccTrack *prog_track;           // Programming track instance
    PicoConfigStorage *config_storage;  // Configuration storage (optional)
    
    // ACK detection configuration (from NV storage or defaults)
    uint16_t ack_limit_ma;         // ACK threshold above baseline (default: 60mA)
    uint16_t ack_min_duration_us;  // Min ACK pulse duration (default: 4500µs)
    uint16_t ack_max_duration_us;  // Max ACK pulse duration (default: 8000µs)
    
    // Current monitoring
    float baseline_current_ma;     // Decoder idle current (measured before ACK detection)
    
    // Internal helper methods
    
    /**
     * @brief Measure baseline current (decoder idle current)
     * 
     * Samples programming track current over 50ms to establish baseline.
     * Decoder must be powered and settled (100ms minimum).
     * 
     * @return true if baseline measured successfully, false on error
     */
    bool measureBaselineCurrent();
    
    /**
     * @brief Detect ACK pulse (60mA for 6ms within 8ms window)
     * 
     * Monitors programming track current for ACK pulse:
     * - Current must exceed baseline + ack_limit_ma
     * - Pulse duration must be within ack_min_duration_us to ack_max_duration_us
     * - Detection window is timeout_ms
     * 
     * @param timeout_ms Maximum time to wait for ACK pulse (default: 20ms)
     * @return true if valid ACK detected, false otherwise
     */
    bool detectACK(uint32_t timeout_ms = CV_READ_TIMEOUT_MS);
    
    /**
     * @brief Generate NMRA Direct Mode CV read packet
     * 
     * Creates DCC packet for CV read operation (NMRA S-9.2.3):
     * - 20-bit preamble (programming track)
     * - Address byte: 0x76 (direct mode broadcast)
     * - Instruction byte: 1110CCAA (CC=CV bits 9-8, AA=11 for read)
     * - CV address low byte
     * - Data byte: 0x00 (not used for read)
     * - Error detection byte (XOR of previous bytes)
     * 
     * @param cv_number CV number (1-1024)
     * @return DCC packet structure
     */
    raw_dcc_cmd_t generateCVReadPacket(uint16_t cv_number);
    
    /**
     * @brief Generate NMRA Direct Mode CV verify packet
     * 
     * Creates DCC packet for CV verify operation (ACK if match):
     * - Same format as read packet
     * - Instruction byte: AA=01 for verify
     * - Data byte: value to verify
     * - Decoder sends ACK if CV value matches
     * 
     * @param cv_number CV number (1-1024)
     * @param byte_value Value to verify (0-255)
     * @return DCC packet structure
     */
    raw_dcc_cmd_t generateCVVerifyPacket(uint16_t cv_number, uint8_t byte_value);

public:
    /**
     * @brief Constructor
     * 
     * @param prog_track Programming track instance (must not be nullptr)
     * @param config Optional configuration storage for ACK parameters
     */
    PicoDccProgrammer(PicoDccTrack *prog_track, PicoConfigStorage *config = nullptr);
    
    /**
     * @brief Read CV value from decoder
     * 
     * Uses NMRA Direct Mode verify packets to determine CV value:
     * 1. Measure baseline current
     * 2. Try each byte value (0-255)
     * 3. Send verify packet (6-8 times per NMRA spec)
     * 4. Check for ACK pulse
     * 5. Return byte value that produced ACK
     * 
     * Note: This is the "brute force" method (256 iterations max).
     * Optimized bit manipulation method can be added later (8 iterations).
     * 
     * @param cv_number CV number to read (1-1024)
     * @return CV value (0-255) on success, -1 on error
     */
    int16_t readCV(uint16_t cv_number);
    
    /**
     * @brief Read short address from decoder (CV1)
     * 
     * Convenience wrapper for readCV(CV_SHORT_ADDRESS).
     * Short address range: 1-127
     * 
     * @return Short address (1-127) on success, -1 on error
     */
    int16_t readShortAddress();
    
    /**
     * @brief Read long address from decoder (CV17/CV18)
     * 
     * Reads 14-bit long address from CV17 (high byte) and CV18 (low byte).
     * Long address range: 128-10239
     * 
     * Calculation: address = ((CV17 & 0x3F) << 8) | CV18
     * 
     * @return Long address (128-10239) on success, -1 on error
     */
    int16_t readLongAddress();
    
    /**
     * @brief Load ACK configuration from NV storage
     * 
     * Loads ACK parameters from configuration storage:
     * - ack_limit_ma
     * - ack_min_duration_us
     * - ack_max_duration_us
     * 
     * Falls back to defaults if config storage not available.
     */
    void loadConfig();
    
    /**
     * @brief Set ACK threshold above baseline
     * 
     * @param limit_ma ACK threshold in mA (50-100 typical, default 60)
     */
    void setACKThreshold(uint16_t limit_ma);
    
    /**
     * @brief Set ACK minimum pulse duration
     * 
     * @param duration_us Min duration in µs (4000-5000 typical, default 4500)
     */
    void setACKMinDuration(uint16_t duration_us);
    
    /**
     * @brief Set ACK maximum pulse duration
     * 
     * @param duration_us Max duration in µs (7000-9000 typical, default 8000)
     */
    void setACKMaxDuration(uint16_t duration_us);
    
    // Diagnostics and status
    
    /**
     * @brief Get baseline current measurement
     * 
     * @return Baseline current in mA (0.0 if not yet measured)
     */
    float getBaselineCurrent() const { return baseline_current_ma; }
    
    /**
     * @brief Get ACK threshold configuration
     * 
     * @return ACK threshold in mA
     */
    uint16_t getACKThreshold() const { return ack_limit_ma; }
    
    /**
     * @brief Get ACK min duration configuration
     * 
     * @return Min duration in µs
     */
    uint16_t getACKMinDuration() const { return ack_min_duration_us; }
    
    /**
     * @brief Get ACK max duration configuration
     * 
     * @return Max duration in µs
     */
    uint16_t getACKMaxDuration() const { return ack_max_duration_us; }
};

#endif // PICO_DCC_PROGRAMMER_H
