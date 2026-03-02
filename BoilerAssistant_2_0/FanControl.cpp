/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v2.0)
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Applies smoothing, clamping, hysteresis, and BOOST/SAFETY
 *    overrides to the raw fan demand produced by BurnEngine.
 *    Implements:
 *      - BOOST → 100% fan override
 *      - SAFETY → fan OFF override
 *      - Hysteresis mode (fan can turn fully OFF)
 *      - Clamp mode (fan never turns fully OFF)
 *      - Smoothing filter for stable fan transitions
 *      - State-change resets to avoid stale hysteresis
 *
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include <Arduino.h>

extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern BurnState burnState;

// ============================================================
// Internal memory
// ============================================================
static int       lastFan       = 0;
static bool      fanOn         = false;
static BurnState prevBurnState = BURN_IDLE;

// ============================================================
// Initialization
// ============================================================
void fancontrol_init() {
    lastFan       = 0;
    fanOn         = false;
    prevBurnState = burnState;
}

// ============================================================
// Handle burnState transitions
// ============================================================
static void fancontrol_handleStateChange() {
    if (burnState != prevBurnState) {

        // Reset hysteresis + smoothing when entering these states
        if (burnState == BURN_BOOST ||
            burnState == BURN_IDLE  ||
            burnState == BURN_SAFETY) {

            lastFan = 0;
            fanOn   = false;
        }

        prevBurnState = burnState;
    }
}

// ============================================================
// Main fan compute function
// ============================================================
int fan_compute(int demand) {

    // Detect state changes
    fancontrol_handleStateChange();

    // SAFETY OVERRIDE — fan OFF, no exceptions
    if (burnState == BURN_SAFETY) {
        lastFan = 0;
        fanOn   = false;
        return 0;
    }

    // BOOST OVERRIDE — fan 100%
    if (burnState == BURN_BOOST) {
        lastFan = 100;
        fanOn   = true;
        return 100;
    }

    // 1) Smooth raw demand FIRST
    int fan = (lastFan * 3 + demand) / 4;
    lastFan = fan;

    // Save smoothed value BEFORE any clamping
    int smoothed = fan;

    // 2) Clamp max
    if (fan > clampMaxPercent)
        fan = clampMaxPercent;

    // 3) Deadzone Fan: ON → Clamp Mode (fan never turns off)
    if (deadzoneFanMode == 1) {

        // In clamp mode, fan is always at or above clampMinPercent
        if (fan < clampMinPercent)
            fan = clampMinPercent;

        fanOn = true;
    }

    // 4) Deadzone Fan: OFF → Hysteresis Mode (fan can turn off)
    else {
        const int onThreshold  = clampMinPercent + 10;  // turn ON above clamp
        const int offThreshold = 5;                     // turn OFF below 5%

        // Hysteresis decisions use the smoothed value
        if (!fanOn && smoothed > onThreshold)
            fanOn = true;

        if (fanOn && smoothed < offThreshold)
            fanOn = false;
    }

    // 5) Output logic: apply hysteresis + enforce clamp only when ON
    if (!fanOn) {
        fan = 0;  // fully OFF
    } else {
        // Fan is ON → never allow it to run below clampMinPercent
        if (fan < clampMinPercent)
            fan = clampMinPercent;
    }

    return fan;
}

// ============================================================
// Compatibility wrapper
// ============================================================
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
