#include <vector>
#include <cstdint>
#include <string>
#include "dcc_types.h"

std::vector<raw_dcc_cmd_t> queued_commands;
bool track_power_states[2] = {false, false};
std::vector<uint64_t> sent_track_packets;
uint32_t mock_adc_reading = 0;
uint32_t mock_time_ms = 1000;
std::vector<std::string> uart_output_log;
