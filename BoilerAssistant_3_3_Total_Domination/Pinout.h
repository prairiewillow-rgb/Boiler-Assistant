/*
 * Boiler Assistant - Pinout
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef PINOUT_H
#define PINOUT_H

/* ============================================================
 *  PRIMARY I2C BUS (LCD + BME280 + Keypad)
 * ============================================================ */

#define PIN_I2C1_SDA   A4
#define PIN_I2C1_SCL   A5

/* ============================================================
 *  DIGITAL OUTPUTS
 * ============================================================ */

// Fan dimmer PSM output (Auto Tank only permits PWM fallback).
#define PIN_FAN_PWM        D5

// Dimmer zero-cross input; D0 supports external interrupts on UNO R4.
#define PIN_FAN_ZERO_CROSS D0

// Damper relay (active LOW)
#define PIN_DAMPER         D6

/* ============================================================
 *  DS18B20 WATER TEMPERATURE SENSORS (OneWire bus)
 * ============================================================ */

#define PIN_DS18B20_DATA   D8

/* ============================================================
 *  MAX31855 Exhaust Thermocouple (software SPI on the UNO R4 SPI header pins)
 *      SCK  = D13
 *      MISO = D12
 *      MOSI = D11 (unused by MAX31855)
 *  One K-type thermocouple (TC1, exhaust) is the only thermocouple.
 *  All water temperatures use the DS18B20 probes on D8.
 * ============================================================ */

#define PIN_MAX31855_MISO  D12  // DO  (Data Out -> MCU MISO)
#define PIN_MAX31855_SCK   D13  // CLK (Clock from MCU)
#define PIN_TC1_CS         D7   // CS (Chip Select) - Exhaust probe

/* ============================================================
 *  OPTIONAL MICRO-SD DATA LOGGER (software SPI, own pins)
 *  Generic SPI micro-SD module with on-board 3.3V regulator and
 *  level shifter: VCC -> 5V, GND -> GND. The card never shares
 *  the MAX31855 lines. Units without a card simply report
 *  "no card"; set SD_LOGGING_ENABLED to 0 to leave these pins
 *  untouched entirely.
 *  Card: FAT32-formatted 8-32 GB SDHC, "High Endurance" preferred.
 *  exFAT (most 64 GB+ cards) is not supported.
 * ============================================================ */

#define SD_LOGGING_ENABLED 1

#define PIN_SD_CS          D10
#define PIN_SD_SCK         D9
#define PIN_SD_MOSI        A0
#define PIN_SD_MISO        A1

#endif
