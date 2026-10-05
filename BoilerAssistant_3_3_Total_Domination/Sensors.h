/*
 * ============================================================
 *  Boiler Assistant - Sensor API (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Sensors.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the unified sensor subsystem. Provides
 *    deterministic access to:
 *
 *      - MAX31855 exhaust thermocouple (cached reads)
 *      - DS18B20 water probes (scan + read)
 *      - BME280 outdoor environmental sensor
 *
 *    Architectural Notes:
 *      - All live values are written directly into SystemData (sys.*)
 *      - BME280 bus interface is allocated once; startup waits advance
 *        asynchronously with a 250ms calibration-busy deadline
 *      - BME identity/configuration/raw sample checks precede readiness;
 *        only a valid measurement marks it healthy, failures clear all values
 *      - Missing/invalid BME retries every 30s without resetting LCD/keypad;
 *        shared-bus held-line recovery remains owned by I2CBus
 *      - Runtime OneWire rescans are capped at 250ms between searches
 *        and twice MAX_WATER_PROBES addresses; retain old map on timeout
 *      - Probe roles resolved through sys.probeRoleMap
 *      - All implementation resides in Sensors.cpp
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Start BME280 initialization; initialize the water-probe driver.
bool sensors_init();
// Advance environmental initialization/retry after shared-bus recovery.
void sensors_loop();

// Read MAX31855 (cached)
double exhaust_readF_cached();

// Scan DS18B20 probes and populate sys.waterProbeCount
void scanWaterProbes();

// Re-scan the DS18B20 bus occasionally for probes that were reconnected.
void sensors_rescanWaterProbes();

// Read DS18B20 water probes into sys.waterTempF[]
void sensors_readWaterProbes();

// Read BME280 into sys.envTempF / sys.envHumidity / sys.envPressure
void sensors_readBME280();

#endif
