/*
 * ============================================================
 *  Boiler Assistant – Sensor Module (v2.2)
 *  ------------------------------------------------------------
 *  File: Sensors.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Unified sensor subsystem for:
 *        • MAX31855 thermocouple (exhaust)
 *        • BME280 environmental sensor
 *        • DS18B20 water temperature probes (non‑blocking)
 *
 *      Features:
 *        - Exhaust caching (500 ms)
 *        - Fire‑collapse hard reset to avoid HOLD lock
 *        - 9‑bit DS18B20 resolution (93 ms conversion)
 *        - Non‑blocking OneWire reads
 *        - Automatic probe scanning + role mapping
 *
 *      Power Notes:
 *        - MAX31855 breakout boards are 5V‑safe (power + logic).
 *        - DS18B20 sensors are 5V‑safe and share a single OneWire bus.
 *        - ALL 5V devices must use the UNO R4’s regulated 5V + GND rails.
 *
 *      ⚠ WARNING — BME280 SENSOR VOLTAGE:
 *        - The BME280 module is a **3.3V‑ONLY device**.
 *        - NEVER connect it to 5V power or 5V I2C lines unless the
 *          breakout includes a regulator + level shifting.
 *        - Direct 5V wiring will permanently damage the sensor.
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include "Sensors.h"
#include "SystemState.h"
#include "EEPROMStorage.h"

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MAX31855.h>

/* ============================================================
 *  MAX31855 – EXHAUST THERMOCOUPLE
 * ============================================================ */

#define PIN_MAX_SCK   13
#define PIN_MAX_CS    7
#define PIN_MAX_DO    12

Adafruit_MAX31855 max31855(PIN_MAX_SCK, PIN_MAX_CS, PIN_MAX_DO);

static double lastExhaustF = 0;
static unsigned long lastExhaustReadMs = 0;

/* ============================================================
 *  BME280 – ENVIRONMENT SENSOR
 * ============================================================ */

Adafruit_BME280 bme;

float envTempF    = 0;
float envHumidity = 0;
float envPressure = 0;
bool  envSensorOK = false;

/* ============================================================
 *  DS18B20 – WATER TEMPERATURE PROBES (NON‑BLOCKING)
 * ============================================================ */

#define PIN_DS18B20_DATA 8

OneWire oneWire(PIN_DS18B20_DATA);
DallasTemperature waterSensors(&oneWire);

DeviceAddress probeAddr[MAX_WATER_PROBES];

float   waterTempF[MAX_WATER_PROBES] = {0};
uint8_t probeCount = 0;

// Default role map (0..7)
uint8_t probeRoleMap[PROBE_ROLE_COUNT] = { 0,1,2,3,4,5,6,7 };

/* ============================================================
 *  INIT
 * ============================================================ */

bool sensors_init() {

    // MAX31855
    max31855.begin();

    // BME280
    bool ok = bme.begin(0x76);
    envSensorOK = ok;

    // DS18B20
    waterSensors.begin();
    waterSensors.setWaitForConversion(false);   // NON-BLOCKING
    scanWaterProbes();

    // Set all probes to 9-bit resolution (93 ms conversion)
    for (int i = 0; i < probeCount; i++) {
        waterSensors.setResolution(probeAddr[i], 9);
    }

    return ok;
}

/* ============================================================
 *  EXHAUST READ (MAX31855)
 * ============================================================ */

double exhaust_readF_cached() {
    unsigned long now = millis();

    // 500 ms cache
    if (now - lastExhaustReadMs < 500) {
        return lastExhaustF;
    }

    lastExhaustReadMs = now;

    double c = max31855.readCelsius();
    if (isnan(c)) return lastExhaustF;

    lastExhaustF = (c * 9.0 / 5.0) + 32.0;
    return lastExhaustF;
}

double smoothExhaustF(double rawF) {
    static double smoothed = 0;

    if (rawF < -100 || rawF > 2000) return smoothed;

    // HARD RESET WHEN FIRE COLLAPSES
    // Prevents HOLD lock and 77% fan trap
    if (abs(smoothed - rawF) > 150) {
        smoothed = rawF;
        return smoothed;
    }

    // Normal smoothing
    smoothed = (smoothed * 0.30) + (rawF * 0.70);
    return smoothed;
}

/* ============================================================
 *  BME280 READ
 * ============================================================ */

void sensors_readBME280() {
    if (!envSensorOK) return;

    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();

    if (!isnan(t)) envTempF = t * 9.0 / 5.0 + 32.0;
    if (!isnan(h)) envHumidity = h;
    if (!isnan(p)) envPressure = p / 100.0F;
}

/* ============================================================
 *  DS18B20 SCAN
 * ============================================================ */

void scanWaterProbes() {
    probeCount = 0;
    oneWire.reset_search();

    DeviceAddress addr;

    while (oneWire.search(addr)) {
        if (probeCount < MAX_WATER_PROBES) {
            memcpy(probeAddr[probeCount], addr, 8);
            probeCount++;
        }
    }
}

/* ============================================================
 *  DS18B20 READ (NON‑BLOCKING)
 * ============================================================ */

void sensors_readWaterProbes() {
    if (probeCount == 0) return;

    waterSensors.requestTemperatures();  // non-blocking

    for (uint8_t i = 0; i < probeCount; i++) {
        float c = waterSensors.getTempC(probeAddr[i]);

        if (c > -55 && c < 125) {
            waterTempF[i] = c * 9.0 / 5.0 + 32.0;
        }
    }
}

/* ============================================================
 *  READ ALL SENSORS (BME every loop, DS18B20 timed externally)
 * ============================================================ */

void sensors_readAll() {
    sensors_readBME280();
}
