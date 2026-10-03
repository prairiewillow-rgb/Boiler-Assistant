/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v3.3.7 "Total Domination")
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
 *      â€¢ Clamp Mode (fan always on within min/max limits)
 *      â€¢ Fan-off Mode with hysteresis and re-enable thresholds
 *      â€¢ BOOST and SAFETY overrides
 *      â€¢ State-transition smoothing between RAMP/HOLD
 *      â€¢ Damper pre-fan delay (5 s)
 *      â€¢ Gradual ramping toward the requested fan speed (no startup
 *        kick: most boilers hold flue temp on natural draft through
 *        the deadband, so a full-power kick only causes overshoot
 *        and rapid on/off hunting)
 *      â€¢ Exhaust-probe fallback at max clamp fan output
 *      â€¢ Full SystemData migration (no legacy globals)
 *
 *  Architectural Notes:
 *      - FanControl owns all fan smoothing and hysteresis logic.
 *      - SystemData (sys.*) is the single source of truth.
 *      - This module never touches UI, EEPROM, or WiFi logic.
 *      - Output is always deterministic and operatorâ€‘visible.
 *
 *  Version:
 *      Boiler Assistant v3.3.7 "Total Domination"
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include "SystemData.h"
#include <Arduino.h>

/* ============================================================
 *  INTERNAL MEMORY
 * ============================================================ */
static int       lastFan       = 0;
static bool      fanOn         = false;
static BurnState prevBurnState = BURN_IDLE;

// Ramp limiter memory
static int lastOutput = 0;
static unsigned long lastRampMs = 0;
static unsigned long fanStartDelayUntil = 0;
static const unsigned long FAN_DAMPER_DELAY_MS = 5000UL;
static const unsigned long FAN_RAMP_INTERVAL_MS = 100UL;
static const int FAN_RAMP_STEP_PERCENT = 3;
static const unsigned long FAN_HOLD_RAMP_UP_INTERVAL_MS = 500UL;
static const int FAN_HOLD_RAMP_UP_STEP_PERCENT = 1;

// Fan-off mode anti-short-cycle timers
static unsigned long fanStateChangedMs = 0;
static const unsigned long FAN_MIN_ON_MS  = 30000UL;
static const unsigned long FAN_MIN_OFF_MS = 60000UL;

/* ============================================================
 *  INIT
 * ============================================================ */
void fancontrol_init() {
    lastFan       = 0;
    fanOn         = false;
    prevBurnState = sys.burnState;
    lastOutput    = 0;
    lastRampMs    = millis();
    fanStartDelayUntil = 0;
}

static int fancontrol_rampTo(int target) {
    unsigned long now = millis();

    bool slowUp = (sys.burnState == BURN_HOLD && target > lastOutput);
    unsigned long intervalMs = slowUp ? FAN_HOLD_RAMP_UP_INTERVAL_MS : FAN_RAMP_INTERVAL_MS;
    int stepPercent = slowUp ? FAN_HOLD_RAMP_UP_STEP_PERCENT : FAN_RAMP_STEP_PERCENT;

    unsigned long intervals = (now - lastRampMs) / intervalMs;
    if (intervals == 0) return lastOutput;

    int maxChange = (int)intervals * stepPercent;
    int delta = target - lastOutput;
    if (delta > maxChange) delta = maxChange;
    if (delta < -maxChange) delta = -maxChange;

    lastOutput += delta;
    lastRampMs += intervals * intervalMs;
    return lastOutput;
}

/* ============================================================
 *  HANDLE STATE TRANSITIONS
 * ============================================================ */
static void fancontrol_handleStateChange() {
    if (sys.burnState != prevBurnState) {

        bool enteringActiveState =
            sys.burnState == BURN_BOOST ||
            sys.burnState == BURN_RAMP ||
            sys.burnState == BURN_HOLD;
        bool wasActiveState =
            prevBurnState == BURN_BOOST ||
            prevBurnState == BURN_RAMP ||
            prevBurnState == BURN_HOLD;

        if (enteringActiveState && !wasActiveState) {
            fanStartDelayUntil = millis() + FAN_DAMPER_DELAY_MS;
            lastOutput = 0;
        }

        // Reset smoothing when leaving HOLD
        if (prevBurnState == BURN_HOLD && sys.burnState == BURN_RAMP) {
            lastFan = sys.clampMaxPercent;
            fanOn   = true;
        }

        // Reset on BOOST, IDLE, SAFETY
        if (sys.burnState == BURN_BOOST ||
            sys.burnState == BURN_IDLE) {

            lastFan = 0;
            fanOn   = false;
            if (sys.burnState == BURN_IDLE) fanStartDelayUntil = 0;
        }

        prevBurnState = sys.burnState;
    }
}

/* ============================================================
 *  MAIN FAN COMPUTE FUNCTION
 * ============================================================ */
int fan_compute(int demand) {

    fancontrol_handleStateChange();

    if (fanStartDelayUntil != 0) {
        if ((long)(millis() - fanStartDelayUntil) < 0) {
            lastOutput = 0;
            return 0;
        }
        fanStartDelayUntil = 0;
        lastRampMs = millis();
    }

    // Exhaust probe lost: run at max clamp (BOOST stays 100%) until tank logic idles the burn.
    if ((sys.exhaustFallbackActive || !sys.exhaustSensorOK) &&
        (sys.burnState == BURN_BOOST ||
         sys.burnState == BURN_RAMP ||
         sys.burnState == BURN_HOLD)) {
        int fan = (sys.burnState == BURN_BOOST) ? 100 : sys.clampMaxPercent;
        fanOn = true;
        lastOutput = fan;
        lastRampMs = millis();
        return fan;
    }

    if (sys.safetyState != SAFETY_OK) {
        fanOn = false;
        lastOutput = 0;
        lastRampMs = millis();
        return 0;
    }

    if (sys.emberGuardianLatched || sys.burnState == BURN_EMBER_GUARD) {
        fanOn = false;
        lastOutput = 0;
        lastRampMs = millis();
        return 0;
    }

    // BOOST override
    if (sys.burnState == BURN_BOOST) {
        fanOn = true;
        lastOutput = 100;
        lastRampMs = millis();
        return 100;
    }

    if (sys.burnState == BURN_IDLE) {
        fanOn = false;
        lastOutput = 0;
        lastRampMs = millis();
        return 0;
    }

    // ============================================================
    // MODE 1: Clamp Mode (fan always on)
    // Fan-off mode only applies in HOLD; RAMP always keeps the fan running.
    // ============================================================
    if (sys.deadzoneFanMode == 1 || sys.burnState == BURN_RAMP) {
        fanOn = true;

        int fan = demand;
        if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
        if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

        return fancontrol_rampTo(fan);
    }

    // ============================================================
    // MODE 0: Fan-Off Mode (your exact rule)
    // ============================================================

    // OFF when demand < clampMin, ON when demand >= clampMin + 10,
    // each change held for a minimum time to prevent short cycling.
    unsigned long now = millis();
    unsigned long heldMs = now - fanStateChangedMs;

    if (fanOn && demand < sys.clampMinPercent && heldMs >= FAN_MIN_ON_MS) {
        fanOn = false;
        fanStateChangedMs = now;
    } else if (!fanOn && demand >= (sys.clampMinPercent + 10) &&
               heldMs >= FAN_MIN_OFF_MS) {
        fanOn = true;
        fanStateChangedMs = now;
    }

    // Output
    if (!fanOn) {
        lastOutput = 0;
        lastRampMs = millis();
        return 0;
    }

    int fan = demand;
    if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
    if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

    return fancontrol_rampTo(fan);
}

/* ============================================================
 *  WRAPPER
 * ============================================================ */
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
