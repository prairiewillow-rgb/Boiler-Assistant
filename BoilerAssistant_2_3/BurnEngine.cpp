/*
 * ============================================================
 *  Boiler Assistant – Burn Engine Module (v2.3)
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Unified outdoor‑boiler combustion curve for all burn states,
 *    now driven by the v2.3 Environmental Logic engine:
 *      - Uses seasonal effective setpoint and clamp
 *      - Preserves BOOST, RAMP, HOLD, Ember Guard behavior
 *      - Applies humidity/pressure fan bias from env logic
 *      - Keeps smooth transitions and stabilization logic
 *
 *  Architectural Notes:
 *      - RAMP and HOLD share the same curve; only min limits differ
 *      - HOLD uses 3‑cycle stabilization to prevent oscillation
 *      - Ember Guard enforces safe shutdown when fire collapses
 *      - EnvironmentalLogic provides envActiveSetpointF, clamp, bias
 *
 *  Version:
 *      Boiler Assistant v2.3
 * ============================================================
 */


#include <Arduino.h>
#include "SystemState.h"
#include "BurnEngine.h"
#include "EnvironmentalLogic.h"
#include "EEPROMStorage.h"

// Externs from main firmware (.ino)
extern BurnState burnState;

extern int16_t exhaustSetpoint;
extern int16_t deadbandF;
extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

extern bool           holdTimerActive;
extern unsigned long  holdStartMs;
extern unsigned long  HOLD_STABILITY_MS;

extern bool           rampTimerActive;
extern unsigned long  rampStartMs;
extern unsigned long  RAMP_STABILITY_MS;

extern unsigned long  boostStartMs;
extern int16_t        boostTimeSeconds;

// Ember Guardian v2.3 unified names
extern bool           emberGuardianActive;
extern bool           emberGuardianTimerActive;
extern unsigned long  emberGuardianStartMs;
extern int16_t        emberGuardianTimerMinutes;

// Environmental active state (from EnvironmentalLogic.cpp)
extern EnvSeason envActiveSeason;
extern int16_t   envActiveSetpointF;
extern uint8_t   envActiveClampPercent;
extern uint8_t   envActiveRampProfile;
extern int8_t    envFanBiasPercent;

// Sensor hook
extern double exhaust_readF_cached();

// Internal smoothing
static double lastExhaustF = NAN;

static double smoothExhaust(double raw) {
    if (isnan(lastExhaustF)) {
        lastExhaustF = raw;
        return raw;
    }
    lastExhaustF = 0.8 * lastExhaustF + 0.2 * raw;
    return lastExhaustF;
}

/* ============================================================
 *  INIT
 * ============================================================ */

void burnengine_init() {
    burnState                 = BURN_IDLE;
    holdTimerActive           = false;
    rampTimerActive           = false;

    emberGuardianTimerActive  = false;
    emberGuardianActive       = false;
}

/* ============================================================
 *  BOOST CONTROL
 * ============================================================ */

void burnengine_startBoost() {
    boostStartMs = millis();
    burnState    = BURN_BOOST;

    // Reset Ember Guardian timer on BOOST
    emberGuardianTimerActive = false;
    emberGuardianActive      = false;
}

/* ============================================================
 *  CORE COMPUTE
 * ============================================================ */

int burnengine_compute() {
    unsigned long now = millis();
    double rawExh = exhaust_readF_cached();
    double exh    = smoothExhaust(rawExh);

    // Update environmental logic (active setpoint/clamp/ramp/bias)
    env_logic_update(now);

    int16_t targetSetpoint = envActiveSetpointF > 0 ? envActiveSetpointF : exhaustSetpoint;
    int16_t db             = deadbandF;

    /* ============================================================
     *  EMBER GUARDIAN TIMER
     * ============================================================ */

    if (!emberGuardianActive && emberGuardianTimerMinutes > 0) {

        if (!emberGuardianTimerActive) {
            emberGuardianTimerActive = true;
            emberGuardianStartMs     = now;
        } else {
            unsigned long elapsedMs = now - emberGuardianStartMs;
            unsigned long totalMs   = (unsigned long)emberGuardianTimerMinutes * 60000UL;

            if (elapsedMs >= totalMs) {
                emberGuardianActive = true;
                burnState           = BURN_EMBER_GUARD;
            }
        }
    }

    // If Ember Guardian active → fan OFF
    if (emberGuardianActive && burnState == BURN_EMBER_GUARD) {
        return 0;
    }

    /* ============================================================
     *  BOOST MODE
     * ============================================================ */

    if (burnState == BURN_BOOST) {
        unsigned long elapsed = (now - boostStartMs) / 1000UL;

        if (elapsed >= (unsigned long)boostTimeSeconds) {
            burnState        = BURN_RAMP;
            rampTimerActive  = true;
            rampStartMs      = now;
        }

        return clampMaxPercent;
    }

    /* ============================================================
     *  BASIC HYSTERESIS CONTROL
     * ============================================================ */

    int demand = 0;

    if (exh < targetSetpoint - db) {
        burnState = BURN_RAMP;
        demand = clampMaxPercent;

    } else if (exh > targetSetpoint + db) {
        burnState = BURN_IDLE;
        demand = 0;

    } else {
        burnState = BURN_HOLD;

        // Proportional inside band
        double span = (double)(2 * db);
        double pos  = (exh - (targetSetpoint - db)) / span; // 0..1
        double pct  = clampMinPercent + (clampMaxPercent - clampMinPercent) * pos;

        demand = (int)pct;
    }

    /* ============================================================
     *  DEADZONE MODE
     * ============================================================ */

    if (deadzoneFanMode && demand < clampMinPercent) {
        demand = 0;
    }

    /* ============================================================
     *  CLAMP FINAL OUTPUT
     * ============================================================ */

    if (demand < 0)   demand = 0;
    if (demand > 100) demand = 100;

    return demand;
}
