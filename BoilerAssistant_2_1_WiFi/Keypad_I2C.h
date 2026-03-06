/*
 * ============================================================
 *  Boiler Assistant – Keypad I²C API (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: Keypad_I2C.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the 4×4 matrix keypad driver using
 *      an I²C expander (PCF8574‑style). Provides:
 *
 *        • keypad_init() — attach TwoWire bus
 *        • keypad_read() — debounced, stable key events
 *
 *      Notes:
 *        - No blocking delays (only µs settling in .cpp)
 *        - Returns a single keypress event per press
 *        - Zero dynamic allocation, zero Strings
 *        - Fully real‑time safe for the main loop
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef KEYPAD_I2C_H
#define KEYPAD_I2C_H

#include <Wire.h>

// Initialize keypad driver with I²C bus reference
void keypad_init(TwoWire &bus);

// Read a single debounced key event (returns 0 if none)
char keypad_read();

#endif
