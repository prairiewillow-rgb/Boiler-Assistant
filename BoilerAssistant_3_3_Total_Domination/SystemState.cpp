/*
 * Boiler Assistant - SystemState
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "SystemState.h"
#include "SystemData.h"
#include <Arduino.h>

/* ============================================================
 *  GLOBAL SYSTEM DATA INSTANCE
 * ============================================================ */
extern SystemData sys;

/* ============================================================
 *  INITIALIZATION (v3)
 * ============================================================ */

void systemstate_init() {

    // Deterministic startup state
    sys.burnState = BURN_RAMP;

    // v3: Safety always starts clear
    sys.safetyState = SAFETY_OK;

    // v3: Default tank setpoints (safe conservative values)
    sys.tankLowSetpointF  = 140;   // typical low threshold
    sys.tankHighSetpointF = 180;   // typical high threshold
}

/* ============================================================
 *  RESET (used by BurnEngine or UI)
 * ============================================================ */

void systemstate_reset() {

    // Reset to a known safe state
    sys.burnState = BURN_RAMP;

    // v3: Reset safety lockout
    sys.safetyState = SAFETY_OK;

    // v3: Do NOT reset runMode or tank setpoints
    //     (these are operator preferences stored in EEPROM)
}
