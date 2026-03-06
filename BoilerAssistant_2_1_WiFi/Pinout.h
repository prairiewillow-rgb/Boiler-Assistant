/*
 * ============================================================
 *  Boiler Assistant – Hardware Pinout (UNO R4 WiFi)
 *  ------------------------------------------------------------
 *  File: Pinout.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized hardware pin definitions for the UNO R4 WiFi
 *      platform used by Boiler Assistant v2.1. Ensures:
 *        • Single source of truth for all hardware mappings
 *        • Clean separation between logic and hardware layout
 *        • Safe, explicit pin usage for I2C, SPI, PWM, relays,
 *          and DS18B20 OneWire sensors.
 *
 *      Notes:
 *        - Primary I2C bus (A4/A5) drives LCD, BME280, keypad.
 *        - DS18B20 sensors share a single OneWire bus on D8.
 *        - MAX31855 thermocouples use hardware SPI (D12/D13).
 *        - Fan output uses a PWM‑capable pin on UNO R4.
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
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

// Fan PWM output (UNO R4 PWM-capable)
#define PIN_FAN_PWM        5

// Damper relay (active LOW)
#define PIN_DAMPER         6

/* ============================================================
 *  DS18B20 WATER TEMPERATURE SENSORS (OneWire bus)
 * ============================================================ */

#define PIN_DS18B20_DATA   8

/* ============================================================
 *  SPI – MAX31855 Thermocouples
 *  UNO R4 SPI pins:
 *      SCK  = D13
 *      MISO = D12
 *      MOSI = D11 (unused by MAX31855)
 * ============================================================ */

#define PIN_MAX31855_MISO  12
#define PIN_MAX31855_SCK   13

// Chip selects
#define PIN_TC1_CS         7   // Exhaust
#define PIN_TC2_CS         3
#define PIN_TC3_CS         4
#define PIN_TC4_CS         5

#endif
