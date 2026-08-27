# Non-Volatile Storage Options for Raspberry Pi Pico

## Executive Summary

For tunable parameters (ACK detection thresholds, calibration values, etc.), the Raspberry Pi Pico offers several non-volatile storage options. The **recommended solution** is using the **last sector of flash memory** with the Pico SDK's `hardware_flash` API, which survives firmware updates if properly managed.

## Storage Requirements for PicoDCC

### Parameters Needing Non-Volatile Storage

**CV Programming Calibration:**
- `adc_to_ma_conversion` (float, 4 bytes) - Current sensor calibration factor
- `ack_threshold_ma` (float, 4 bytes) - ACK detection threshold (default 60mA, tunable 50-70mA)
- `ack_min_duration_ms` (float, 4 bytes) - Minimum ACK pulse duration (default 5ms)
- `ack_max_duration_ms` (float, 4 bytes) - Maximum ACK pulse duration (default 7ms)

**Track Configuration:**
- `main_track_current_limit_ma` (uint16_t, 2 bytes) - Overcurrent threshold
- `prog_track_current_limit_ma` (uint16_t, 2 bytes) - Programming track limit
- `baseline_current_ma` (float, 4 bytes) - Idle current baseline

**System Configuration:**
- `config_version` (uint32_t, 4 bytes) - Structure version for validation
- `config_checksum` (uint32_t, 4 bytes) - CRC32 for data integrity

**Total Storage Needed**: ~36 bytes (fits easily in single 4KB flash sector)

## Option 1: Flash Memory (Last Sector) ⭐ RECOMMENDED

### Overview
The RP2040 has 2MB of flash memory. By reserving the last 4KB sector for configuration, you can store persistent data that survives firmware updates (if firmware doesn't overflow into that sector).

### Advantages
✅ **No external hardware required**  
✅ **Large capacity** (4KB sector = plenty of space)  
✅ **Survives power loss** (true non-volatile)  
✅ **Fast read access** (memory-mapped reads)  
✅ **Can survive firmware updates** (if last sector reserved)  
✅ **Built-in SDK support** (`hardware_flash` API)  

### Disadvantages
⚠️ **Write endurance limited** (~10,000 erase cycles per sector)  
⚠️ **Sector erase required** (must erase entire 4KB before write)  
⚠️ **Write requires disabling interrupts** (~1ms blocking operation)  
⚠️ **Firmware must not overflow** (linker script must reserve space)  
⚠️ **Both cores must be halted** during flash write (Core 1 impact)

### Technical Implementation

**Linker Script Modification** (`pico_flash_region.ld`):
```ld
/* Reserve last 4KB sector for configuration */
MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4092k     /* Firmware */
    CONFIG(r) : ORIGIN = 0x103FF000, LENGTH = 4k          /* Last 4KB sector */
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 264k
}
```

**Configuration Structure** (`lib/pico_config_storage.h`):
```cpp
#ifndef PICO_CONFIG_STORAGE_H
#define PICO_CONFIG_STORAGE_H

#include <stdint.h>
#include <string.h>

// Configuration stored in last 4KB flash sector
#define CONFIG_FLASH_OFFSET (2 * 1024 * 1024 - 4096)  // Last 4KB of 2MB flash
#define CONFIG_FLASH_SIZE 4096
#define CONFIG_MAGIC 0x50444343  // "PDCC" in hex
#define CONFIG_VERSION 1

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
    
    // Reserved for future use
    uint8_t reserved[4060];      // Pad to 4KB sector size
    
    uint32_t checksum;           // CRC32 of all data above
} pico_config_t;

// The size is a precondition, not a detail: save() hands the whole struct to
// flash_range_program, which requires a multiple of the 256-byte page and
// hard-asserts otherwise -- inside the critical section, with Core 1 halted and
// the sector already erased. reserved[] was 100 bytes short of the sector it
// claimed to pad to, so every save faulted there (#13). A static_assert in
// pico_config_storage.h now fails the build in both modes if it drifts again.

// Configuration management class
class PicoConfigStorage {
private:
    pico_config_t config;
    bool config_valid;
    
    uint32_t calculateCRC32(const uint8_t *data, size_t length);
    bool validateConfig(const pico_config_t *cfg);
    
public:
    PicoConfigStorage();
    
    // Load configuration from flash
    bool load();
    
    // Save configuration to flash (blocks ~1ms)
    bool save();
    
    // Reset to factory defaults
    void resetToDefaults();
    
    // Getters with defaults if config invalid
    float getADCToMAConversion() const;
    float getACKThreshold() const;
    float getACKMinDuration() const;
    float getACKMaxDuration() const;
    float getBaselineCurrent() const;
    uint16_t getMainTrackCurrentLimit() const;
    uint16_t getProgTrackCurrentLimit() const;
    
    // Setters (call save() after to persist)
    void setADCToMAConversion(float value);
    void setACKThreshold(float value);
    void setACKMinDuration(float value);
    void setACKMaxDuration(float value);
    void setBaselineCurrent(float value);
    void setMainTrackCurrentLimit(uint16_t value);
    void setProgTrackCurrentLimit(uint16_t value);
    
    // Status
    bool isValid() const { return config_valid; }
};

#endif // PICO_CONFIG_STORAGE_H
```

**Implementation** (`lib/pico_config_storage.cpp`):
```cpp
#include "pico_config_storage.h"
#include "pico_diagnostic.h"

#ifdef TEST_BUILD
#include "../test/mocks.h"
#else
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#endif

// Factory default values
#define DEFAULT_ADC_TO_MA 0.0488f        // Must be calibrated per hardware
#define DEFAULT_ACK_THRESHOLD 60.0f      // 60mA per NMRA S-9.2.3
#define DEFAULT_ACK_MIN_DURATION 5.0f    // 5ms minimum
#define DEFAULT_ACK_MAX_DURATION 7.0f    // 7ms maximum
#define DEFAULT_BASELINE_CURRENT 10.0f   // 10mA typical decoder idle
#define DEFAULT_MAIN_CURRENT_LIMIT 3000  // 3A for main track
#define DEFAULT_PROG_CURRENT_LIMIT 250   // 250mA for prog track

PicoConfigStorage::PicoConfigStorage() : config_valid(false) {
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
    config_valid = true;
}

bool PicoConfigStorage::load() {
#ifdef TEST_BUILD
    // In test mode, use default values
    resetToDefaults();
    return true;
#else
    // Read from flash (memory-mapped, no special API needed)
    const pico_config_t *flash_config = 
        (const pico_config_t*)(XIP_BASE + CONFIG_FLASH_OFFSET);
    
    // Validate configuration
    if (validateConfig(flash_config)) {
        memcpy(&config, flash_config, sizeof(pico_config_t));
        config_valid = true;
        LOG_INFO(COMPONENT_SYSTEM, "Configuration loaded from flash");
        return true;
    } else {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid flash config, using defaults");
        resetToDefaults();
        return false;
    }
#endif
}

bool PicoConfigStorage::save() {
#ifdef TEST_BUILD
    // In test mode, pretend to save
    LOG_INFO(COMPONENT_SYSTEM, "Mock: Configuration saved");
    return true;
#else
    // Update checksum before saving
    config.checksum = calculateCRC32((const uint8_t*)&config, 
                                     sizeof(pico_config_t) - sizeof(uint32_t));
    
    // Flash programming requires:
    // 1. Halt Core 1 (if running)
    // 2. Disable interrupts
    // 3. Erase sector
    // 4. Program sector
    // 5. Re-enable interrupts
    // 6. Resume Core 1
    
    // Check if Core 1 is running (multicore_lockout_victim_init already called)
    bool core1_was_running = multicore_lockout_victim_is_initialized(1);
    
    // Halt Core 1 if running (track operations will pause ~1ms)
    if (core1_was_running) {
        multicore_lockout_start_blocking();
    }
    
    // Disable interrupts for flash write
    uint32_t interrupts = save_and_disable_interrupts();
    
    // Erase sector (4KB)
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    
    // Program sector (must write in 256-byte pages)
    flash_range_program(CONFIG_FLASH_OFFSET, (const uint8_t*)&config, 
                       sizeof(pico_config_t));
    
    // Re-enable interrupts
    restore_interrupts(interrupts);
    
    // Resume Core 1
    if (core1_was_running) {
        multicore_lockout_end_blocking();
    }
    
    LOG_INFO(COMPONENT_SYSTEM, "Configuration saved to flash");
    
    // Verify write
    return load();
#endif
}

bool PicoConfigStorage::validateConfig(const pico_config_t *cfg) {
    // Check magic number
    if (cfg->magic != CONFIG_MAGIC) {
        LOG_WARNING(COMPONENT_SYSTEM, "Invalid config magic");
        return false;
    }
    
    // Check version (support only current version for now)
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
    
    return true;
}

uint32_t PicoConfigStorage::calculateCRC32(const uint8_t *data, size_t length) {
    // CRC32 implementation (standard polynomial 0x04C11DB7)
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

// Getter implementations with defaults
float PicoConfigStorage::getADCToMAConversion() const {
    return config_valid ? config.adc_to_ma_conversion : DEFAULT_ADC_TO_MA;
}

float PicoConfigStorage::getACKThreshold() const {
    return config_valid ? config.ack_threshold_ma : DEFAULT_ACK_THRESHOLD;
}

float PicoConfigStorage::getACKMinDuration() const {
    return config_valid ? config.ack_min_duration_ms : DEFAULT_ACK_MIN_DURATION;
}

float PicoConfigStorage::getACKMaxDuration() const {
    return config_valid ? config.ack_max_duration_ms : DEFAULT_ACK_MAX_DURATION;
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

// Setter implementations
void PicoConfigStorage::setADCToMAConversion(float value) {
    config.adc_to_ma_conversion = value;
    config_valid = true;
}

void PicoConfigStorage::setACKThreshold(float value) {
    config.ack_threshold_ma = value;
    config_valid = true;
}

void PicoConfigStorage::setACKMinDuration(float value) {
    config.ack_min_duration_ms = value;
    config_valid = true;
}

void PicoConfigStorage::setACKMaxDuration(float value) {
    config.ack_max_duration_ms = value;
    config_valid = true;
}

void PicoConfigStorage::setBaselineCurrent(float value) {
    config.baseline_current_ma = value;
    config_valid = true;
}

void PicoConfigStorage::setMainTrackCurrentLimit(uint16_t value) {
    config.main_track_current_limit_ma = value;
    config_valid = true;
}

void PicoConfigStorage::setProgTrackCurrentLimit(uint16_t value) {
    config.prog_track_current_limit_ma = value;
    config_valid = true;
}
```

### Usage Example in PicoDccController

```cpp
// Global configuration instance
PicoConfigStorage global_config;

void setup() {
    // Load configuration on boot
    if (!global_config.load()) {
        LOG_WARNING(COMPONENT_SYSTEM, "Using factory defaults");
    }
    
    // Pass to programmer
    float calibration = global_config.getADCToMAConversion();
    programmer.setCalibration(calibration);
}

// DCC-EX configuration commands
void handleConfigCommand(const char *cmd) {
    // <D CONFIG SET ADC_MA 0.0488>
    if (strstr(cmd, "ADC_MA")) {
        float value = parseFloat(cmd);
        global_config.setADCToMAConversion(value);
        global_config.save();  // Persist to flash
        DCCEX_RESPONSE("<D CONFIG ADC_MA %0.4f>", value);
    }
    
    // <D CONFIG GET ADC_MA>
    if (strstr(cmd, "GET ADC_MA")) {
        DCCEX_RESPONSE("<D CONFIG ADC_MA %0.4f>", 
                      global_config.getADCToMAConversion());
    }
    
    // <D CONFIG RESET>
    if (strstr(cmd, "RESET")) {
        global_config.resetToDefaults();
        global_config.save();
        DCCEX_RESPONSE("<D CONFIG RESET OK>");
    }
}
```

### Flash Write Performance Impact

**Timing:**
- Sector erase: ~400ms (4KB sector)
- Page program: ~0.6ms per 256-byte page (16 pages = 9.6ms for 4KB)
- **Total write time**: ~410ms (blocks both cores)

**Mitigation Strategy:**
- Only write when user explicitly changes config (rare operation)
- Display warning: "Saving configuration, tracks paused..."
- Main track locomotives will coast during write (acceptable for infrequent config changes)

### Surviving Firmware Updates

**Strategy 1: Linker Script Reservation** (Recommended):
```cmake
# CMakeLists.txt
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--defsym=CONFIG_RESERVED=4096")
```

This ensures firmware never grows into last 4KB sector.

**Strategy 2: Manual Upload Process**:
```bash
# Upload firmware (leaves last 4KB untouched)
picotool load PicoDCC.uf2 --offset 0x0 --verify

# Configuration sector remains intact at 0x101FF000
```

**Strategy 3: Configuration Backup/Restore**:
```cpp
// DCC-EX command: <D CONFIG EXPORT>
// Outputs configuration as human-readable text
// User can re-import after firmware update: <D CONFIG IMPORT ...>
```

---

## Option 2: External I2C EEPROM

### Overview
Add external EEPROM chip (e.g., 24LC256 = 32KB) via I2C bus.

### Advantages
✅ **High write endurance** (1,000,000+ cycles)  
✅ **True non-volatile** (no firmware interaction)  
✅ **Non-blocking writes** (can use async I2C)  
✅ **Immune to firmware updates**  
✅ **Large capacity** (32KB typical)

### Disadvantages
❌ **Requires external hardware** (BOM cost ~$0.50)  
❌ **PCB redesign needed** (add I2C connections)  
❌ **Slower access** (~1ms per byte vs instant flash read)  
❌ **I2C bus complexity** (pull-ups, clock speed, etc.)

### Implementation
```cpp
#include <hardware/i2c.h>

#define EEPROM_I2C_ADDR 0x50
#define EEPROM_I2C i2c0

void eeprom_write_byte(uint16_t addr, uint8_t data) {
    uint8_t buf[3] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), data};
    i2c_write_blocking(EEPROM_I2C, EEPROM_I2C_ADDR, buf, 3, false);
    sleep_ms(5);  // EEPROM write cycle time
}

uint8_t eeprom_read_byte(uint16_t addr) {
    uint8_t buf[2] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
    i2c_write_blocking(EEPROM_I2C, EEPROM_I2C_ADDR, buf, 2, true);
    uint8_t data;
    i2c_read_blocking(EEPROM_I2C, EEPROM_I2C_ADDR, &data, 1, false);
    return data;
}
```

---

## Option 3: External SPI Flash

### Overview
Add external SPI flash chip (e.g., W25Q32 = 4MB) for large storage needs.

### Advantages
✅ **Very large capacity** (4MB+)  
✅ **Fast read/write** (80 MHz SPI)  
✅ **Immune to firmware updates**  
✅ **Moderate write endurance** (100,000 cycles)

### Disadvantages
❌ **Requires external hardware**  
❌ **PCB redesign needed**  
❌ **More complex driver** (SPI protocol, wear leveling)  
❌ **Overkill for small config data**

---

## Option 4: SD Card

### Overview
Use SD card via SPI interface for mass storage.

### Advantages
✅ **Massive capacity** (GB range)  
✅ **User-accessible** (can edit config on PC)  
✅ **File system support** (FatFS)

### Disadvantages
❌ **Requires SD card slot** (mechanical component)  
❌ **Complex driver** (FatFS, wear leveling)  
❌ **Unreliable connection** (card removal)  
❌ **Overkill for PicoDCC use case**

---

## Option 5: Pico W Filesystem (LittleFS)

### Overview
If using Pico W (WiFi variant), can use LittleFS on flash.

### Advantages
✅ **Wear leveling built-in**  
✅ **File system abstraction**  
✅ **Survives firmware updates** (separate partition)

### Disadvantages
⚠️ **Only on Pico W** (not standard Pico)  
⚠️ **More complex setup**

---

## Comparison Table

| Option | NV Storage | Cost | Complexity | Write Endurance | Survives FW Update | Best For |
|--------|-----------|------|------------|----------------|-------------------|----------|
| **Flash (Last Sector)** | ✅ | $0 | Low | 10K cycles | ✅ (with care) | **Configuration data** |
| I2C EEPROM | ✅ | +$0.50 | Medium | 1M cycles | ✅ | Frequent writes |
| SPI Flash | ✅ | +$1.00 | High | 100K cycles | ✅ | Large storage |
| SD Card | ✅ | +$5.00 | Very High | High | ✅ | User files |
| LittleFS (Pico W) | ✅ | +$1.00 | Medium | 100K cycles | ✅ | File-based config |

---

## Recommendation: Flash Memory (Last Sector)

### Why This is Best for PicoDCC

1. **No hardware changes needed** - works on existing PCB
2. **Sufficient endurance** - 10,000 writes = 27 years @ 1 write/day
3. **Simple implementation** - well-documented SDK API
4. **Survives firmware updates** - with linker script reservation
5. **Fast reads** - memory-mapped, instant access
6. **Adequate capacity** - 4KB sector holds all config parameters with room to grow

### Write Endurance Analysis

**Configuration writes are rare:**
- Initial calibration: 1 write
- Threshold tuning: 5-10 writes during setup
- Normal operation: 0 writes (read-only)
- **Estimated lifetime**: 10,000 cycles ÷ 20 config changes = 500 devices worth

Even if you recalibrate every week, 10,000 cycles = **192 years** of operation.

### Migration Path (If Needed)

If write endurance becomes an issue (unlikely):
1. Add I2C EEPROM chip to PCB (next revision)
2. Abstract storage behind interface (PicoConfigStorage class already does this)
3. Change backend from flash to EEPROM (transparent to application)

---

## Implementation Plan

### Phase 1: Create Storage Infrastructure (Week 1)
1. Create `lib/pico_config_storage.h` and `.cpp` (code provided above)
2. Modify linker script to reserve last 4KB sector
3. Add CMake configuration for reserved space
4. Create unit tests (mock flash operations)
5. Add to build system

### Phase 2: Integrate with CV Programmer (Week 2)
1. Add `PicoConfigStorage` instance to `PicoDccController`
2. Load config on boot
3. Pass calibration values to `PicoDccProgrammer`
4. Use thresholds in ACK detection logic

### Phase 3: DCC-EX Configuration Commands (Week 3)
1. Add `<D CONFIG SET param value>` command parser
2. Add `<D CONFIG GET param>` command parser
3. Add `<D CONFIG SAVE>` command (persist to flash)
4. Add `<D CONFIG RESET>` command (factory defaults)
5. Add `<D CONFIG EXPORT>` command (backup to text)

### Phase 4: Calibration Workflow (Week 4)
1. Add `<D CAL START>` command (enter calibration mode)
2. Add `<D CAL ADC>` command (measure current ADC reading)
3. Add `<D CAL SET load_ma adc_reading>` (calculate conversion factor)
4. Add `<D CAL SAVE>` (persist calibration)
5. Document calibration procedure

### Phase 5: Testing & Documentation (Week 5)
1. Test flash write/read cycle
2. Verify config survives power cycle
3. Verify config survives firmware update (with reserved linker)
4. Document all configuration commands
5. Create calibration guide for users

---

## Example DCC-EX Configuration Commands

### Get Current Configuration
```
<D CONFIG GET ADC_MA>
  → <D CONFIG ADC_MA 0.0488>

<D CONFIG GET ACK_THRESH>
  → <D CONFIG ACK_THRESH 60.0>

<D CONFIG GET ALL>
  → <D CONFIG ADC_MA 0.0488 ACK_THRESH 60.0 ACK_MIN 5.0 ACK_MAX 7.0>
```

### Set Configuration (Not Persisted Until SAVE)
```
<D CONFIG SET ADC_MA 0.0512>
  → <D CONFIG ADC_MA 0.0512 OK>

<D CONFIG SET ACK_THRESH 55.0>
  → <D CONFIG ACK_THRESH 55.0 OK>
```

### Save Configuration to Flash
```
<D CONFIG SAVE>
  → <D CONFIG SAVE OK 410ms>  (includes write time warning)
```

### Reset to Factory Defaults
```
<D CONFIG RESET>
  → <D CONFIG RESET OK>
```

### Calibration Workflow
```
# Step 1: Connect 100mA calibration load
<1 PROG>
  → <p1 PROG>

# Step 2: Read current ADC value
<D CAL ADC>
  → <D CAL ADC 2048>

# Step 3: Calculate and set conversion factor
<D CAL SET 100.0 2048>
  → <D CAL ADC_MA 0.0488 OK>

# Step 4: Save calibration
<D CONFIG SAVE>
  → <D CONFIG SAVE OK>

# Step 5: Verify
<D CONFIG GET ADC_MA>
  → <D CONFIG ADC_MA 0.0488>
```

---

## Next Steps

1. **Implement `PicoConfigStorage` class** (code provided above)
2. **Modify linker script** to reserve last 4KB flash sector
3. **Add to CMakeLists.txt** (new library target)
4. **Create unit tests** with mocked flash operations
5. **Integrate with `PicoDccController`** (load config on boot)
6. **Add DCC-EX configuration commands** to command parser

**Total implementation time**: ~2-3 weeks alongside CV programming work

Would you like me to:
1. **Start implementing the flash storage system** (create actual files)?
2. **Modify the linker script** to reserve the last sector?
3. **Add configuration loading to PicoDccController**?

This solution provides persistent, tunable configuration storage with **zero hardware changes** and excellent integration with your existing DCC-EX command system.
