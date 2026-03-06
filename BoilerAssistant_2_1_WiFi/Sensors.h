/*
 * ============================================================
 *  Boiler Assistant – Sensor Interface Module (v2.1 – Ember Guardian)
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
 *      Notes:
 *        - All heavy lifting is implemented in Sensors.cpp
 *        - DS18B20 reads are non‑blocking (93 ms conversion)
 *        - Probe roles are persisted via EEPROMStorage
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
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

