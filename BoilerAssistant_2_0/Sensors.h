/*
 * ============================================================
 *  Boiler Assistant – Sensor Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: Sensors.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for all sensor subsystems:
 *        • MAX31855 exhaust thermocouple (500ms cadence)
 *        • NAN‑safe smoothing for BurnEngine + UI
 *        • BME280 environmental sensor (temp/humidity/pressure)
 *        • Future DS18B20 water temperature probes
 *
 *  v2.0 Notes:
 *      - Updated to match UNO R4 timing requirements
 *      - Exhaust smoothing and spike filtering integrated
 *      - Environmental sensor access unified for UI + logic
 *
 * ============================================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// ============================================================
//  Exhaust Thermocouple (MAX31855)
// ============================================================

// Initialize all sensors (SPI + I2C)
void sensors_init();

// Cached exhaust temperature (°F), updated every 500ms
double exhaust_readF_cached();

// Smoothed exhaust temperature (°F) for UI + BurnEngine
double smoothExhaustF(double rawF);


// ============================================================
//  Environmental Sensor (BME280)
// ============================================================

// Update BME280 values (1 Hz)
void sensors_readBME280();

// Accessors for environmental data
float env_readTempF();
float env_readHumidity();
float env_readPressure();

// Global environmental values (defined in .ino)
extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;


// ============================================================
//  Water Temperature Probes (DS18B20 – Future)
// ============================================================

void sensors_readWaterTemps();

#endif

