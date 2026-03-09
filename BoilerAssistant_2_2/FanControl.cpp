/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v2.2)
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Deterministic fan control logic for all burn states.
 *      Implements:
 *          • Clamp mode (fan always on within limits)
 *          • Fan‑off mode with hysteresis
 *          • BOOST and SAFETY overrides
 *          • State‑transition smoothing between RAMP/HOLD
 *
 *      This module ensures stable, predictable fan behavior
 *      regardless of burn state transitions or demand spikes.
 *
 *  v2.2 Additions:
 *      - Standardized v2.2 header format (no codename)
 *      - Clarified clamp vs fan‑off mode behavior
 *      - Improved documentation of state‑transition rules
 *
 *  Architectural Notes:
 *      - No blocking calls allowed
 *      - All logic is deterministic and real‑time safe
 *      - Matches procedural API defined in FanControl.h
 *
 *  Version:
 *      Boiler Assistant v2.2
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

        // Reset smoothing when leaving HOLD
        if (prevBurnState == BURN_HOLD && burnState == BURN_RAMP) {
            lastFan = clampMaxPercent;
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

// ============================================================
// Main fan compute function
// ============================================================
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
    if (deadzoneFanMode == 1) {
        fanOn = true;

        int fan = demand;
        if (fan < clampMinPercent) fan = clampMinPercent;
        if (fan > clampMaxPercent) fan = clampMaxPercent;

        return fan;
    }

    // ============================================================
    // MODE 0: Fan-Off Mode (your exact rule)
    // ============================================================

    // Fan OFF when demand < clampMinPercent
    if (demand < clampMinPercent) {
        fanOn = false;
        return 0;
    }

    // Fan ON when demand >= clampMinPercent + 10
    if (demand >= (clampMinPercent + 10)) {
        fanOn = true;
    }

    // Output
    if (!fanOn) {
        return 0;
    }

    int fan = demand;
    if (fan < clampMinPercent) fan = clampMinPercent;
    if (fan > clampMaxPercent) fan = clampMaxPercent;

    return fan;
}

// ============================================================
// Wrapper
// ============================================================
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
