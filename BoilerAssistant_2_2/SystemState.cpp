/*
 * ============================================================
 *  Boiler Assistant – System State Module (v2.2)
 *  ------------------------------------------------------------
 *  File: SystemState.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized system‑level state management.
 *      Responsibilities:
 *        • Own the global burnState
 *        • Provide deterministic startup behavior
 *        • Provide a safe reset entry point for UI + BurnEngine
 *
 *      Notes:
 *        - Startup and reset both initialize to BURN_RAMP.
 *        - BurnEngine owns all transitions after initialization.
 *        - This module ensures consistent state after UI‑triggered resets.
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include "SystemState.h"

/* ============================================================
 *  EXTERNAL GLOBALS (defined elsewhere)
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
    burnState = BURN_RAMP;
}

/* ============================================================
 *  RESET (used by BurnEngine or UI)
 * ============================================================ */

void systemstate_reset() {
    burnState = BURN_RAMP;
}
