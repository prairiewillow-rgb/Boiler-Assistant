/*
 * ============================================================
 *  Boiler Assistant – Sensor Module (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Sensors.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Unified sensor subsystem for the Boiler Assistant controller.
 *    Implements deterministic acquisition of:
 *
 *      • MAX31855 exhaust thermocouple
 *      • DS18B20 water probes (up to MAX_WATER_PROBES)
 *      • BME280 outdoor environmental sensor
 *
 *    All live values are written directly into SystemData (sys.*),
 *    following the Total Domination Architecture (TDA):
 *      - No dynamic allocation
 *      - No blocking delays beyond sensor‑required µs waits
 *      - Deterministic smoothing and caching for exhaust readings
 *      - Probe roles resolved through sys.probeRoleMap
 *
 *  Architectural Notes:
 *      - Exhaust readings use a 250 ms cache to avoid MAX31855 spam
 *      - Water probes use 20% smoothing for stable tank readings
 *      - BME280 values are read only when envSensorOK is true
 *      - This module contains no UI, MQTT, or EEPROM logic
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include "Sensors.h"
#include "SystemData.h"
#include "SystemState.h"
#include "EEPROMStorage.h"
#include "Pinout.h"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MAX31855.h>

/* ============================================================
 *  GLOBALS
 * ============================================================ */

extern SystemData sys;

// DS18B20 bus + driver
static OneWire oneWire(PIN_DS18B20_DATA);
static DallasTemperature waterSensors(&oneWire);
static DeviceAddress probeAddr[MAX_WATER_PROBES];

// BME280
static Adafruit_BME280 bme;

static Adafruit_MAX31855 max31855(
    PIN_MAX31855_SCK,
    PIN_TC1_CS,
    PIN_MAX31855_MISO
);

static unsigned long lastExhaustReadMs = 0;
static double lastExhaustF = NAN;

/* ============================================================
 *  EXHAUST SENSOR (MAX31855)
 * ============================================================ */

double exhaust_readF_cached() {
    unsigned long now = millis();
    if (now - lastExhaustReadMs < 250) {
        return lastExhaustF;
    }

    lastExhaustReadMs = now;

    double c = max31855.readCelsius();

    if (isnan(c)) {
        sys.exhaustSensorOK = false;
        return lastExhaustF;
    }

    sys.exhaustSensorOK = true;

    lastExhaustF = c * 9.0 / 5.0 + 32.0;
    return lastExhaustF;
}

/* ============================================================
 *  WATER PROBE SCAN
 * ============================================================ */

void scanWaterProbes() {
    sys.waterProbeCount = 0;
    oneWire.reset_search();

    DeviceAddress addr;

    while (oneWire.search(addr)) {
        if (sys.waterProbeCount < MAX_WATER_PROBES) {
            memcpy(probeAddr[sys.waterProbeCount], addr, 8);
            sys.waterProbeCount++;
        }
    }
}

/* ============================================================
 *  WATER PROBE READ
 * ============================================================ */

void sensors_readWaterProbes() {
    if (sys.waterProbeCount == 0) return;

    waterSensors.requestTemperatures();

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        float c = waterSensors.getTempC(probeAddr[i]);

        if (c > -55 && c < 125) {
            float newF = c * 9.0f / 5.0f + 32.0f;

            if (isnan(sys.waterTempF[i])) {
                sys.waterTempF[i] = newF;
            } else {
                sys.waterTempF[i] = sys.waterTempF[i] * 0.8f + newF * 0.2f;
            }
        }
    }
}

/* ============================================================
 *  BME280 READ
 * ============================================================ */

void sensors_readBME280() {
    if (!sys.envSensorOK) return;

    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();

    if (!isnan(t)) sys.envTempF    = t * 9.0f / 5.0f + 32.0f;
    if (!isnan(h)) sys.envHumidity = h;
    if (!isnan(p)) sys.envPressure = p / 100.0f;
}

/* ============================================================
 *  INIT
 * ============================================================ */

bool sensors_init() {
    // BME280
    bool ok = bme.begin(0x76);
    sys.envSensorOK = ok;

    // DS18B20
    waterSensors.begin();
    waterSensors.setWaitForConversion(false);

    scanWaterProbes();

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        waterSensors.setResolution(probeAddr[i], 9);
    }

    return ok;
}

/* ============================================================
 *  READ ALL
 * ============================================================ */

void sensors_readAll() {
    double rawF = exhaust_readF_cached();

    // v3.x exhaust smoothing
    sys.exhaustRawF = rawF;

    static double last = NAN;
    if (isnan(last)) last = rawF;
    last = (last * 0.90) + (rawF * 0.10);

    sys.exhaustSmoothF = last;

    sensors_readWaterProbes();
    sensors_readBME280();
}
