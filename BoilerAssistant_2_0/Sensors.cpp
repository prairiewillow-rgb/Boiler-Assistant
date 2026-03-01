/*
 * ============================================================
 *  Boiler Assistant – Sensor Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: Sensors.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Unified sensor interface for the Boiler Assistant.
 *      Provides:
 *        • MAX31855 thermocouple reading with spike rejection
 *        • NAN‑safe smoothing for exhaust temperature
 *        • Deterministic 500ms exhaust sampling cadence
 *        • BME280 environmental sensor (temp/humidity/pressure)
 *        • UNO R4‑safe SPI and I2C timing adjustments
 *
 *  v2.0 Notes:
 *      - MAX31855 timing tuned for Renesas RA4M1 (UNO R4)
 *      - BME280 I2C clock reduced to 100kHz for stability
 *      - NAN‑safe smoothing and spike filtering improved
 *      - Exhaust cadence locked to 500ms for deterministic control
 *      - Environmental sensor retry logic added for noisy I2C
 *
 * ============================================================
 */

#include "Sensors.h"
#include "Pinout.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// ============================================================
// INTERNAL STATE
// ============================================================

// MAX31855
static unsigned long lastTCRead = 0;
static double lastTCValueF = NAN;

// smoothing state
static double smoothLast = NAN;

// BME280
static Adafruit_BME280 bme;
static bool bmeOK = false;
static unsigned long lastBMEread = 0;

// Extern globals (declared in .ino)
extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;


// ============================================================
// sensors_init()
// ============================================================
void sensors_init() {

    // --- MAX31855 SPI ---
    SPI.begin();

    // UNO R4: SPI clock must be slowed for MAX31855
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    pinMode(PIN_TC1_CS, OUTPUT);
    digitalWrite(PIN_TC1_CS, HIGH);

    // --- BME280 I2C ---
    Wire.begin();
    Wire.setClock(100000);   // UNO R4 default is 400kHz; BME280 is more stable at 100kHz

    bmeOK = bme.begin(0x76);

    if (!bmeOK) {
        envSensorOK = false;
    }
}


// ============================================================
// readMAX31855_F() – raw thermocouple read
// ============================================================
static double readMAX31855_F() {

    uint8_t b0, b1, b2, b3;

    digitalWrite(PIN_TC1_CS, LOW);
    delayMicroseconds(2);  // UNO R4 needs slightly longer CS settle

    b0 = SPI.transfer(0);
    b1 = SPI.transfer(0);
    b2 = SPI.transfer(0);
    b3 = SPI.transfer(0);

    digitalWrite(PIN_TC1_CS, HIGH);

    uint32_t raw =
        ((uint32_t)b0 << 24) |
        ((uint32_t)b1 << 16) |
        ((uint32_t)b2 << 8)  |
        (uint32_t)b3;

    // Fault bit?
    if (raw & 0x00010000) {
        return NAN;
    }

    // Extract 14-bit signed thermocouple temperature (bits 31..18)
    int16_t tcData = (raw >> 18) & 0x3FFF;

    // Sign extend
    if (tcData & 0x2000) {
        tcData |= 0xC000;
    }

    double tempC = tcData * 0.25;
    double tempF = (tempC * 9.0 / 5.0) + 32.0;

    // Reject impossible values
    if (tempF < -100 || tempF > 2000)
        return NAN;

    return tempF;
}


// ============================================================
// exhaust_readF_cached()
// ============================================================
double exhaust_readF_cached() {

    unsigned long now = millis();

    if (now - lastTCRead >= 500) {   // 0.5 second cadence
        lastTCRead = now;

        double t = readMAX31855_F();

        // Only update if valid
        if (!isnan(t)) {

            // Reject spikes > 150°F jump in 0.5s
            if (!isnan(lastTCValueF)) {
                if (fabs(t - lastTCValueF) > 150.0) {
                    return lastTCValueF;  // ignore spike
                }
            }

            lastTCValueF = t;
        }
    }

    return lastTCValueF;
}


// ============================================================
// smoothExhaustF()
// ============================================================
double smoothExhaustF(double rawF) {

    // If raw is invalid, keep last smooth value
    if (isnan(rawF)) {
        return smoothLast;
    }

    // First sample
    if (isnan(smoothLast)) {
        smoothLast = rawF;
        return smoothLast;
    }

    // UNO R4 has faster CPU → slightly stronger smoothing
    smoothLast = (smoothLast * 0.88) + (rawF * 0.12);
    return smoothLast;
}


// ============================================================
// BME280 Environmental Sensor
// ============================================================
void sensors_readBME280() {

    unsigned long now = millis();

    if (!bmeOK) {
        envSensorOK = false;
        return;
    }

    if (now - lastBMEread < 1000) {
        return; // 1 Hz update
    }
    lastBMEread = now;

    float tC = bme.readTemperature();
    float h  = bme.readHumidity();
    float p  = bme.readPressure() / 100.0F;

    // Retry once if NAN (UNO R4 I2C sometimes returns NAN on first read)
    if (isnan(tC) || isnan(h) || isnan(p)) {
        delay(5);
        tC = bme.readTemperature();
        h  = bme.readHumidity();
        p  = bme.readPressure() / 100.0F;
    }

    if (isnan(tC) || isnan(h) || isnan(p)) {
        envSensorOK = false;
        return;
    }

    envTempF    = (tC * 9.0 / 5.0) + 32.0;
    envHumidity = h;
    envPressure = p;
    envSensorOK = true;
}


// ============================================================
// Environmental Sensor Accessors
// ============================================================
float env_readTempF()     { return envTempF; }
float env_readHumidity()  { return envHumidity; }
float env_readPressure()  { return envPressure; }


// ============================================================
// Water Temperature Probes (DS18B20) – Stub
// ============================================================
void sensors_readWaterTemps() {
    // Placeholder for future DS18B20 implementation
}
