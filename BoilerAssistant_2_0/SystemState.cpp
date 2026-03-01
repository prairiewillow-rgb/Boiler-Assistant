/*
 * ============================================================
 *  Boiler Assistant – System State Module (v2.0)
 *  ------------------------------------------------------------
 *  File: SystemState.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized system‑level state management.
 *      Provides:
 *        • Global burnState ownership
 *        • Deterministic startup state
 *        • Safe reset entry point for UI and BurnEngine
 *
 *  v2.0 Notes:
 *      - Startup and reset both initialize to BURN_RAMP
 *      - Simplified to avoid duplication with BurnEngine logic
 *      - Ensures consistent state after UI‑triggered resets
 *
 * ============================================================
 */

#include "SystemState.h"

// ------------------------------------------------------------
//  Extern globals (defined in .ino)
// ------------------------------------------------------------
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

// ------------------------------------------------------------
//  Initialization
// ------------------------------------------------------------
void systemstate_init() {
    burnState = BURN_RAMP;
}

// ------------------------------------------------------------
//  Reset (used by BurnEngine or UI)
// ------------------------------------------------------------
void systemstate_reset() {
    burnState = BURN_RAMP;
}
