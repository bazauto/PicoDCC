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
#define ACK_HYSTERESIS_DEFAULT_MA 10      // Dropout margin to keep pulse continuous
#define CV_READ_CONFIRMATIONS 2          // Require two ACK detections per byte before accepting

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
    
    // Current monitoring
    float baseline_current_ma;     // Decoder idle current (measured before ACK detection)
    
    // Note: ACK detection parameters (threshold, min/max duration) are no longer
    // cached as member variables. They are read dynamically from config_storage
    // during ACK detection to support runtime configuration changes via <D ACK> commands.
    
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
     * - Current must exceed baseline + threshold (dynamically read from config)
     * - Pulse duration must be within min/max range (dynamically read from config)
     * - Detection window is timeout_ms
     * 
     * ACK parameters are read from config_storage on each call to support
     * runtime configuration changes via <D ACK> commands.
     * 
     * @param timeout_ms Maximum time to wait for ACK pulse (default: 20ms)
     * @return true if valid ACK detected, false otherwise
     */
    bool detectACK(uint32_t timeout_ms = CV_READ_TIMEOUT_MS);
    
    /**
     * @brief Generate NMRA Direct Mode CV verify packet
     * 
     * Creates verify byte packet (NMRA S-9.2.3):
     * - Instruction: 0111CCAA (CC=CV bits 9-8, AA=11)
     * - Decoder sends ACK if CV value matches
     * 
     * @param cv_number CV number (1-1024)
     * @param byte_value Value to verify (0-255)
     * @return DCC packet structure
     */
    raw_dcc_cmd_t generateCVVerifyPacket(uint16_t cv_number, uint8_t byte_value);
    
    /**
     * @brief Generate NMRA Direct Mode CV write packet
     * 
     * Creates write byte packet (NMRA S-9.2.3):
     * - Instruction: 0111CCAA (CC=CV bits 9-8, AA=11)
     * - Decoder sends ACK, then writes value to CV
     * 
     * @param cv_number CV number (1-1024)
     * @param value Value to write (0-255)
     * @return DCC packet structure
     */
    raw_dcc_cmd_t generateCVWritePacket(uint16_t cv_number, uint8_t value);

    raw_dcc_cmd_t generateResetPacket();

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
     * @brief Write CV value to decoder
     * 
     * Sends Direct Mode write byte packet to decoder and waits for ACK.
     * Decoder acknowledges first, then writes the value to CV.
     * 
     * @param cv CV number to write (1-1024)
     * @param value Value to write (0-255)
     * @return true if ACK received (write successful), false otherwise
     */
    bool writeCV(uint16_t cv, uint8_t value);
    
    /**
     * @brief Verify CV value matches expected value
     * 
     * Sends Direct Mode verify packet to decoder and checks for ACK pulse.
     * JMRI uses this for decoder identification on programming track.
     * 
     * @param cv CV number to verify (1-1024)
     * @param expected_value Expected CV value (0-255)
     * @return true if ACK received (value matches), false otherwise
     */
    bool verifyCV(uint16_t cv, uint8_t expected_value);
    
    /**
     * @brief Read short address from decoder (CV1)
     * 
     * Convenience method that reads CV1 (short address).
     * Valid range: 1-127
     * 
     * @return Short address (1-127), -1 on error or no decoder
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
     * Reads current value from config storage (if available).
     * 
     * @return ACK threshold in mA
     */
    uint16_t getACKThreshold() const {
        if (config_storage != nullptr) {
            return static_cast<uint16_t>(config_storage->getACKThreshold());
        }
        return ACK_LIMIT_DEFAULT_MA;
    }
    
    /**
     * @brief Get ACK min duration configuration
     * 
     * Reads current value from config storage (if available).
     * 
     * @return Min duration in µs
     */
    uint16_t getACKMinDuration() const {
        if (config_storage != nullptr) {
            return static_cast<uint16_t>(config_storage->getACKMinDuration() * 1000.0f);
        }
        return ACK_MIN_DURATION_DEFAULT_US;
    }
    
    /**
     * @brief Get ACK max duration configuration
     * 
     * Reads current value from config storage (if available).
     * 
     * @return Max duration in µs
     */
    uint16_t getACKMaxDuration() const {
        if (config_storage != nullptr) {
            return static_cast<uint16_t>(config_storage->getACKMaxDuration() * 1000.0f);
        }
        return ACK_MAX_DURATION_DEFAULT_US;
    }
};

#endif // PICO_DCC_PROGRAMMER_H
