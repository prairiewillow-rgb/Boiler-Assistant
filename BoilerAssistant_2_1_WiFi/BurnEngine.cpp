/*
 * ============================================================
 *  Boiler Assistant – Burn Engine (v2.1 – Final Ember Guardian)
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core combustion state machine for Boiler Assistant v2.1.
 *    Implements:
 *      - BOOST ignition cycle
 *      - RAMP approach logic
 *      - HOLD stabilization logic
 *      - SAFETY hard-stop mode
 *      - EMBER GUARDIAN (v2.1 final logic)
 *      - Damper + fan coordination
 *      - Exhaust-based control with deadband + clamps
 *
 *  Ember Guardian (v2.1 Final):
 *      - Replaces legacy Coalbed logic entirely
 *      - Monitors flue temperature collapse during RAMP/HOLD
 *      - Starts a timed countdown when:
 *            • State = RAMP or HOLD
 *            • Fan is at clampMaxPercent
 *            • Exhaust < flueLowThreshold
 *      - Cancels countdown if fire recovers above flueRecoveryThreshold
 *      - Enters full EMBER_GUARD lockout when timer expires
 *      - Auto-exits lockout only when fire fully recovers
 *      - UI line 4 displays live countdown + lockout status
 *
 *  v2.1 Major Changes:
 *      - NEW: Ember Guardian
 *      - NEW: Full lockout state (BURN_EMBER_GUARD)
 *      - NEW: Recovery-based auto-exit from lockout
 *      - NEW: UI line 4 messaging for countdown + lockout
 *      - NEW: Cleaner BOOST → RAMP → HOLD transitions
 *      - NEW: Damper logic unified with fanPercent mapping
 *      - NEW: Stability and safety behavior exported to SystemData
 *
 *  Architectural Notes:
 *      - BurnEngine owns all combustion state transitions
 *      - No WiFi or UI logic is allowed inside this module
 *      - All timing is non-blocking (millis-based)
 *      - All globals remain for compatibility with UI + WiFiAPI
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#include "BurnEngine.h"
#include "SystemState.h"
#include "Pinout.h"
#include <Arduino.h>

/* ============================================================
 *  EXTERNAL GLOBALS (from main application)
 * ============================================================ */

extern int16_t exhaustSetpoint;
extern int16_t deadbandF;
extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;

extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

extern int16_t smoothExh;
extern int16_t lastFanPercent;
extern int16_t lastExhaustF;

extern int16_t boostTimeSeconds;
extern unsigned long boostStartMs;

extern int16_t emberGuardMinutes;
extern bool    emberGuardActive;
extern unsigned long emberGuardStartMs;

extern BurnState burnState;

// UI hook (line 4 text)
extern char uiLine4Buffer[32];

/* ============================================================
 *  HARDWARE HELPERS
 * ============================================================ */

static void damper_setClosed() {
    analogWrite(PIN_DAMPER, 0);
}

static void damper_setForFanPercent(int fanPercent) {
    int val = map(fanPercent, 0, 100, 0, 255);
    digitalWrite(PIN_DAMPER, fanPercent > 0 ? HIGH : LOW);
}

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void burnengine_init() {
    burnState         = BURN_IDLE;
    emberGuardActive  = false;
    emberGuardStartMs = 0;
    lastFanPercent    = 0;
    lastExhaustF      = smoothExh;
    damper_setClosed();
}

/* ============================================================
 *  BOOST START
 * ============================================================ */

void burnengine_startBoost() {
    emberGuardActive  = false;
    emberGuardStartMs = 0;

    burnState    = BURN_BOOST;
    boostStartMs = millis();
}

/* ============================================================
 *  MAIN COMPUTE LOOP
 * ============================================================ */

int burnengine_compute() {
    int fanPercent = 0;
    unsigned long now = millis();

    /* --------------------------------------------------------
     *  BOOST MODE
     * -------------------------------------------------------- */
    if (burnState == BURN_BOOST) {
        fanPercent = clampMaxPercent;

        unsigned long elapsed = (now - boostStartMs) / 1000UL;
        if (elapsed >= (unsigned long)boostTimeSeconds) {
            burnState = BURN_RAMP;
        }

        damper_setForFanPercent(fanPercent);
        lastFanPercent = fanPercent;
        lastExhaustF   = smoothExh;
        return fanPercent;
    }

    /* --------------------------------------------------------
     *  EMBER GUARDIAN COUNTDOWN (RAMP/HOLD ONLY)
     * -------------------------------------------------------- */
    if (!emberGuardActive) {

        bool validState = (burnState == BURN_RAMP || burnState == BURN_HOLD);
        bool fanAtMax   = (lastFanPercent >= clampMaxPercent);

        bool emberStart     = (smoothExh < flueLowThreshold);
        bool emberRecovered = (smoothExh >= flueRecoveryThreshold);

        bool conditions = validState && fanAtMax && emberStart;

        // Start countdown
        if (conditions && emberGuardStartMs == 0) {
            emberGuardStartMs = now;
        }

        // Cancel countdown if fire recovers
        if (emberRecovered && emberGuardStartMs > 0) {
            emberGuardStartMs = 0;
        }

        // Countdown running
        if (emberGuardStartMs > 0) {
            unsigned long elapsed = now - emberGuardStartMs;
            unsigned long maxMs   = (unsigned long)emberGuardMinutes * 60000UL;

            int remaining = emberGuardMinutes - (elapsed / 60000UL);
            if (remaining < 0) remaining = 0;

            snprintf(uiLine4Buffer, 32, "Ember Guard in %dm", remaining);

            // Timer expired
            if (elapsed >= maxMs) {

                // Only enter full guard if still dying
                if (smoothExh < flueLowThreshold) {
                    emberGuardActive = true;
                    burnState        = BURN_EMBER_GUARD;
                    fanPercent       = 0;
                    damper_setClosed();
                    snprintf(uiLine4Buffer, 32, "EMBER GUARD! *RESET");
                    lastFanPercent   = fanPercent;
                    lastExhaustF     = smoothExh;
                    return fanPercent;
                } else {
                    // Fire recovered during countdown
                    emberGuardStartMs = 0;
                }
            }
        }
    }

    /* --------------------------------------------------------
     *  EMBER GUARDIAN ACTIVE (FULL LOCKOUT)
     * -------------------------------------------------------- */
    if (emberGuardActive && burnState == BURN_EMBER_GUARD) {

        // Auto-exit when fire recovers
        if (smoothExh >= flueRecoveryThreshold) {
            emberGuardActive  = false;
            emberGuardStartMs = 0;
            burnState         = BURN_RAMP;
        } else {
            // Stay in full guard
            fanPercent = 0;
            damper_setClosed();
            snprintf(uiLine4Buffer, 32, "EMBER GUARD! *RESET");
            lastFanPercent = fanPercent;
            lastExhaustF   = smoothExh;
            return fanPercent;
        }
    }

    /* --------------------------------------------------------
     *  NORMAL BURN LOGIC
     * -------------------------------------------------------- */
    switch (burnState) {

        case BURN_IDLE:
            fanPercent = 0;
            damper_setClosed();
            break;

        case BURN_RAMP:
            if (smoothExh < exhaustSetpoint - deadbandF) {
                fanPercent = clampMaxPercent;
            } else {
                burnState  = BURN_HOLD;
                fanPercent = clampMinPercent;
            }
            break;

        case BURN_HOLD: {

            if (smoothExh < exhaustSetpoint - deadbandF) {
                burnState  = BURN_RAMP;
                fanPercent = clampMaxPercent;
                break;
            }

            int16_t error = exhaustSetpoint - smoothExh;

            if (error > deadbandF) {
                fanPercent = clampMaxPercent;
            } else if (error < -deadbandF) {
                fanPercent = clampMinPercent;
            } else {
                fanPercent = map(error,
                                 -deadbandF, deadbandF,
                                 clampMinPercent, clampMaxPercent);
            }
            break;
        }

        case BURN_SAFETY:
            fanPercent = 0;
            damper_setClosed();
            break;

        case BURN_EMBER_GUARD:
            fanPercent = 0;
            damper_setClosed();
            break;

        case BURN_BOOST:
            fanPercent = clampMaxPercent;
            break;
    }

    /* --------------------------------------------------------
     *  FINAL CLAMP + APPLY
     * -------------------------------------------------------- */
    if (fanPercent < 0)   fanPercent = 0;
    if (fanPercent > 100) fanPercent = 100;

    damper_setForFanPercent(fanPercent);

    lastFanPercent = fanPercent;
    lastExhaustF   = smoothExh;
    return fanPercent;
}
