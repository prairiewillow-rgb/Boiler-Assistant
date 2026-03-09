/*
 * ============================================================
 *  Boiler Assistant – Burn Engine Module (v2.2)
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Unified outdoor‑boiler combustion curve for all burn states.
 *    Implements a continuous adaptive algorithm used for:
 *      - RAMP and HOLD (same math, different labels)
 *      - BOOST mode with timed exit
 *      - Ember Guard detection and recovery
 *      - Smooth transitions and stabilization logic
 *      - Damper control tied to fan demand
 *
 *  v2.2 Additions:
 *      - Standardized v2.2 header format (no codename)
 *      - Clarified state transitions and curve behavior
 *      - Improved documentation for Ember Guard logic
 *      - Unified terminology across burn states
 *
 *  Architectural Notes:
 *      - RAMP and HOLD share the same curve; only min limits differ
 *      - HOLD uses 3‑cycle stabilization to prevent oscillation
 *      - Ember Guard enforces safe shutdown when fire collapses
 *      - All math is deterministic and real‑time safe
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include "BurnEngine.h"
#include "SystemState.h"
#include "Pinout.h"
#include <Arduino.h>

// ============================================================
// Extern globals from main
// ============================================================

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

// ============================================================
// HOLD stabilization counter (3-cycle smoothing)
// ============================================================

static uint8_t holdStabilizeCounter = 0;

// ============================================================
// Hardware helpers
// ============================================================

static void damper_setClosed() {
    analogWrite(PIN_DAMPER, 0);
}

static void damper_setForFanPercent(int fanPercent) {
    digitalWrite(PIN_DAMPER, fanPercent > 0 ? HIGH : LOW);
}

// ============================================================
// Init
// ============================================================

void burnengine_init() {
    burnState            = BURN_IDLE;
    emberGuardActive     = false;
    emberGuardStartMs    = 0;
    lastFanPercent       = 0;
    lastExhaustF         = smoothExh;
    holdStabilizeCounter = 0;
    damper_setClosed();
}

// ============================================================
// BOOST start
// ============================================================

void burnengine_startBoost() {
    emberGuardActive  = false;
    emberGuardStartMs = 0;

    burnState    = BURN_BOOST;
    boostStartMs = millis();
}

// ============================================================
// Main compute – returns RAW demand 0–100 %
// ============================================================

int burnengine_compute() {
    int fanPercent = 0;
    unsigned long now = millis();

    // --------------------------------------------------------
    // BOOST MODE
    // --------------------------------------------------------
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

    // --------------------------------------------------------
    // EMBER GUARD COUNTDOWN
    // --------------------------------------------------------
    if (!emberGuardActive) {

        bool validState = (burnState == BURN_RAMP || burnState == BURN_HOLD);
        bool fanAtMax   = (lastFanPercent >= clampMaxPercent);

        bool emberStart     = (smoothExh < flueLowThreshold);
        bool emberRecovered = (smoothExh >= flueRecoveryThreshold);

        bool conditions = validState && fanAtMax && emberStart;

        if (conditions && emberGuardStartMs == 0)
            emberGuardStartMs = now;

        if (emberRecovered && emberGuardStartMs > 0)
            emberGuardStartMs = 0;

        if (emberGuardStartMs > 0) {

            unsigned long elapsed = now - emberGuardStartMs;
            unsigned long maxMs   = (unsigned long)emberGuardMinutes * 60000UL;

            int remaining = emberGuardMinutes - (elapsed / 60000UL);
            if (remaining < 0) remaining = 0;

            snprintf(uiLine4Buffer, 32, "Ember Guard in %dm", remaining);

            if (elapsed >= maxMs) {

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
                    emberGuardStartMs = 0;
                }
            }
        }
    }

    // --------------------------------------------------------
    // EMBER GUARD ACTIVE
    // --------------------------------------------------------
    if (emberGuardActive && burnState == BURN_EMBER_GUARD) {

        if (smoothExh >= flueRecoveryThreshold) {
            emberGuardActive  = false;
            emberGuardStartMs = 0;
            burnState         = BURN_RAMP;
        } else {
            fanPercent = 0;
            damper_setClosed();
            snprintf(uiLine4Buffer, 32, "EMBER GUARD! *RESET");
            lastFanPercent = fanPercent;
            lastExhaustF   = smoothExh;
            return fanPercent;
        }
    }

    // --------------------------------------------------------
    // NORMAL BURN LOGIC (Unified Curve)
    // --------------------------------------------------------
    switch (burnState) {

        case BURN_IDLE:
            fanPercent = 0;
            damper_setClosed();
            break;

        // =====================================================
        // RAMP (same curve as HOLD)
        // =====================================================
        case BURN_RAMP: {

            // If close enough, switch to HOLD (label only)
            if (smoothExh >= exhaustSetpoint - deadbandF) {
                burnState            = BURN_HOLD;
                holdStabilizeCounter = 3;
                fanPercent           = lastFanPercent;
                break;
            }

            int16_t error = exhaustSetpoint - smoothExh;

            float x = constrain((float)error / 100.0f, 0.0f, 1.0f);
            float curve = pow(x, 1.8f);

            int targetPercent = (int)(curve * clampMaxPercent);

            // RAMP min = clampMinPercent
            if (targetPercent < clampMinPercent)
                targetPercent = clampMinPercent;
            if (targetPercent > clampMaxPercent)
                targetPercent = clampMaxPercent;

            fanPercent = targetPercent;
            break;
        }

        // =====================================================
        // HOLD (same curve, but min = 0%)
        // =====================================================
        case BURN_HOLD: {

            // 3-cycle smoothing
            if (holdStabilizeCounter > 0) {
                holdStabilizeCounter--;
                fanPercent = lastFanPercent;
                break;
            }

            // If we fall too low, go back to RAMP
            if (smoothExh < exhaustSetpoint - deadbandF) {
                burnState = BURN_RAMP;
                break;
            }

            // Fire dying → allow zero
            if (smoothExh < flueLowThreshold) {
                fanPercent = 0;
                break;
            }

            int16_t error = exhaustSetpoint - smoothExh;

            float x = constrain((float)error / 100.0f, 0.0f, 1.0f);
            float curve = pow(x, 1.8f);

            int targetPercent = (int)(curve * clampMaxPercent);

            // HOLD min = 0%
            if (targetPercent < 0)
                targetPercent = 0;
            if (targetPercent > clampMaxPercent)
                targetPercent = clampMaxPercent;

            // Limit change to ±3% per cycle
            if (targetPercent > lastFanPercent + 3)
                fanPercent = lastFanPercent + 3;
            else if (targetPercent < lastFanPercent - 3)
                fanPercent = lastFanPercent - 3;
            else
                fanPercent = targetPercent;

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

    // Clamp raw demand
    if (fanPercent < 0)   fanPercent = 0;
    if (fanPercent > 100) fanPercent = 100;

    damper_setForFanPercent(fanPercent);

    lastFanPercent = fanPercent;
    lastExhaustF   = smoothExh;
    return fanPercent;
}
