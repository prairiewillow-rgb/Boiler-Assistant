/*
 * ============================================================
 *  Boiler Assistant – System State Module (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: SystemState.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized system‑level state management.
 *
 *      Responsibilities:
 *        • Own the global burnState (shared across modules)
 *        • Provide deterministic startup behavior
 *        • Provide a safe reset entry point for UI + BurnEngine
 *
 *      Notes:
 *        - Startup and reset both initialize to BURN_RAMP.
 *        - BurnEngine owns all transitions after initialization.
 *        - This module ensures consistent state after UI‑triggered resets.
 *
 *  v2.3‑Environmental Notes:
 *        - No seasonal logic is handled here.
 *        - Behavior unchanged from v2.2, but documentation updated.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#include "SystemState.h"

/* ============================================================
 *  EXTERNAL GLOBALS (defined in .ino or other modules)
 * ============================================================ */

extern BurnState burnState;

extern int exhaustSetpoint;
extern int deadbandF;

extern int clampMinPercent;
extern int clampMaxPercent;

extern int boostTimeSeconds;

extern int coalBedTimerMinutes;
extern int flueLowThreshold;
extern int flueRecoveryThreshold;

extern int deadzoneFanMode;

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void systemstate_init() {
    // Deterministic startup state
    burnState = BURN_RAMP;
}

/* ============================================================
 *  RESET (used by BurnEngine or UI)
 * ============================================================ */

void systemstate_reset() {
    // Reset to a known safe state
    burnState = BURN_RAMP;
}
