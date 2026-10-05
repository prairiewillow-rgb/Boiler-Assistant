/*
 * ============================================================
 *  Boiler Assistant - Shared I2C Recovery (v3.3.9 "Total Domination")
 *  File: I2CBus.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Owns the 100 kHz LCD/keypad/BME bus and 25ms Wire timeout.
 *    Recovery closes Wire, emits up to nine open-drain clock
 *    pulses and a STOP, then reinitializes the peripheral.
 *    Held-line checks and retries use one-second intervals;
 *    a generation counter informs device drivers of recovery.
 *
 *  Version: Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#include "I2CBus.h"
#include "Pinout.h"
#include <Wire.h>

static const unsigned long RECOVERY_INTERVAL_MS = 1000UL;
static bool recoveryPending = false;
static bool busReady = false;
static unsigned long lastRecoveryMs = 0;
static unsigned long lastCheckMs = 0;
static uint32_t generation = 0;

static bool linesReleased() {
    return digitalRead(PIN_I2C1_SCL) == HIGH &&
           digitalRead(PIN_I2C1_SDA) == HIGH;
}

static void configureWire() {
    Wire.begin();
    // PCF8574 LCD/keypad expanders support standard mode, not 400 kHz.
    Wire.setClock(100000);
    Wire.setWireTimeout(25000, false);
}

static bool releaseClock() {
    pinMode(PIN_I2C1_SCL, INPUT_PULLUP);
    unsigned long start = micros();
    while (digitalRead(PIN_I2C1_SCL) == LOW) {
        if (micros() - start >= 200UL) return false;
    }
    delayMicroseconds(5);
    return true;
}

static void recoverBus() {
    // The Renesas Wire core ignores reset_on_timeout. Close it explicitly,
    // clock out a stranded slave byte, then generate STOP using open-drain GPIO.
    Wire.end();
    pinMode(PIN_I2C1_SDA, INPUT_PULLUP);
    bool clockReleased = releaseClock();
    for (uint8_t i = 0; clockReleased &&
         digitalRead(PIN_I2C1_SDA) == LOW && i < 9; ++i) {
        digitalWrite(PIN_I2C1_SCL, LOW);
        pinMode(PIN_I2C1_SCL, OUTPUT);
        delayMicroseconds(5);
        clockReleased = releaseClock();
    }

    if (clockReleased) {
        digitalWrite(PIN_I2C1_SCL, LOW);
        pinMode(PIN_I2C1_SCL, OUTPUT);
        digitalWrite(PIN_I2C1_SDA, LOW);
        pinMode(PIN_I2C1_SDA, OUTPUT);
        delayMicroseconds(5);
        clockReleased = releaseClock();
        pinMode(PIN_I2C1_SDA, INPUT_PULLUP);
        delayMicroseconds(5);
    }

    busReady = clockReleased && linesReleased();
    configureWire();
    ++generation;
    recoveryPending = !busReady;
    Serial.println(busReady ? "I2C: bus recovered"
                            : "I2C: bus still held LOW; retrying in 1s");
}

void i2cbus_init() {
    configureWire();
    busReady = linesReleased();
    recoveryPending = !busReady;
    lastRecoveryMs = millis() - RECOVERY_INTERVAL_MS;
    lastCheckMs = millis();
    if (!busReady) i2cbus_loop();
}

void i2cbus_requestRecovery() {
    recoveryPending = true;
    busReady = false;
}

bool i2cbus_ready() {
    return busReady;
}

uint32_t i2cbus_generation() {
    return generation;
}

void i2cbus_loop() {
    unsigned long now = millis();
    if (now - lastCheckMs >= RECOVERY_INTERVAL_MS) {
        lastCheckMs = now;
        if (!linesReleased()) i2cbus_requestRecovery();
    }
    if (!recoveryPending || now - lastRecoveryMs < RECOVERY_INTERVAL_MS) return;
    lastRecoveryMs = now;
    recoverBus();
}
