/*
 * ============================================================
 *  Boiler Assistant - Hardware Pinout Definitions
 *  ------------------------------------------------------------
 *  File: Pinout.h
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Centralized pin mapping for the Boiler Assistant firmware.
 *    This file defines all GPIO assignments for the Uno-based
 *    controller using the GeeekPi D-1229 Rev 1.2 Terminal Hat.
 *
 *  Responsibilities:
 *    - Provide a single source of truth for all pin assignments
 *    - Document wiring colors and hardware roles
 *    - Ensure consistency across all modules
 *
 *  Notes:
 *    - No logic is implemented here; this file only declares
 *      hardware pin constants.
 *    - Changing pin assignments should be done carefully, as
 *      multiple modules depend on these definitions.
 *
 *  License Notes:
 *    This software is open-source for personal and private use.
 *    Commercial use, resale, or inclusion in commercial products
 *    is prohibited under the CC BY-NC-SA 4.0 license.
 *
 * ============================================================
 */

#ifndef PINOUT_H
#define PINOUT_H

// ============================================================
//  I2C BUS (Internal A4/A5)
//  GeeekPi D-1229 Rev 1.2 Terminal Hat
// ============================================================
#define PIN_I2C_SDA        A4   // Blue
#define PIN_I2C_SCL        A5   // Yellow/Black


// ============================================================
//  DIGITAL OUTPUTS
//  Fan, damper, and relay control pins.
// ============================================================
#define PIN_DAMPER_RELAY   11   // Yellow (LOW = ON)
#define PIN_FAN_PWM        10   // Yellow/Black PWM


// ============================================================
//  SPI (MAX31855 Thermocouples)
// ============================================================
#define PIN_MAX31855_MISO  12
#define PIN_MAX31855_SCK   13
#define PIN_TC1_CS         A0

#define PIN_TC2_CS         A2
#define PIN_TC3_CS         A3
#define PIN_TC4_CS         A4


// ============================================================
//  KEYPAD (4x4 Matrix)
// ============================================================
#define PIN_KEYPAD_ROW1     2   // Brown
#define PIN_KEYPAD_ROW2     3   // Brown/Black
#define PIN_KEYPAD_ROW3     4   // Orange
#define PIN_KEYPAD_ROW4     5   // Orange/Black

#define PIN_KEYPAD_COL1     6   // Black
#define PIN_KEYPAD_COL2     7   // White
#define PIN_KEYPAD_COL3     8   // White/Black
#define PIN_KEYPAD_COL4     9   // Grey

#endif
