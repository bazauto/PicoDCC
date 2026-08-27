/*
    Configuration storage for PicoDCC using last 4KB flash sector.
    Stores calibration values and tunable parameters in non-volatile memory.
    Survives firmware updates if linker script reserves the last sector.
*/
#ifndef PICO_CONFIG_STORAGE_H
#define PICO_CONFIG_STORAGE_H

#include <stdint.h>
#include <stddef.h>  // For size_t

// Configuration stored in last 4KB flash sector (0x103FF000)
// For 4MB flash: 4 * 1024 * 1024 - 4096 = 0x3FF000 offset from flash base
#define CONFIG_FLASH_OFFSET (4 * 1024 * 1024 - 4096)
#define CONFIG_FLASH_SIZE 4096
#define CONFIG_MAGIC 0x50444343  // "PDCC" in hex
#define CONFIG_VERSION 1

// Factory default values
#define DEFAULT_ADC_TO_MA 0.0488f        // Must be calibrated per hardware
#define DEFAULT_ACK_THRESHOLD 60.0f      // 60mA per NMRA S-9.2.3
#define DEFAULT_ACK_MIN_DURATION 5.0f    // 5ms minimum ACK pulse
#define DEFAULT_ACK_MAX_DURATION 7.0f    // 7ms maximum ACK pulse
#define DEFAULT_BASELINE_CURRENT 10.0f   // 10mA typical decoder idle
#define DEFAULT_MAIN_CURRENT_LIMIT 3000  // 3A for main track
#define DEFAULT_PROG_CURRENT_LIMIT 250   // 250mA for programming track

typedef struct {
    uint32_t magic;              // Magic number for validation (0x50444343)
    uint32_t version;            // Structure version
    
    // CV Programming Calibration
    float adc_to_ma_conversion;     // ADC counts to mA conversion factor
    float ack_threshold_ma;         // ACK detection threshold (50-70mA)
    float ack_min_duration_ms;      // Minimum ACK pulse duration
    float ack_max_duration_ms;      // Maximum ACK pulse duration
    float baseline_current_ma;      // Programming track baseline current
    
    // Track Current Limits
    uint16_t main_track_current_limit_ma;   // Main track overcurrent limit
    uint16_t prog_track_current_limit_ma;   // Prog track overcurrent limit
    
    // Reserved for future use (pad to 4KB sector size)
    uint8_t reserved[4060];
    
    uint32_t checksum;           // CRC32 of all data above
} pico_config_t;

// Size of one flash page. `flash_range_program` requires its byte count to be a
// multiple of this; the SDK hard-asserts otherwise.
#define CONFIG_FLASH_PAGE_SIZE 256

// The struct is handed to `flash_range_program` whole, so its size is not a detail --
// it is a precondition of the SDK call, and the SDK checks it by hard-asserting.
//
// That assert fires at the worst possible moment: inside the critical section, with
// interrupts off, Core 1 already halted by `multicore_lockout_start_blocking()`, and
// the sector already erased. Core 1 never resumes, so the DCC signal never resumes --
// and per rule 1 in CLAUDE.md, decoders that lose the signal fall back to DC mode,
// which on a powered track is full speed. The config is left erased as well.
//
// `reserved` was 100 bytes short of the sector it claimed to pad to, so every save()
// ever attempted took that path. Nothing caught it because save() is stubbed out
// entirely under TEST_BUILD, so the 11 storage tests exercised the mock.
//
// These asserts are the guard, and they are deliberately compile-time: they fail in
// BOTH build modes, including the host build CI runs, which is what makes the sizing
// testable at all. If you add a field above, shrink `reserved` by the same number of
// bytes and the build will tell you when you have it right.
static_assert(sizeof(pico_config_t) == CONFIG_FLASH_SIZE,
              "pico_config_t must exactly fill the 4KB config sector -- adjust reserved[]");
static_assert(CONFIG_FLASH_SIZE % CONFIG_FLASH_PAGE_SIZE == 0,
              "config sector must be a whole number of flash pages");

// Runtime configuration (RAM-based, volatile)
typedef struct {
    float ack_threshold_ma;         // ACK detection threshold
    float ack_min_duration_ms;      // Minimum ACK pulse duration
    float ack_max_duration_ms;      // Maximum ACK pulse duration
} runtime_config_t;

// Configuration management class with hybrid architecture
class PicoConfigStorage {
private:
    pico_config_t config;           // Persistent flash configuration
    runtime_config_t runtime;       // Runtime adjustable parameters
    bool config_valid;
    bool unsaved_changes;           // Track if runtime differs from flash
    
    uint32_t calculateCRC32(const uint8_t *data, size_t length);
    bool validateConfig(const pico_config_t *cfg);
    
public:
    PicoConfigStorage();
    
    // Load configuration from flash (initializes runtime from flash)
    bool load();
    
    // Save configuration to flash (persists runtime to flash)
    // REQUIRES: Layout Maintenance Mode with main track power OFF
    // Blocks ~410ms, halts both cores
    bool save();
    
    // Reset to factory defaults
    void resetToDefaults();
    
    // Discard unsaved runtime changes (restore from flash)
    void discardChanges();
    
    // Getters - runtime values (may differ from flash if changed)
    float getACKThreshold() const { return runtime.ack_threshold_ma; }
    float getACKMinDuration() const { return runtime.ack_min_duration_ms; }
    float getACKMaxDuration() const { return runtime.ack_max_duration_ms; }
    
    // Getters - flash-only values (calibration, limits)
    float getADCToMAConversion() const;
    float getBaselineCurrent() const;
    uint16_t getMainTrackCurrentLimit() const;
    uint16_t getProgTrackCurrentLimit() const;
    
    // Runtime setters (immediate effect, marks unsaved)
    void setACKThreshold(float value);
    void setACKMinDuration(float value);
    void setACKMaxDuration(float value);
    
    // Flash setters (requires save() to persist)
    void setADCToMAConversion(float value);
    void setBaselineCurrent(float value);
    void setMainTrackCurrentLimit(uint16_t value);
    void setProgTrackCurrentLimit(uint16_t value);
    
    // Status
    bool isValid() const { return config_valid; }
    bool hasUnsavedChanges() const { return unsaved_changes; }
    
    // Get raw config for diagnostic/export
    const pico_config_t* getConfig() const { return &config; }
};

#endif // PICO_CONFIG_STORAGE_H
