#include "Sensors.h"
#include "Pinout.h"
#include "SystemState.h"
#include <Arduino.h>
#include <SPI.h>

// ============================================================
// INTERNAL STATE
// ============================================================

static unsigned long lastTCRead = 0;
static double lastTCValueF = 0;

// smoothing state
static double smoothLast = 0;


// ============================================================
// sensors_init()
// ============================================================
void sensors_init() {

    // UNO R4 uses fixed SPI pins — no custom pin assignment
    SPI.begin();

    pinMode(PIN_TC1_CS, OUTPUT);
    digitalWrite(PIN_TC1_CS, HIGH);   // deselect MAX31855
}


// ============================================================
// readMAX31855_F()
// ============================================================
double readMAX31855_F() {

    uint8_t b0, b1, b2, b3;

    digitalWrite(PIN_TC1_CS, LOW);
    delayMicroseconds(1);

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

    return tempF;
}


// ============================================================
// exhaust_readF_cached()
// ============================================================
double exhaust_readF_cached() {

    unsigned long now = millis();

    if (now - lastTCRead >= 500) {   // .5 second tick
        lastTCRead = now;

        double t = readMAX31855_F();
        if (!isnan(t)) {
            lastTCValueF = t;
        }
    }

    return lastTCValueF;
}


// ============================================================
// smoothExhaustF()
// ============================================================
double smoothExhaustF(double rawF) {
    smoothLast = (smoothLast * 0.5) + (rawF * 0.5);
    return smoothLast;
}


// ============================================================
// Environmental Sensor (BME280) – Stub
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
