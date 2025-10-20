#include "pico_config_storage.h"
#include "../pico_diagnostic.h"
#include <string.h>

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#endif

PicoConfigStorage::PicoConfigStorage() : config_valid(false), unsaved_changes(false) {
    resetToDefaults();
}

void PicoConfigStorage::resetToDefaults() {
    memset(&config, 0, sizeof(pico_config_t));
    
    config.magic = CONFIG_MAGIC;
    config.version = CONFIG_VERSION;
    
    config.adc_to_ma_conversion = DEFAULT_ADC_TO_MA;
    config.ack_threshold_ma = DEFAULT_ACK_THRESHOLD;
    config.ack_min_duration_ms = DEFAULT_ACK_MIN_DURATION;
    config.ack_max_duration_ms = DEFAULT_ACK_MAX_DURATION;
    config.baseline_current_ma = DEFAULT_BASELINE_CURRENT;
    config.main_track_current_limit_ma = DEFAULT_MAIN_CURRENT_LIMIT;
    config.prog_track_current_limit_ma = DEFAULT_PROG_CURRENT_LIMIT;
    
    config.checksum = calculateCRC32((const uint8_t*)&config, 
                                     sizeof(pico_config_t) - sizeof(uint32_t));
    
    // Initialize runtime configuration from flash defaults
    runtime.ack_threshold_ma = config.ack_threshold_ma;
    runtime.ack_min_duration_ms = config.ack_min_duration_ms;
    runtime.ack_max_duration_ms = config.ack_max_duration_ms;
    
    config_valid = true;
    unsaved_changes = false;
}

bool PicoConfigStorage::load() {
#ifdef TEST_BUILD
    // In test mode, always use default values
    // Individual tests can modify config via setters if needed
    resetToDefaults();
    LOG_INFO(COMPONENT_SYSTEM, "Test mode: Using default configuration");
    return true;
#else
    // Read from flash (memory-mapped, direct pointer access)
    const pico_config_t *flash_config = 
        (const pico_config_t*)(XIP_BASE + CONFIG_FLASH_OFFSET);
    
    // Validate configuration from flash
    if (validateConfig(flash_config)) {
        memcpy(&config, flash_config, sizeof(pico_config_t));
        
        // Initialize runtime configuration from loaded flash values
        runtime.ack_threshold_ma = config.ack_threshold_ma;
        runtime.ack_min_duration_ms = config.ack_min_duration_ms;
        runtime.ack_max_duration_ms = config.ack_max_duration_ms;
        
        config_valid = true;
        unsaved_changes = false;
        LOG_INFO(COMPONENT_SYSTEM, "Configuration loaded from flash");
        return true;
    } else {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid flash config, using defaults");
        resetToDefaults();
        return false;
    }
#endif
}

void PicoConfigStorage::discardChanges() {
    // Restore runtime configuration from flash
    runtime.ack_threshold_ma = config.ack_threshold_ma;
    runtime.ack_min_duration_ms = config.ack_min_duration_ms;
    runtime.ack_max_duration_ms = config.ack_max_duration_ms;
    unsaved_changes = false;
    LOG_INFO(COMPONENT_SYSTEM, "Discarded unsaved configuration changes");
}

bool PicoConfigStorage::save() {
#ifdef TEST_BUILD
    // In test mode, pretend to save successfully
    // Persist runtime values to flash config
    config.ack_threshold_ma = runtime.ack_threshold_ma;
    config.ack_min_duration_ms = runtime.ack_min_duration_ms;
    config.ack_max_duration_ms = runtime.ack_max_duration_ms;
    
    config.checksum = calculateCRC32((const uint8_t*)&config, 
                                     sizeof(pico_config_t) - sizeof(uint32_t));
    config_valid = true;
    unsaved_changes = false;
    LOG_INFO(COMPONENT_SYSTEM, "Test mode: Configuration saved (mock)");
    return true;
#else
    // Persist runtime values to flash config
    config.ack_threshold_ma = runtime.ack_threshold_ma;
    config.ack_min_duration_ms = runtime.ack_min_duration_ms;
    config.ack_max_duration_ms = runtime.ack_max_duration_ms;
    
    // Update checksum before saving
    config.checksum = calculateCRC32((const uint8_t*)&config, 
                                     sizeof(pico_config_t) - sizeof(uint32_t));
    
    // Flash programming sequence:
    // 1. Check if Core 1 is running
    // 2. Halt Core 1 (track operations will pause ~410ms)
    // 3. Disable interrupts
    // 4. Erase sector (4KB)
    // 5. Program sector (256-byte pages)
    // 6. Re-enable interrupts
    // 7. Resume Core 1
    
    bool core1_was_running = multicore_lockout_victim_is_initialized(1);
    
    // Halt Core 1 if running (prevents flash access conflicts)
    if (core1_was_running) {
        multicore_lockout_start_blocking();
    }
    
    // Disable interrupts for flash write
    uint32_t interrupts = save_and_disable_interrupts();
    
    // Erase sector (4KB at CONFIG_FLASH_OFFSET)
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    
    // Program sector (write in 256-byte pages as required by flash hardware)
    flash_range_program(CONFIG_FLASH_OFFSET, (const uint8_t*)&config, 
                       sizeof(pico_config_t));
    
    // Re-enable interrupts
    restore_interrupts(interrupts);
    
    // Resume Core 1 if it was running
    if (core1_was_running) {
        multicore_lockout_end_blocking();
    }
    
    unsaved_changes = false;
    LOG_INFO(COMPONENT_SYSTEM, "Configuration saved to flash");
    
    // Verify write by re-loading and comparing checksums
    const pico_config_t *flash_config = 
        (const pico_config_t*)(XIP_BASE + CONFIG_FLASH_OFFSET);
    
    if (flash_config->checksum == config.checksum) {
        return true;
    } else {
        LOG_ERROR(COMPONENT_SYSTEM, "Flash write verification failed");
        return false;
    }
#endif
}

bool PicoConfigStorage::validateConfig(const pico_config_t *cfg) {
    // Check magic number
    if (cfg->magic != CONFIG_MAGIC) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid config magic");
        return false;
    }
    
    // Check version (only support current version for now)
    if (cfg->version != CONFIG_VERSION) {
        LOG_WARNING(COMPONENT_SYSTEM, "Unsupported config version");
        return false;
    }
    
    // Verify checksum
    uint32_t calculated_crc = calculateCRC32((const uint8_t*)cfg, 
                                            sizeof(pico_config_t) - sizeof(uint32_t));
    if (calculated_crc != cfg->checksum) {
        LOG_WARNING(COMPONENT_SYSTEM, "Config checksum mismatch");
        return false;
    }
    
    // Sanity check values (prevent corrupted config from causing issues)
    if (cfg->adc_to_ma_conversion <= 0.0f || cfg->adc_to_ma_conversion > 1.0f) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid ADC conversion factor");
        return false;
    }
    
    if (cfg->ack_threshold_ma < 30.0f || cfg->ack_threshold_ma > 100.0f) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid ACK threshold");
        return false;
    }
    
    if (cfg->ack_min_duration_ms < 1.0f || cfg->ack_min_duration_ms > 10.0f) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid ACK min duration");
        return false;
    }
    
    if (cfg->ack_max_duration_ms < 1.0f || cfg->ack_max_duration_ms > 10.0f) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid ACK max duration");
        return false;
    }
    
    return true;
}

uint32_t PicoConfigStorage::calculateCRC32(const uint8_t *data, size_t length) {
    // CRC32 implementation using standard polynomial 0x04C11DB7 (reversed 0xEDB88320)
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

// Getter implementations for flash-only values (calibration, limits)
float PicoConfigStorage::getADCToMAConversion() const {
    return config_valid ? config.adc_to_ma_conversion : DEFAULT_ADC_TO_MA;
}

float PicoConfigStorage::getBaselineCurrent() const {
    return config_valid ? config.baseline_current_ma : DEFAULT_BASELINE_CURRENT;
}

uint16_t PicoConfigStorage::getMainTrackCurrentLimit() const {
    return config_valid ? config.main_track_current_limit_ma : DEFAULT_MAIN_CURRENT_LIMIT;
}

uint16_t PicoConfigStorage::getProgTrackCurrentLimit() const {
    return config_valid ? config.prog_track_current_limit_ma : DEFAULT_PROG_CURRENT_LIMIT;
}

// Note: Runtime getters (ACK threshold/durations) are inline in header

// Runtime setters (immediate effect, marks unsaved)
void PicoConfigStorage::setACKThreshold(float value) {
    runtime.ack_threshold_ma = value;
    unsaved_changes = true;
    config_valid = true;
}

void PicoConfigStorage::setACKMinDuration(float value) {
    runtime.ack_min_duration_ms = value;
    unsaved_changes = true;
    config_valid = true;
}

void PicoConfigStorage::setACKMaxDuration(float value) {
    runtime.ack_max_duration_ms = value;
    unsaved_changes = true;
    config_valid = true;
}

// Flash setters (requires save() to persist)
void PicoConfigStorage::setADCToMAConversion(float value) {
    config.adc_to_ma_conversion = value;
    unsaved_changes = true;
    config_valid = true;
}

void PicoConfigStorage::setBaselineCurrent(float value) {
    config.baseline_current_ma = value;
    unsaved_changes = true;
    config_valid = true;
}

void PicoConfigStorage::setMainTrackCurrentLimit(uint16_t value) {
    config.main_track_current_limit_ma = value;
    unsaved_changes = true;
    config_valid = true;
}

void PicoConfigStorage::setProgTrackCurrentLimit(uint16_t value) {
    config.prog_track_current_limit_ma = value;
    unsaved_changes = true;
    config_valid = true;
}
