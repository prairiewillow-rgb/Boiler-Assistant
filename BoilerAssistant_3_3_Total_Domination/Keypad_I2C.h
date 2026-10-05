/*
 * ============================================================
 *  Boiler Assistant - Keypad I2C API (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Keypad_I2C.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the 4x4 matrix keypad driver using
 *      an I2C expander (PCF8574-style). Provides:
 *
 *          - keypad_init() - attach TwoWire bus
 *          - keypad_read() - debounced, stable key events
 *
 *      Notes:
 *          - No blocking delays (only us-level settling in .cpp)
 *          - Returns a single keypress event per press
 *          - Zero dynamic allocation, zero Strings
 *          - Checked, timeout-bounded synchronous I2C transactions
 *          - Failed-device retry and stable-release recovery
 *
 *  Recovery Notes:
 *      - Shared 100 kHz bus is owned by I2CBus
 *      - Key events resume only after recovery and stable release
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef KEYPAD_I2C_H
#define KEYPAD_I2C_H

#include <Wire.h>

// Initialize keypad driver with I2C bus reference
void keypad_init(TwoWire &bus);

// Read a single debounced key event (returns 0 if none)
char keypad_read();

#endif
