/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v1.3.1)
 *  ------------------------------------------------------------
 *  File: BurnLogic.h
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Features:
 *    • BOOST override (100% fan)
 *    • Deterministic fan OFF gate (no chatter)
 *    • Hysteresis ON/OFF thresholds
 *    • Clamp min/max
 *    • Deadzone smoothing
 *    • Damper ALWAYS OPEN (LOW)
 *
 * ============================================================
 */

#include "FanControl.h"
#include "Pinout.h"
#include "SystemState.h"
#include <Arduino.h>

// ------------------------------------------------------------
// Hysteresis thresholds
// ------------------------------------------------------------
static const int FAN_ON_HYST  = 3;   // turn ON above clampMin + 3%
static const int FAN_OFF_HYST = 1;   // turn OFF below clampMin - 1%

// Deadzone smoothing
static const int FAN_DEADZONE_DELTA = 3;

static int lastAppliedFanPercent = 0;


// ------------------------------------------------------------
// Helper: percent → PWM
// ------------------------------------------------------------
static int percentToPwm(int percent) {
    if (percent <= 0)  return 0;
    if (percent >= 100) return 255;
    return map(percent, 0, 100, 0, 255);
}


// ------------------------------------------------------------
// Helper: Apply PWM + damper (always open)
// ------------------------------------------------------------
static void applyHardwareOutputs(int fanPercent) {
    analogWrite(PIN_FAN_PWM, percentToPwm(fanPercent));

    // DAMPER ALWAYS OPEN (LOW = OPEN)
    digitalWrite(PIN_DAMPER_RELAY, LOW);
}


// ------------------------------------------------------------
// fancontrol_init()
// ------------------------------------------------------------
void fancontrol_init() {
    pinMode(PIN_FAN_PWM, OUTPUT);
    pinMode(PIN_DAMPER_RELAY, OUTPUT);

    fanIsOff = true;
    lastAppliedFanPercent = 0;

    // Start OFF, damper OPEN
    analogWrite(PIN_FAN_PWM, 0);
    digitalWrite(PIN_DAMPER_RELAY, LOW);
}


// ------------------------------------------------------------
// BOOST timeout handler
// ------------------------------------------------------------
void fancontrol_updateBoost() {
    if (burnState != BURN_BOOST) return;

    unsigned long now = millis();
    unsigned long elapsed = now - burnBoostStart;
    unsigned long boostMs = (unsigned long)boostTimeSeconds * 1000UL;

    if (elapsed >= boostMs) {
        burnState = (burnLogicMode == 0) ? BURN_ADAPTIVE : BURN_PID;
    }
}


// ------------------------------------------------------------
// fancontrol_apply()
// ------------------------------------------------------------
int fancontrol_apply(int rawFanPercent) {

    // --------------------------------------------------------
    // BOOST MODE — ALWAYS WINS
    // --------------------------------------------------------
    if (burnState == BURN_BOOST) {
        fanIsOff = false;
        lastAppliedFanPercent = 100;
        applyHardwareOutputs(100);
        return 100;
    }

    // --------------------------------------------------------
    // v1.3.1 FAN OFF GATE (dominates everything)
    // --------------------------------------------------------
    if (!fanIsOff) {
        // Fan currently ON → check if we should turn OFF
        if (rawFanPercent < (clampMinPercent - FAN_OFF_HYST)) {
            fanIsOff = true;
            lastAppliedFanPercent = 0;
            applyHardwareOutputs(0);
            return 0;
        }
    } else {
        // Fan currently OFF → check if we should turn ON
        if (rawFanPercent > (clampMinPercent + FAN_ON_HYST)) {
            fanIsOff = false;
            // continue to shaping logic
        } else {
            // Stay OFF
            lastAppliedFanPercent = 0;
            applyHardwareOutputs(0);
            return 0;
        }
    }

    // --------------------------------------------------------
    // Fan is ON — apply shaping
    // --------------------------------------------------------
    int shaped = rawFanPercent;

    // Clamp
    if (shaped < clampMinPercent) shaped = clampMinPercent;
    if (shaped > clampMaxPercent) shaped = clampMaxPercent;

    // Deadzone smoothing
    if (deadzoneFanMode == 0) {
        int delta = shaped - lastAppliedFanPercent;
        if (delta > -FAN_DEADZONE_DELTA && delta < FAN_DEADZONE_DELTA) {
            shaped = lastAppliedFanPercent;
        }
    }

    // Apply
    lastAppliedFanPercent = shaped;
    applyHardwareOutputs(shaped);

    return shaped;
}
