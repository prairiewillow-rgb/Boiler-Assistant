/*
 * Boiler Assistant - WiFiAPI
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#pragma once

#include <stddef.h>

// Initialize WiFi + HTTP JSON API (non-blocking)
void wifiapi_init();

// Releases the listening socket so the WiFi bridge is free (used before OTA).
void wifiapi_stop();

// Run WiFi retry + HTTP server loop (non-blocking)
void wifiapi_loop();
int wifiapi_statusText(char* out, size_t size);  // web counters for the NET line
const char* wifiapi_alertText();  // dashboard alert line ("" when none)
