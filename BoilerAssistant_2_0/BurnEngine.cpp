/*
 * ============================================================
 *  Boiler Assistant – Burn Engine (v2.0)
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core burn‑phase state machine for the Boiler Assistant.
 *    Implements:
 *      - BOOST phase with countdown timer
 *      - RAMP phase with HOLD stability timer
 *      - HOLD phase with RAMP fallback stability timer
 *      - COALBED entry timer + exit logic
 *      - SAFETY override (damper closed, fan off)
 *      - Fan demand curve with soft caps
 *      - Globalized stability timers for UI access
 *
 *  v2.0 Major Changes:
 *      - Added BOOST countdown timer
 *      - Added HOLD and RAMP stability timers
 *      - Added COALBED entry timer
 *      - Cleaned state transitions (BOOST→RAMP→HOLD→COALBED)
 *      - Damper logic unified (active‑LOW relay)
 *      - Stability timers exposed globally for UI
 *      - HOLD fan curve softened with 80% cap
 *
 * ============================================================
 */

#include "BurnEngine.h"
#include "SystemState.h"
#include "Pinout.h"
#include <Arduino.h>
#include <stdint.h>

extern int16_t exhaustSetpoint;
extern int16_t deadbandF;
extern int16_t boostTimeSeconds;

extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;
extern int16_t coalBedTimerMinutes;

extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;

extern BurnState burnState;

// ============================================================
//  GLOBAL TIMERS (UI needs these)
// ============================================================

// BOOST
bool boostActive = false;
unsigned long boostStartMs = 0;

// COALBED
bool coalbedTimerActive = false;
unsigned long coalbedStartMs = 0;

// HOLD stability timer
bool holdTimerActive = false;
unsigned long holdStartMs = 0;
unsigned long HOLD_STABILITY_MS = 5000; // 5 seconds

// RAMP stability timer
bool rampTimerActive = false;
unsigned long rampStartMs = 0;
unsigned long RAMP_STABILITY_MS = 3000; // 3 seconds


// ============================================================
//  burnengine_init()
// ============================================================
void burnengine_init() {
    burnState    = BURN_BOOST;
    boostActive  = true;
    boostStartMs = millis();

    holdTimerActive = false;
    rampTimerActive = false;
    coalbedTimerActive = false;

    pinMode(PIN_DAMPER_RELAY, OUTPUT);
    digitalWrite(PIN_DAMPER_RELAY, HIGH); // CLOSED default
}

bool burnengine_isBoostActive() {
    return boostActive;
}


// ============================================================
//  burnengine_compute()
// ============================================================
int burnengine_compute(double smoothExh) {
    unsigned long now = millis();

    // ============================================================
    // SAFETY OVERRIDE
    // ============================================================
    if (burnState == BURN_SAFETY) {
        boostActive = false;
        coalbedTimerActive = false;
        holdTimerActive = false;
        rampTimerActive = false;

        digitalWrite(PIN_DAMPER_RELAY, HIGH);   // damper CLOSED
        return 0;                               // fan OFF
    }

    // ============================================================
    // BOOST MODE
    // ============================================================
    if (boostActive) {
        if (now - boostStartMs < (unsigned long)boostTimeSeconds * 1000UL) {

            burnState = BURN_BOOST;

            // BOOST forces damper OPEN
            digitalWrite(PIN_DAMPER_RELAY, LOW);

            return 100; // fan demand
        }

        // BOOST expired → go to RAMP
        boostActive = false;
        burnState   = BURN_RAMP;
    }

    // ============================================================
    // COALBED ENTRY LOGIC
    // ============================================================
    if (smoothExh < flueLowThreshold) {

        if (!coalbedTimerActive) {
            coalbedTimerActive = true;
            coalbedStartMs = now;
        }

        unsigned long requiredMs =
            (unsigned long)coalBedTimerMinutes * 60UL * 1000UL;

        if (coalbedTimerActive && (now - coalbedStartMs >= requiredMs)) {
            burnState = BURN_COALBED;
            coalbedTimerActive = false;
            holdTimerActive = false;
            rampTimerActive = false;
        }
    }
    else {
        coalbedTimerActive = false;
    }

    // ============================================================
    // NORMAL STATE MACHINE
    // ============================================================
    switch (burnState) {

        // --------------------------------------------------------
        // RAMP → HOLD (with stability timer)
        // --------------------------------------------------------
        case BURN_RAMP: {
            bool inHoldBand = (smoothExh >= exhaustSetpoint - deadbandF);

            if (inHoldBand) {
                if (!holdTimerActive) {
                    holdTimerActive = true;
                    holdStartMs = now;
                }

                if (now - holdStartMs >= HOLD_STABILITY_MS) {
                    burnState = BURN_HOLD;
                    holdTimerActive = false;
                }
            } else {
                holdTimerActive = false;
            }
            break;
        }

        // --------------------------------------------------------
        // HOLD → RAMP (with stability timer)
        // --------------------------------------------------------
        case BURN_HOLD: {

            bool belowRampBand = (smoothExh < exhaustSetpoint - (deadbandF * 2));

            if (belowRampBand) {
                if (!rampTimerActive) {
                    rampTimerActive = true;
                    rampStartMs = now;
                }

                if (now - rampStartMs >= RAMP_STABILITY_MS) {
                    burnState = BURN_RAMP;
                    rampTimerActive = false;
                    holdTimerActive = false;
                }
            } else {
                rampTimerActive = false;
            }

            break;
        }

        // --------------------------------------------------------
        // COALBED exit
        // --------------------------------------------------------
        case BURN_COALBED:
            if (smoothExh > exhaustSetpoint - deadbandF) {
                burnState = BURN_RAMP;
                holdTimerActive = false;
                rampTimerActive = false;
            }
            break;

        case BURN_IDLE:
        case BURN_BOOST:
        case BURN_SAFETY:
        default:
            break;
    }

    // ============================================================
    // DAMPER CONTROL (active LOW)
    // ============================================================
    if (burnState == BURN_IDLE) {
        digitalWrite(PIN_DAMPER_RELAY, HIGH);   // CLOSED
    } else {
        digitalWrite(PIN_DAMPER_RELAY, LOW);    // OPEN
    }

    // ============================================================
    // FAN DEMAND
    // ============================================================

    if (burnState == BURN_BOOST)
        return 100;

    if (burnState == BURN_RAMP)
        return 100;

    // ============================================================
    // HOLD FAN CURVE (with 80% soft cap)
    // ============================================================
    if (burnState == BURN_HOLD) {

        double error = exhaustSetpoint - smoothExh;

        // Zone 1: near setpoint
        if (error <= 5) {
            return clampMinPercent;
        }

        // Zone 2: moderate error
        if (error <= 25) {
            int fan = clampMinPercent + (error - 5) * 2;
            fan = min(fan, 80); // soft cap
            return constrain(fan, clampMinPercent, clampMaxPercent);
        }

        // Zone 3: large error
        int fan = 50 + (error - 25) * 2;

        if (error <= 40) {
            fan = min(fan, 80);
        }

        return constrain(fan, clampMinPercent, 100);
    }

    if (burnState == BURN_COALBED)
        return 0;

    if (burnState == BURN_IDLE)
        return 0;

    if (burnState == BURN_SAFETY)
        return 0;

    return 0;
}


// ============================================================
//  burnengine_startBoost()
// ============================================================
void burnengine_startBoost() {
    if (burnState == BURN_SAFETY)
        return;

    boostActive  = true;
    boostStartMs = millis();
    burnState    = BURN_BOOST;
}
