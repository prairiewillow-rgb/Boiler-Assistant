/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Applies smoothing, clamping, hysteresis, and BOOST/SAFETY
 *      overrides to the raw fan demand produced by BurnEngine.
 *
 *      Implements:
 *        - BOOST override → 100% fan
 *        - SAFETY override → fan OFF
 *        - Clamp Mode (deadzoneFanMode = 1)
 *            • Fan never turns fully OFF
 *            • Enforces clampMinPercent minimum
 *        - Hysteresis Mode (deadzoneFanMode = 0)
 *            • Fan can turn fully OFF
 *            • ON/OFF thresholds prevent rapid toggling
 *        - Smoothing filter for stable transitions
 *        - State-change resets to avoid stale hysteresis
 *        - RAMP entry fix (jump to clampMaxPercent)
 *
 *  v2.1 Notes:
 *      - Updated documentation to match Ember Guardian architecture
 *      - Behavior unchanged from v2.0 (logic preserved exactly)
 *      - Fully compatible with new BurnEngine v2.1 state machine
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include <Arduino.h>

/* ============================================================
 *  EXTERNAL GLOBALS
 * ============================================================ */

extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern BurnState burnState;

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static int       lastFan       = 0;
static bool      fanOn         = false;
static BurnState prevBurnState = BURN_IDLE;

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void fancontrol_init() {
    lastFan       = 0;
    fanOn         = false;
    prevBurnState = burnState;
}

/* ============================================================
 *  STATE TRANSITION HANDLER
 * ============================================================ */

static void fancontrol_handleStateChange() {
    if (burnState != prevBurnState) {

        // Reset hysteresis + smoothing when entering these states
        if (burnState == BURN_BOOST ||
            burnState == BURN_IDLE  ||
            burnState == BURN_SAFETY) {

            lastFan = 0;
            fanOn   = false;
        }

        // OPTION C FIX:
        // When entering RAMP, start from clampMaxPercent instead of 0%
        if (burnState == BURN_RAMP && prevBurnState != BURN_RAMP) {
            lastFan = clampMaxPercent;
            fanOn   = true;
        }

        prevBurnState = burnState;
    }
}

/* ============================================================
 *  MAIN FAN COMPUTE FUNCTION
 * ============================================================ */

int fan_compute(int demand) {

    // Detect state changes
    fancontrol_handleStateChange();

    // SAFETY OVERRIDE — fan OFF
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

    // 3) Clamp Mode (deadzoneFanMode = 1)
    if (deadzoneFanMode == 1) {

        // Fan never turns off
        if (fan < clampMinPercent)
            fan = clampMinPercent;

        fanOn = true;
    }

    // 4) Hysteresis Mode (deadzoneFanMode = 0)
    else {
        const int onThreshold  = clampMinPercent + 10;  // turn ON above clamp
        const int offThreshold = 5;                     // turn OFF below 5%

        // Hysteresis decisions use the smoothed value
        if (!fanOn && smoothed > onThreshold)
            fanOn = true;

        if (fanOn && smoothed < offThreshold)
            fanOn = false;
    }

    // 5) Output logic
    if (!fanOn) {
        fan = 0;  // fully OFF
    } else {
        if (fan < clampMinPercent)
            fan = clampMinPercent;
    }

    return fan;
}

/* ============================================================
 *  COMPATIBILITY WRAPPER
 * ============================================================ */

int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
