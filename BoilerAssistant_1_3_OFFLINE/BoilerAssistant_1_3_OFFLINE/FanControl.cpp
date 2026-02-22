/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v1.3)
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Applies all post‑processing to the raw fan percentage:
 *
 *          • BOOST override
 *          • Clamp min/max
 *          • Deadzone mode (true OFF capability)
 *          • Constant‑airflow mode (deadzone OFF)
 *          • PWM output
 *          • Damper relay control
 *
 *      Deadzone Fan Modes:
 *
 *          Deadzone Fan: ON
 *              → Constant airflow mode
 *              → Fan NEVER turns off
 *              → Always runs at clampMin or higher
 *
 *          Deadzone Fan: OFF
 *              → TRUE deadzone mode
 *              → Fan turns OFF immediately when fanPercent ≤ clampMin
 *              → Fan stays OFF for at least 20 seconds
 *              → Fan only turns ON after fanPercent ≥ clampMin + 10
 *                AND remains there for 10 seconds straight
 *
 *      This header is documentation-only and has ZERO effect on
 *      compiled code, logic, timing, or behavior.
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include "Pinout.h"
#include <Arduino.h>

// ============================================================
//  fancontrol_init()
// ============================================================
void fancontrol_init() {
    pinMode(PIN_FAN_PWM, OUTPUT);
    pinMode(PIN_DAMPER_RELAY, OUTPUT);

    digitalWrite(PIN_DAMPER_RELAY, LOW);  // damper OPEN at boot (active‑low)
}


// ============================================================
//  BOOST Mode Handler
// ============================================================
void fancontrol_updateBoost() {
    if (burnState == BURN_BOOST) {
        if (millis() - burnBoostStart >= (unsigned long)boostTimeSeconds * 1000UL) {
            burnState = (burnLogicMode == 0) ? BURN_ADAPTIVE : BURN_PID;
        }
    }
}


// ============================================================
//  fancontrol_apply() — BOOST ALWAYS WINS
// ============================================================
int fancontrol_apply(int rawFanPercent) {

    int fanPercent = rawFanPercent;

    // --------------------------------------------------------
    // BOOST MODE — OVERRIDES EVERYTHING
    // --------------------------------------------------------
    if (burnState == BURN_BOOST) {

        fanPercent = 100;          // Force 100%
        fanIsOff = false;          // Fan is ON
        int pwmValue = 255;        // Full PWM output
        analogWrite(PIN_FAN_PWM, pwmValue);

        // Damper forced OPEN (active‑low)
        digitalWrite(PIN_DAMPER_RELAY, LOW);

        return fanPercent;         // Skip all other logic
    }

    // ============================================================
    // CONSTANT AIRFLOW MODE (Deadzone Fan: ON)
    // ============================================================
    if (deadzoneFanMode == 1) {

        // Always run at clampMin or higher
        if (fanPercent < clampMinPercent)
            fanPercent = clampMinPercent;

        if (fanPercent > clampMaxPercent)
            fanPercent = clampMaxPercent;

        fanIsOff = false;
    }

    // ============================================================
    // TRUE DEADZONE MODE (Deadzone Fan: OFF)
    // ============================================================
    else {

        static bool fanWasOff = false;
        static unsigned long offSince = 0;
        static unsigned long onCandidateSince = 0;

        unsigned long now = millis();

        int onThreshold  = clampMinPercent + 10;  // turn ON at clampMin + 10%
        int offThreshold = clampMinPercent;       // turn OFF at clampMin

        // --------------------------------------------------------
        // FAN IS CURRENTLY OFF
        // --------------------------------------------------------
        if (fanWasOff) {

            // Stay OFF for at least 20 seconds
            if (now - offSince < 20000) {
                fanPercent = 0;
                fanIsOff = true;
                onCandidateSince = 0;
            }
            else {
                // Check if we have enough demand to turn ON
                if (fanPercent >= onThreshold) {

                    if (onCandidateSince == 0)
                        onCandidateSince = now;

                    // Must stay above threshold for 10 seconds
                    if (now - onCandidateSince >= 10000) {
                        fanPercent = clampMinPercent;
                        fanWasOff = false;
                        fanIsOff = false;
                        onCandidateSince = 0;
                    }
                }
                else {
                    // Reset ON timer if we dip below threshold
                    onCandidateSince = 0;
                    fanPercent = 0;
                    fanIsOff = true;
                }
            }
        }

        // --------------------------------------------------------
        // FAN IS CURRENTLY ON
        // --------------------------------------------------------
        else {

            // Turn OFF immediately if below clampMin
            if (fanPercent < offThreshold) {
                fanPercent = 0;
                fanWasOff = true;
                fanIsOff = true;
                offSince = now;
                onCandidateSince = 0;
            }
            else {
                // Stay ON — clamp AFTER deadzone logic
                if (fanPercent < clampMinPercent)
                    fanPercent = clampMinPercent;

                if (fanPercent > clampMaxPercent)
                    fanPercent = clampMaxPercent;

                fanIsOff = false;
            }
        }
    }

    // ============================================================
    // PWM Output (normal operation)
    // ============================================================
    int pwmValue = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwmValue);

    // ============================================================
    // DAMPER RELAY CONTROL (normal burn states)
    // ============================================================
    if (burnState == BURN_ADAPTIVE ||
        burnState == BURN_PID)
    {
        digitalWrite(PIN_DAMPER_RELAY, LOW);   // damper open
    }

    return fanPercent;
}


