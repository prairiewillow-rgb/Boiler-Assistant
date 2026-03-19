/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic fan control logic for all burn states. This module
 *    implements the Total Domination Architecture (TDA) for fan output,
 *    ensuring stable, predictable behavior across BOOST, RAMP, HOLD,
 *    IDLE, and SAFETY transitions.
 *
 *    Responsibilities:
 *      • Clamp Mode (fan always on within min/max limits)
 *      • Fan‑off Mode with hysteresis and re‑enable thresholds
 *      • BOOST and SAFETY overrides
 *      • State‑transition smoothing between RAMP/HOLD
 *      • Full SystemData migration (no legacy globals)
 *
 *  Architectural Notes:
 *      - FanControl owns all fan smoothing and hysteresis logic.
 *      - SystemData (sys.*) is the single source of truth.
 *      - This module never touches UI, EEPROM, or WiFi logic.
 *      - Output is always deterministic and operator‑visible.
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include "SystemData.h"
#include <Arduino.h>

/* ============================================================
 *  COMPATIBILITY SHIM (v2.2 → v2.3)
 * ============================================================ */
#ifndef BURN_SAFETY
#define BURN_SAFETY ((BurnState)99)
#endif

extern BurnState burnState;

/* ============================================================
 *  INTERNAL MEMORY
 * ============================================================ */
static int       lastFan       = 0;
static bool      fanOn         = false;
static BurnState prevBurnState = BURN_IDLE;

// Ramp limiter memory
static int lastOutput = 0;

/* ============================================================
 *  INIT
 * ============================================================ */
void fancontrol_init() {
    lastFan       = 0;
    fanOn         = false;
    prevBurnState = burnState;
    lastOutput    = 0;
}

/* ============================================================
 *  HANDLE STATE TRANSITIONS
 * ============================================================ */
static void fancontrol_handleStateChange() {
    if (burnState != prevBurnState) {

        // Reset smoothing when leaving HOLD
        if (prevBurnState == BURN_HOLD && burnState == BURN_RAMP) {
            lastFan = sys.clampMaxPercent;
            fanOn   = true;
        }

        // Reset on BOOST, IDLE, SAFETY
        if (burnState == BURN_BOOST ||
            burnState == BURN_IDLE  ||
            burnState == BURN_SAFETY) {

            lastFan = 0;
            fanOn   = false;
        }

        prevBurnState = burnState;
    }
}

/* ============================================================
 *  MAIN FAN COMPUTE FUNCTION
 * ============================================================ */
int fan_compute(int demand) {

    fancontrol_handleStateChange();

    // SAFETY override
    if (burnState == BURN_SAFETY) {
        fanOn = false;
        return 0;
    }

    // BOOST override
    if (burnState == BURN_BOOST) {
        fanOn = true;
        return 100;
    }

    // ============================================================
    // MODE 1: Clamp Mode (fan always on)
    // ============================================================
    if (sys.deadzoneFanMode == 1) {
        fanOn = true;

        int fan = demand;
        if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
        if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

        // Ramp limiter
        int delta = fan - lastOutput;
        if (delta > 3)  delta = 3;
        if (delta < -3) delta = -3;

        fan = lastOutput + delta;
        lastOutput = fan;

        return fan;
    }

    // ============================================================
    // MODE 0: Fan-Off Mode (your exact rule)
    // ============================================================

    // Fan OFF when demand < clampMinPercent
    if (demand < sys.clampMinPercent) {
        fanOn = false;
        return 0;
    }

    // Fan ON when demand >= clampMinPercent + 10
    if (demand >= (sys.clampMinPercent + 10)) {
        fanOn = true;
    }

    // Output
    if (!fanOn) {
        return 0;
    }

    int fan = demand;
    if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
    if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

    // ============================================================
    // Output Ramp Limiter (smooth fan transitions)
    // ============================================================
    int delta = fan - lastOutput;
    if (delta > 3)  delta = 3;
    if (delta < -3) delta = -3;

    fan = lastOutput + delta;
    lastOutput = fan;

    return fan;
}

/* ============================================================
 *  WRAPPER
 * ============================================================ */
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
