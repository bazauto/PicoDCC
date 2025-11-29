#include "pico_dccex_config.h"
#include "../pico_diagnostic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TEST_BUILD
#include <hardware/adc.h>
#endif

PicoDccExConfig::PicoDccExConfig(PicoConfigStorage *cfg) : config(cfg) {
}

bool PicoDccExConfig::processCommand(const char *buffer) {
    // Check if this is a configuration or calibration command
    // Configuration commands: <D CONFIG ...>
    // Calibration commands: <D CAL ...>
    
    if (buffer[0] != 'D' && buffer[0] != 'd') {
        return false;  // Not a diagnostic/config command
    }
    
    // Skip 'D' and whitespace
    const char *cmd = buffer + 1;
    while (*cmd == ' ') cmd++;
    
    // Check for CONFIG commands
    if (strncmp(cmd, "CONFIG", 6) == 0) {
        cmd += 6;
        while (*cmd == ' ') cmd++;
        
        if (strncmp(cmd, "GET", 3) == 0) {
            cmd += 3;
            while (*cmd == ' ') cmd++;
            handleConfigGet(cmd);
            return true;
        }
        else if (strncmp(cmd, "SET", 3) == 0) {
            cmd += 3;
            while (*cmd == ' ') cmd++;
            
            // Parse parameter and value
            char param[32] = {0};
            char value_str[32] = {0};
            if (sscanf(cmd, "%31s %31s", param, value_str) == 2) {
                handleConfigSet(param, value_str);
            } else {
                DCCEX_RESPONSE("<X>");  // Invalid command
            }
            return true;
        }
        else if (strncmp(cmd, "SAVE", 4) == 0) {
            handleConfigSave();
            return true;
        }
        else if (strncmp(cmd, "RESET", 5) == 0) {
            handleConfigReset();
            return true;
        }
        else if (strncmp(cmd, "EXPORT", 6) == 0) {
            handleConfigExport();
            return true;
        }
    }
    
    // Check for CAL commands
    if (strncmp(cmd, "CAL", 3) == 0) {
        cmd += 3;
        while (*cmd == ' ') cmd++;
        
        if (strncmp(cmd, "START", 5) == 0) {
            handleCalStart();
            return true;
        }
        else if (strncmp(cmd, "ADC", 3) == 0) {
            cmd += 3;
            while (*cmd == ' ') cmd++;
            
            // Optional ADC channel parameter (default 1 for prog track)
            uint8_t channel = 1;
            if (*cmd >= '0' && *cmd <= '3') {
                channel = *cmd - '0';
            }
            handleCalADC(channel);
            return true;
        }
        else if (strncmp(cmd, "SET", 3) == 0) {
            cmd += 3;
            while (*cmd == ' ') cmd++;
            
            // Parse load current and ADC reading
            float load_ma;
            uint16_t adc_reading;
            char load_str[16] = {0};
            char adc_str[16] = {0};
            
            if (sscanf(cmd, "%15s %15s", load_str, adc_str) == 2) {
                if (parseFloat(load_str, &load_ma) && parseUInt16(adc_str, &adc_reading)) {
                    handleCalSet(load_ma, adc_reading);
                } else {
                    DCCEX_RESPONSE("<X>");  // Parse error
                }
            } else {
                DCCEX_RESPONSE("<X>");  // Invalid format
            }
            return true;
        }
        else if (strncmp(cmd, "SAVE", 4) == 0) {
            handleCalSave();
            return true;
        }
    }
    
    return false;  // Not a config/cal command
}

void PicoDccExConfig::handleConfigGet(const char *param) {
    char response[128];
    
    if (strcmp(param, "ADC_MA") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG ADC_MA %.4f>",
                config->getADCToMAConversion());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "ACK_THRESH") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG ACK_THRESH %.1f>",
                config->getACKThreshold());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "ACK_MIN") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG ACK_MIN %.1f>",
                config->getACKMinDuration());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "ACK_MAX") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG ACK_MAX %.1f>",
                config->getACKMaxDuration());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "BASELINE") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG BASELINE %.1f>",
                config->getBaselineCurrent());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "MAIN_LIMIT") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG MAIN_LIMIT %u>",
                config->getMainTrackCurrentLimit());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "PROG_LIMIT") == 0 || strcmp(param, "") == 0) {
        snprintf(response, sizeof(response), "<D CONFIG PROG_LIMIT %u>",
                config->getProgTrackCurrentLimit());
        DCCEX_RESPONSE(response);
    }
    else if (strcmp(param, "ALL") == 0) {
        // Export all parameters
        handleConfigExport();
    }
    else {
        DCCEX_RESPONSE("<X>");  // Unknown parameter
    }
}

void PicoDccExConfig::handleConfigSet(const char *param, const char *value_str) {
    float float_val;
    uint16_t uint_val;
    char response[128];
    
    if (strcmp(param, "ADC_MA") == 0) {
        if (parseFloat(value_str, &float_val)) {
            config->setADCToMAConversion(float_val);
            snprintf(response, sizeof(response), "<D CONFIG ADC_MA %.4f OK>", float_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "ACK_THRESH") == 0) {
        if (parseFloat(value_str, &float_val)) {
            config->setACKThreshold(float_val);
            snprintf(response, sizeof(response), "<D CONFIG ACK_THRESH %.1f OK>", float_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "ACK_MIN") == 0) {
        if (parseFloat(value_str, &float_val)) {
            config->setACKMinDuration(float_val);
            snprintf(response, sizeof(response), "<D CONFIG ACK_MIN %.1f OK>", float_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "ACK_MAX") == 0) {
        if (parseFloat(value_str, &float_val)) {
            config->setACKMaxDuration(float_val);
            snprintf(response, sizeof(response), "<D CONFIG ACK_MAX %.1f OK>", float_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "BASELINE") == 0) {
        if (parseFloat(value_str, &float_val)) {
            config->setBaselineCurrent(float_val);
            snprintf(response, sizeof(response), "<D CONFIG BASELINE %.1f OK>", float_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "MAIN_LIMIT") == 0) {
        if (parseUInt16(value_str, &uint_val)) {
            config->setMainTrackCurrentLimit(uint_val);
            snprintf(response, sizeof(response), "<D CONFIG MAIN_LIMIT %u OK>", uint_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else if (strcmp(param, "PROG_LIMIT") == 0) {
        if (parseUInt16(value_str, &uint_val)) {
            config->setProgTrackCurrentLimit(uint_val);
            snprintf(response, sizeof(response), "<D CONFIG PROG_LIMIT %u OK>", uint_val);
            DCCEX_RESPONSE(response);
        } else {
            DCCEX_RESPONSE("<X>");
        }
    }
    else {
        DCCEX_RESPONSE("<X>");  // Unknown parameter
    }
}

void PicoDccExConfig::handleConfigSave() {
    // Save configuration to flash
    // Warning: This blocks both cores for ~410ms
    DCCEX_RESPONSE("<D CONFIG SAVING>");
    
    bool success = config->save();
    
    if (success) {
        DCCEX_RESPONSE("<D CONFIG SAVE OK>");
    } else {
        DCCEX_RESPONSE("<D CONFIG SAVE FAILED>");
    }
}

void PicoDccExConfig::handleConfigReset() {
    config->resetToDefaults();
    DCCEX_RESPONSE("<D CONFIG RESET OK>");
}

void PicoDccExConfig::handleConfigExport() {
    char response[256];
    
    // Send all configuration parameters
    snprintf(response, sizeof(response),
            "<D CONFIG ALL ADC_MA=%.4f ACK_THRESH=%.1f ACK_MIN=%.1f ACK_MAX=%.1f "
            "BASELINE=%.1f MAIN_LIMIT=%u PROG_LIMIT=%u>",
            config->getADCToMAConversion(),
            config->getACKThreshold(),
            config->getACKMinDuration(),
            config->getACKMaxDuration(),
            config->getBaselineCurrent(),
            config->getMainTrackCurrentLimit(),
            config->getProgTrackCurrentLimit());
    
    DCCEX_RESPONSE(response);
}

void PicoDccExConfig::handleCalStart() {
    DCCEX_RESPONSE("<D CAL START OK>\n");
    DCCEX_RESPONSE("<D CAL MSG Connect 100mA calibration load to programming track>\n");
    DCCEX_RESPONSE("<D CAL MSG Enable programming track power: <1 PROG>>\n");
    DCCEX_RESPONSE("<D CAL MSG Read ADC value: <D CAL ADC>>\n");
    DCCEX_RESPONSE("<D CAL MSG Set calibration: <D CAL SET 100.0 <adc_value>>>\n");
    DCCEX_RESPONSE("<D CAL MSG Save calibration: <D CAL SAVE>>\n");
}

void PicoDccExConfig::handleCalADC(uint8_t adc_channel) {
#ifdef TEST_BUILD
    // In test mode, return mock ADC value
    extern uint32_t mock_adc_reading;
    char response[64];
    snprintf(response, sizeof(response), "<D CAL ADC %u VALUE %u>",
            adc_channel, mock_adc_reading);
    DCCEX_RESPONSE(response);
#else
    // Select and read ADC channel
    // adc_select_input(adc_channel);
    // uint16_t reading = adc_read();

    float current_sum = 0.0f;
    uint32_t sample_count = 50;
    for (uint32_t i = 0; i < sample_count; i++) {
        adc_select_input(adc_channel);
        current_sum += adc_read();
        sleep_us(1000);  // 1ms between samples
    }
    uint16_t reading = current_sum / static_cast<float>(sample_count);
    
    char response[64];
    snprintf(response, sizeof(response), "<D CAL ADC %u VALUE %u>",
            adc_channel, reading);
    DCCEX_RESPONSE(response);
#endif
}

void PicoDccExConfig::handleCalSet(float load_ma, uint16_t adc_reading) {
    if (adc_reading == 0) {
        DCCEX_RESPONSE("<D CAL ERROR Zero ADC reading>");
        return;
    }
    
    if (load_ma <= 0.0f || load_ma > 1000.0f) {
        DCCEX_RESPONSE("<D CAL ERROR Invalid load current>");
        return;
    }
    
    // Calculate conversion factor: mA per ADC count
    float conversion = load_ma / (float)adc_reading;
    config->setADCToMAConversion(conversion);
    
    char response[128];
    snprintf(response, sizeof(response),
            "<D CAL SET OK ADC_MA=%.4f (%.1fmA @ %u adc_reading)>",
            conversion, load_ma, adc_reading);
    DCCEX_RESPONSE(response);
}

void PicoDccExConfig::handleCalSave() {
    // Save calibration to flash
    handleConfigSave();
}

bool PicoDccExConfig::parseFloat(const char *str, float *result) {
    char *endptr;
    *result = strtof(str, &endptr);
    return (endptr != str && (*endptr == '\0' || *endptr == ' '));
}

bool PicoDccExConfig::parseUInt16(const char *str, uint16_t *result) {
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (endptr != str && (*endptr == '\0' || *endptr == ' ') &&
        val >= 0 && val <= 65535) {
        *result = (uint16_t)val;
        return true;
    }
    return false;
}
