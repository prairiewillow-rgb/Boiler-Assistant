/*
 * ============================================================
 *  Boiler Assistant – Burn Engine Header (v2.0)
 *  ------------------------------------------------------------
 *  File: BurnEngine.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the BurnEngine module.
 *    Exposes:
 *      - BurnState enum (via SystemState.h)
 *      - Global burn state variable
 *      - BOOST, HOLD, RAMP, COALBED stability timers
 *      - BurnEngine initialization and compute functions
 *      - BOOST activation helper
 *
 *  v2.0 Notes:
 *      - Stability timers are now global for UI access
 *      - BOOST, RAMP, HOLD, COALBED logic unified in BurnEngine.cpp
 *      - Header simplified to avoid enum duplication
 *
 * ============================================================
 */

#ifndef BURN_ENGINE_H
#define BURN_ENGINE_H

#include <Arduino.h>
#include <stdint.h>
#include "SystemState.h"   // BurnState enum is defined here

// ============================================================
//  GLOBAL BURN STATE
// ============================================================
extern BurnState burnState;

// ============================================================
//  GLOBAL TIMERS (UI reads these)
// ============================================================

// BOOST
extern bool boostActive;
extern unsigned long boostStartMs;

// HOLD stability timer
extern bool holdTimerActive;
extern unsigned long holdStartMs;
extern unsigned long HOLD_STABILITY_MS;

// RAMP stability timer
extern bool rampTimerActive;
extern unsigned long rampStartMs;
extern unsigned long RAMP_STABILITY_MS;

// COALBED entry timer
extern bool coalbedTimerActive;
extern unsigned long coalbedStartMs;

// ============================================================
//  Engine API
// ============================================================

// Initialize burn engine (called on startup and manual reset)
void burnengine_init();

// Compute fan output based on exhaust temp
int burnengine_compute(double smoothExh);

// Start BOOST mode manually
void burnengine_startBoost();

// Query BOOST state
bool burnengine_isBoostActive();

#endif
