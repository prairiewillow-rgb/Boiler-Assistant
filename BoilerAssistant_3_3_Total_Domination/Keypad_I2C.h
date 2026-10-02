/*
 * ============================================================
 *  Boiler Assistant â€“ Keypad IÂ²C API (v3.3.6 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Keypad_I2C.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the 4Ã—4 matrix keypad driver using
 *      an IÂ²C expander (PCF8574â€‘style). Provides:
 *
 *          â€¢ keypad_init() â€” attach TwoWire bus
 *          â€¢ keypad_read() â€” debounced, stable key events
 *
 *      Notes:
 *          - No blocking delays (only Âµsâ€‘level settling in .cpp)
 *          - Returns a single keypress event per press
 *          - Zero dynamic allocation, zero Strings
 *          - Fully realâ€‘time safe for the main loop
 *
 *  v2.3â€‘Environmental Notes:
 *      - Updated header to match v2.3â€‘Environmental identity
 *      - API unchanged and fully backward compatible
 *
 *  Version:
 *      Boiler Assistant v3.3.6 "Total Domination"
 * ============================================================
 */

#ifndef KEYPAD_I2C_H
#define KEYPAD_I2C_H

#include <Wire.h>

// Initialize keypad driver with IÂ²C bus reference
void keypad_init(TwoWire &bus);

// Read a single debounced key event (returns 0 if none)
char keypad_read();

#endif
