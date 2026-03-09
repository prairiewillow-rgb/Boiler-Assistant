/*
 * ============================================================
 *  Boiler Assistant – Sensor Interface Module (v2.2)
 *  ------------------------------------------------------------
 *  File: Sensors.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Unified interface for all Boiler Assistant sensors:
 *        • MAX31855 exhaust thermocouple (500 ms cached)
 *        • NAN‑safe exhaust smoothing for UI + BurnEngine
 *        • BME280 environmental sensor (temperature, humidity, pressure)
 *        • DS18B20 water probes (up to 8, non‑blocking)
 *        • Persistent, user‑assignable probe roles
 *
 *      Features:
 *        - Non‑blocking DS18B20 reads (93 ms conversion)
 *        - Automatic probe scanning + role mapping
 *        - Exhaust smoothing with collapse‑reset protection
 *
 *      Power Notes:
 *        - MAX31855 breakout boards are 5V‑safe.
 *        - DS18B20 sensors are 5V‑safe on the shared OneWire bus.
 *
 *      ⚠ WARNING — BME280 SENSOR VOLTAGE:
 *        - The BME280 module is a **3.3V‑ONLY device**.
 *        - NEVER connect it to 5V power or 5V I2C lines unless the
 *          breakout includes a regulator + level shifting.
 *
 *      Notes:
 *        - All implementation lives in Sensors.cpp
 *        - Probe roles are persisted via EEPROMStorage
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "SystemState.h"   // Provides PROBE_ROLE_COUNT + MAX_WATER_PROBES

/* ============================================================
 *  MAX31855 – EXHAUST SENSOR
 * ============================================================ */

// Initialize all sensors (MAX31855 + BME280 + DS18B20)
bool sensors_init();

// Cached exhaust temperature (°F), updated every 500 ms
double exhaust_readF_cached();

// Smoothed exhaust temperature (°F) for UI + BurnEngine
double smoothExhaustF(double rawF);

/* ============================================================
 *  BME280 – ENVIRONMENT SENSOR
 * ============================================================ */

// Update BME280 values (called every loop)
void sensors_readBME280();

// Global environmental values (defined in Sensors.cpp)
extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;

/* ============================================================
 *  DS18B20 – WATER TEMPERATURE PROBES
 * ============================================================ */

// Up to 8 physical probes detected at boot
extern float   waterTempF[MAX_WATER_PROBES];
extern uint8_t probeCount;

// Role mapping (Tank, L1 S, L1 R, L2 S, L2 R, L3 S, L3 R, Spare)
extern uint8_t probeRoleMap[PROBE_ROLE_COUNT];

// Scan OneWire bus for DS18B20 probes
void scanWaterProbes();

// Read all detected water probes (non‑blocking)
void sensors_readWaterProbes();

/* ============================================================
 *  Combined Sensor Update
 * ============================================================ */

void sensors_readAll();

#endif
