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
 *  v2.0 Notes:
 *      - Integrated with BurnEngine v2.0 state machine
 *      - Hysteresis thresholds tuned for stability
 *      - BOOST and SAFETY overrides unified
 *      - Smoothing filter preserved from v1.3.1
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
static int lastFan = 0;
static bool fanOn = false;
static BurnState prevBurnState = BURN_IDLE;   // track state changes

// ============================================================
// Initialization
// ============================================================
void fancontrol_init() {
    lastFan = 0;
    fanOn = false;
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
            fanOn = false;
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

    // ============================================================
    // SAFETY OVERRIDE — fan OFF, no exceptions
    // ============================================================
    if (burnState == BURN_SAFETY) {
        lastFan = 0;
        fanOn = false;
        return 0;
    }

    // ============================================================
    // BOOST OVERRIDE — fan 100%
    // ============================================================
    if (burnState == BURN_BOOST) {
        lastFan = 100;
        fanOn = true;
        return 100;
    }

    // ============================================================
    // 1) Smooth raw demand FIRST
    // ============================================================
    int fan = (lastFan * 3 + demand) / 4;
    lastFan = fan;

    // ============================================================
    // 2) Clamp max
    // ============================================================
    if (fan > clampMaxPercent)
        fan = clampMaxPercent;

    // ============================================================
    // 3) Deadzone Fan: ON → Clamp Mode (fan never turns off)
    // ============================================================
    if (deadzoneFanMode == 1) {
        if (fan < clampMinPercent)
            fan = clampMinPercent;
    }

    // ============================================================
    // 4) Deadzone Fan: OFF → Hysteresis Mode (fan can turn off)
    // ============================================================
    else {
        const int onThreshold  = clampMinPercent + 10;  // fixed +10%
        const int offThreshold = clampMinPercent;

        // Turn ON only if rising above clampMin + 10%
        if (!fanOn && fan > onThreshold)
            fanOn = true;

        // Turn OFF only if falling below clampMin
        if (fanOn && fan < offThreshold)
            fanOn = false;

        // Apply hysteresis result
        fan = fanOn ? fan : 0;
    }

    return fan;
}

// ============================================================
// Compatibility wrapper
// ============================================================
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
