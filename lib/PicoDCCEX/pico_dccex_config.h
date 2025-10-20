/*
    DCC-EX Configuration and Calibration Command Handler
    Provides commands for:
    - Configuration storage management
    - Calibration workflow for CV programming
    - System diagnostics and parameter tuning
*/
#ifndef PICO_DCCEX_CONFIG_H
#define PICO_DCCEX_CONFIG_H

#include "../PicoConfigStorage/pico_config_storage.h"
#include "../dccex_communication.h"

#ifdef TEST_BUILD
#include "../../test/mocks.h"
#else
#include <hardware/adc.h>
#include <pico/stdlib.h>
#endif

class PicoDccExConfig {
private:
    PicoConfigStorage *config;
    
public:
    PicoDccExConfig(PicoConfigStorage *cfg);
    
    // Process configuration commands
    // Returns true if command was handled (config command)
    bool processCommand(const char *buffer);
    
private:
    // Command handlers
    void handleConfigGet(const char *param);
    void handleConfigSet(const char *param, const char *value);
    void handleConfigSave();
    void handleConfigReset();
    void handleConfigExport();
    
    // Calibration workflow
    void handleCalStart();
    void handleCalADC(uint8_t adc_channel);
    void handleCalSet(float load_ma, uint16_t adc_reading);
    void handleCalSave();
    
    // Helper functions
    bool parseFloat(const char *str, float *result);
    bool parseUInt16(const char *str, uint16_t *result);
};

#endif // PICO_DCCEX_CONFIG_H
