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
 *          • Deadzone mode
 *          • Constant‑airflow mode (deadzone OFF)
 *          • PWM output
 *          • Damper relay control
 *
 *      Behavior Notes:
 *          • When deadzoneFanMode == 1:
 *                Fan stays OFF until fanPercent ≥ clampMin + 5
 *
 *          • When deadzoneFanMode == 0:
 *                Fan NEVER turns off
 *                Fan ALWAYS runs at clampMinPercent or higher
 *                Even if burn logic outputs 0%
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

    // --------------------------------------------------------
    // Clamp min/max (normal operation only)
    // --------------------------------------------------------
    if (fanPercent < clampMinPercent) fanPercent = clampMinPercent;
    if (fanPercent > clampMaxPercent) fanPercent = clampMaxPercent;

    // --------------------------------------------------------
    // CONSTANT AIRFLOW MODE (Deadzone OFF)
    // --------------------------------------------------------
    if (!deadzoneFanMode) {
        if (fanPercent < clampMinPercent)
            fanPercent = clampMinPercent;

        fanIsOff = false;
    }

    // --------------------------------------------------------
    // Deadzone mode (Deadzone ON) with 10% hysteresis
    // --------------------------------------------------------
    else {
    static bool fanWasOff = false;

    int onThreshold  = clampMinPercent + 10;  // turn ON at clampMin + 10%
    int offThreshold = clampMinPercent;       // turn OFF below clampMin

    if (fanWasOff) {
        // Fan is currently OFF — only turn ON if we exceed ON threshold
        if (fanPercent >= onThreshold) {
            fanPercent = clampMinPercent;  // turn ON at clampMin
            fanWasOff = false;
            fanIsOff = false;
        } else {
            fanPercent = 0;                // stay OFF
            fanIsOff = true;
        }
    } else {
        // Fan is currently ON — only turn OFF if we drop below OFF threshold
        if (fanPercent < offThreshold) {
            fanPercent = 0;                // turn OFF
            fanWasOff = true;
            fanIsOff = true;
        } else {
            // Stay ON
            if (fanPercent < clampMinPercent)
                fanPercent = clampMinPercent;

            fanIsOff = false;
        }
    }
}


    // --------------------------------------------------------
    // PWM Output (normal operation)
    // --------------------------------------------------------
    int pwmValue = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwmValue);

    // --------------------------------------------------------
    // DAMPER RELAY CONTROL (normal burn states)
    // --------------------------------------------------------
    if (burnState == BURN_ADAPTIVE ||
        burnState == BURN_PID)
    {
        digitalWrite(PIN_DAMPER_RELAY, LOW);   // damper open
    }

    return fanPercent;
}


