/*
 * Boiler Assistant - FanControl
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "FanControl.h"
#include "SystemState.h"
#include "SystemData.h"
#include "FanDimmer.h"
#include "Pinout.h"
#include <Arduino.h>

/* ============================================================
 *  INTERNAL MEMORY
 * ============================================================ */
static int       lastFan       = 0;
static bool      fanOn         = false;
static bool      exhaustHot    = false;
static BurnState prevBurnState = BURN_IDLE;

// Ramp limiter memory
static int lastOutput = 0;
static unsigned long lastRampMs = 0;
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
    exhaustHot    = false;
    prevBurnState = sys.burnState;
    lastOutput    = 0;
    lastRampMs    = millis();
    fanStateChangedMs = lastRampMs;
}

static int fancontrol_rampTo(int target) {
    unsigned long now = millis();

    bool slowUp = (sys.burnState == BURN_HOLD && target > lastOutput);
    unsigned long intervalMs = slowUp ? FAN_HOLD_RAMP_UP_INTERVAL_MS : FAN_RAMP_INTERVAL_MS;
    int stepPercent = slowUp ? FAN_HOLD_RAMP_UP_STEP_PERCENT : FAN_RAMP_STEP_PERCENT;

    unsigned long intervals = (now - lastRampMs) / intervalMs;
    if (intervals == 0) return lastOutput < sys.clampMinPercent ? 0 : lastOutput;

    int maxChange = (int)intervals * stepPercent;
    int delta = target - lastOutput;
    if (delta > maxChange) delta = maxChange;
    if (delta < -maxChange) delta = -maxChange;

    lastOutput += delta;
    lastRampMs += intervals * intervalMs;
    // Keep ramp progress internally without energizing below the running minimum.
    return lastOutput < sys.clampMinPercent ? 0 : lastOutput;
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
            lastOutput = 0;
            lastRampMs = millis();
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
        }

        prevBurnState = sys.burnState;
    }
}

/* ============================================================
 *  MAIN FAN COMPUTE FUNCTION
 * ============================================================ */
int fan_compute(int demand) {

    fancontrol_handleStateChange();

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

    unsigned long now = millis();
    if (sys.deadzoneFanMode != 0) {
        exhaustHot = false;
    } else if (sys.exhaustSensorOK && now - sys.exhaustLastGoodMs <= 1000UL &&
               isfinite(sys.exhaustRawF) && isfinite(sys.exhaustSmoothF)) {
        double halfBand = sys.deadbandF / 2.0;
        if (halfBand < 1.0) halfBand = 1.0;
        if (sys.exhaustRawF > sys.exhaustSetpoint + halfBand ||
            sys.exhaustSmoothF > sys.exhaustSetpoint + halfBand) {
            if (!exhaustHot) fanStateChangedMs = now;
            exhaustHot = true;
        } else if (sys.exhaustRawF < sys.exhaustSetpoint - halfBand &&
                   sys.exhaustSmoothF < sys.exhaustSetpoint - halfBand &&
                   now - fanStateChangedMs >= FAN_MIN_OFF_MS) {
            exhaustHot = false;
        }
    }
    // A qualified hot stop outranks BOOST, self-clean and sensor-fault fallback.
    // Invalid/stale readings cannot release it or restart an already-hot fire.
    if (exhaustHot) {
        fanOn = false;
        lastOutput = 0;
        lastRampMs = now;
        return 0;
    }

    // BOOST override
    if (sys.burnState == BURN_BOOST) {
        lastOutput = sys.damperDelayActive ? 0 : sys.activeBoostFanPercent;
        fanOn = lastOutput > 0;
        lastRampMs = millis();
        return lastOutput;
    }

    // Exhaust fallback keeps the chosen BOOST speed above; RAMP/HOLD use max.
    if ((sys.exhaustFallbackActive || !sys.exhaustSensorOK) &&
        (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD)) {
        lastOutput = sys.clampMaxPercent;
        fanOn = lastOutput > 0;
        lastRampMs = millis();
        return lastOutput;
    }

    if (sys.burnState == BURN_IDLE) {
        fanOn = false;
        lastOutput = 0;
        lastRampMs = millis();
        return 0;
    }

    // ============================================================
    // MODE 1: Clamp Mode (fan always on)
    // Outside the hot-stop override, fan-off cycling applies only in HOLD.
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

    // HOLD demand already has temperature hysteresis; allow its minimum-speed
    // call to restart after the off dwell, without a second conflicting band.
    unsigned long heldMs = now - fanStateChangedMs;

    if (fanOn && demand < sys.clampMinPercent && heldMs >= FAN_MIN_ON_MS) {
        fanOn = false;
        fanStateChangedMs = now;
    } else if (!fanOn && demand > 0 && demand >= sys.clampMinPercent &&
               heldMs >= FAN_MIN_OFF_MS) {
        fanOn = true;
        fanStateChangedMs = now;
    }

    // Output
    if (!fanOn) {
        return fancontrol_rampTo(0);
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
