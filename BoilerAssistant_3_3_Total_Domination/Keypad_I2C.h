/*
 * Boiler Assistant - Keypad_I2C
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef KEYPAD_I2C_H
#define KEYPAD_I2C_H

#include <Wire.h>

// Initialize keypad driver with I2C bus reference
void keypad_init(TwoWire &bus);

// Read a single debounced key event (returns 0 if none)
char keypad_read();

#endif
