/*
 * Boiler Assistant - Sensors
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
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

// Advance DS18B20 conversion/reads into sys.waterTempF[]; call every loop pass.
void sensors_readWaterProbes();

// Make a detected probe the tank (control) probe and save it by serial number.
bool sensors_setTankProbe(uint8_t index);

// Rename a detected probe; the name is saved against its serial number.
bool sensors_setProbeName(uint8_t index, const char* name);

// Uppercase ROM ID, stable across discovery order changes; output needs 17 bytes.
bool sensors_waterProbeId(uint8_t index, char* output);

// Read BME280 into sys.envTempF / sys.envHumidity / sys.envPressure
// Name of the first probe silent for 60 s ("Tank probe" if the saved tank
// probe was not found at boot), or nullptr when every probe is reading.
const char* sensors_missingProbe();

void sensors_readBME280();

#endif
