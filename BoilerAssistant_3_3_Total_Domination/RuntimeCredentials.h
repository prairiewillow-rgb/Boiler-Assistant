/*
 * Boiler Assistant - RuntimeCredentials
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#pragma once

struct RuntimeCredentials {
    bool hasCredentials = false;

    char ssid[32]       = {0};
    char pass[64]       = {0};

    char mqttServer[64] = {0};
    char mqttUser[32]   = {0};
    char mqttPass[64]   = {0};

    // Control/API password. Kept at the same struct position for EEPROM compatibility.
    char controlPass[32] = {0};

    char displayName[32] = {0};
};

extern RuntimeCredentials runtimeCreds;
