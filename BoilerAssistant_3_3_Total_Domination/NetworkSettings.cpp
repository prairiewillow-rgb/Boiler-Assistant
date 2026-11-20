#include "NetworkSettings.h"

NetworkSettings networkSettings;

bool network_parseIPv4(const char* text, uint8_t* octets) {
    for (uint8_t i = 0; i < 4; ++i) {
        unsigned value = 0;
        uint8_t digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text++ - '0');
            if (++digits > 3 || value > 255) return false;
        }
        if (digits == 0) return false;
        octets[i] = value;
        if (i < 3) {
            if (*text++ != '.') return false;
        } else if (*text != '\0') return false;
    }
    return true;
}

static uint32_t network_number(const uint8_t* octets) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < 4; ++i) value = (value << 8) | octets[i];
    return value;
}

static bool network_unicast(const uint8_t* octets) {
    return octets[0] != 0 && octets[0] != 127 && octets[0] < 224;
}

const char* network_validate(const NetworkSettings& settings) {
    if (settings.fixed > 1) return "Unknown address mode.";
    if (!settings.fixed) return nullptr;
    if (!network_unicast(settings.ip) || !network_unicast(settings.gateway) ||
        !network_unicast(settings.dns))
        return "IP, gateway and DNS must be usable unicast IPv4 addresses.";
    uint32_t mask = network_number(settings.subnet);
    uint32_t hosts = ~mask;
    if (mask == 0 || hosts < 3 || (hosts & (hosts + 1)) != 0)
        return "Subnet must be contiguous, with room for both unit and gateway.";
    uint32_t ip = network_number(settings.ip);
    uint32_t gateway = network_number(settings.gateway);
    if ((ip & mask) != (gateway & mask) || ip == gateway)
        return "IP and gateway must be different addresses on the same subnet.";
    if ((ip & hosts) == 0 || (ip & hosts) == hosts ||
        (gateway & hosts) == 0 || (gateway & hosts) == hosts)
        return "IP and gateway cannot be network or broadcast addresses.";
    return nullptr;
}
