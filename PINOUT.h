#ifndef PINOUT_H
#define PINOUT_H

// =========================================================
//  Boiler Assistant Uno Master Pinout
//  GeeekPi D-1229 Rev 1.2 Terminal Hat
// =========================================================

// -----------------------------
// I2C BUS (Internal A4/A5)
// -----------------------------
#define PIN_I2C_SDA        A4   // BLUE (header only, do not wire directly) wire to the SDA
#define PIN_I2C_SCL        A5   // YELLOW/BLACK (header only, do not wire directly) wire to the SCL

// -----------------------------
// POWER (Reference Only)
// -----------------------------
// 5V  -> LCD, BME280, MAX31855, Keypad  (RED)
// GND -> Common Ground                  (GREEN)

// -----------------------------
// DIGITAL OUTPUTS
// -----------------------------
#define PIN_DAMPER_RELAY   11   // YELLOW (LOW = ON)
#define PIN_FAN_PWM        10   // YELLOW/BLACK
#define PIN_FAN_RELAY      A1

// -----------------------------
// SPI (MAX31855)
// -----------------------------
#define PIN_MAX31855_MISO  12   // ORANGE/BLACK
#define PIN_MAX31855_SCK   13   // ORANGE
#define PIN_TC1_CS         A0   // GREY/BLACK 

// Future thermocouples (reserved)
#define PIN_TC2_CS         A2   // unused
#define PIN_TC3_CS         A3   // unused
#define PIN_TC4_CS         A4   // unused

// -----------------------------
// KEYPAD (4x4)
// -----------------------------
#define PIN_KEYPAD_ROW1     2   // BROWN
#define PIN_KEYPAD_ROW2     3   // BROWN/BLACK
#define PIN_KEYPAD_ROW3     4   // ORANGE
#define PIN_KEYPAD_ROW4     5   // ORANGE/BLACK

#define PIN_KEYPAD_COL1     6   // BLACK
#define PIN_KEYPAD_COL2     7   // WHITE
#define PIN_KEYPAD_COL3     8   // WHITE/BLACK
#define PIN_KEYPAD_COL4     9   // GREY

#endif
