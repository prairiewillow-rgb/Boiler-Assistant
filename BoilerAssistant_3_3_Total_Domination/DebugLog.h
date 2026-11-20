/*
 * Boiler Assistant - USB Serial debug messages
 * Off by default to save flash. Set BA_SERIAL_DEBUG to 1 to turn the
 * Wi-Fi, web, MQTT, OTA, push, fan, LCD, keypad, I2C and BME280 messages
 * back on. The boot line, the "Probes:" scan lines and the Wi-Fi/web
 * drop-out messages (including the 30 s "NET:" line) always print.
 */
#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>

#ifndef BA_SERIAL_DEBUG
#define BA_SERIAL_DEBUG 0
#endif

#if BA_SERIAL_DEBUG
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DBG_PRINT(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#endif

#endif
