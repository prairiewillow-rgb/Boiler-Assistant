#pragma once

#include <stdint.h>

// Separate from the byte-for-byte legacy RuntimeCredentials EEPROM record.
struct NetworkSettings {
    uint8_t fixed = 0;
    uint8_t ip[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t subnet[4] = {0};
    uint8_t dns[4] = {0};
};

extern NetworkSettings networkSettings;
bool network_parseIPv4(const char* text, uint8_t* octets);
const char* network_validate(const NetworkSettings& settings);
