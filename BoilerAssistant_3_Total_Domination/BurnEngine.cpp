/*
 * ============================================================
 *  Boiler Assistant – Burn Engine Module (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core combustion‑control logic for the Boiler Assistant controller.
 *    Implements the Total Domination Architecture (TDA) for all burn
 *    states, transitions, and safety pathways. This module owns:
 *
 *      - BOOST, RAMP, HOLD, IDLE, and EMBER GUARD state logic
 *      - Exhaust‑based demand computation (smooth + raw pipelines)
 *      - Deadband fan control (Mode 0 and Mode 1)
 *      - Guardian timer, latch, and recovery logic
 *      - Dampers (inverted polarity, Version B)
 *      - Legacy v2.2 → v3.x compatibility shims
 *
 *  v3.0 Additions:
 *      - Standardized state transitions under TDA
 *      - Unified exhaust smoothing + control pathways
 *      - Guardian latch behavior aligned with SystemData contract
 *      - Deterministic fan clamping and demand shaping
 *      - Expanded documentation for open‑source contributors
 *
 *  Architectural Notes:
 *      - This module never touches UI or WiFi logic
 *      - SystemData is the single source of truth for all parameters
 *      - Dampers and fan outputs are applied only through this module
 *      - All timing uses millis() and remains strictly non‑blocking
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"
#include "FanControl.h"
#include "Sensors.h"
#include "Pinout.h"

extern SystemData sys;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */
static int burnengine_computeAutoTank();
static int burnengine_computeContinuous();
static int burnengine_computeHoldDemand(double exhaustControlF,
                                        unsigned long now);

/* ============================================================
 *  HOLD STABILITY LOCK (v2.3-style)
 * ============================================================ */
static bool         holdLocked     = false;
static unsigned long holdLockUntil = 0;
static const unsigned long HOLD_LOCK_MS = 3000UL; // 3 seconds

/* ============================================================
 *  INIT
 * ============================================================ */
void burnengine_init() {
    sys.burnState = BURN_IDLE;

    sys.boostActive        = false;
    sys.holdTimerActive    = false;
    sys.rampTimerActive    = false;

    sys.emberGuardianActive      = false;
    sys.emberGuardianStartMs     = 0;
    sys.emberGuardianLatched     = false;
    sys.emberGuardianTimerActive = false;

    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, HIGH);   // BOOT = CLOSED
}

/* ============================================================
 *  BOOST START
 * ============================================================ */
void burnengine_startBoost() {
    sys.boostActive  = true;
    sys.boostStartMs = millis();

    sys.emberGuardianActive      = false;
    sys.emberGuardianStartMs     = 0;
    sys.emberGuardianLatched     = false;
    sys.emberGuardianTimerActive = false;

    sys.burnState = BURN_BOOST;
}

/* ============================================================
 *  DISPATCHER
 * ============================================================ */
int burnengine_compute() {
    if (sys.controlMode == RUNMODE_CONTINUOUS) {
        return burnengine_computeContinuous();
    } else {
        return burnengine_computeAutoTank();
    }
}

/* ============================================================
 *  HEAT-DEMAND HOLD DEMAND (v2.3-style)
 *  COLDER → MORE fan, HOTTER → LESS fan
 * ============================================================ */
static int burnengine_computeHoldDemand(double exhaustControlF,
                                        unsigned long now)
{
    if (isnan(exhaustControlF)) return 0;

    double bandHalf = sys.deadbandF / 2.0;
    if (bandHalf <= 0) bandHalf = 1.0;

    double low  = sys.exhaustSetpoint - bandHalf;
    double high = sys.exhaustSetpoint + bandHalf;

    /* ============================================================
     *  ⭐ NEW FIX: EXIT HOLD → RAMP WHEN EXHAUST DROPS BELOW BAND
     * ============================================================ */
    if (sys.burnState == BURN_HOLD &&
    exhaustControlF < low)
    {
        sys.burnState = BURN_RAMP;
        holdLocked = false;
        return sys.fanFinal;   // smooth transition at same fan %
    }


    // HOLD stability lock
    if (sys.burnState == BURN_HOLD && !holdLocked) {
        holdLocked     = true;
        holdLockUntil  = now + HOLD_LOCK_MS;
    }

    if (holdLocked && now >= holdLockUntil) {
        holdLocked = false;
    }

    /* ============================================================
     *  MODE 1: FAN ALWAYS ON (UI option 1)
     * ============================================================ */
    if (sys.deadzoneFanMode == 1) {
        if (exhaustControlF <= low) {
            return sys.clampMaxPercent;   // COLD → MORE FAN
        }
        if (exhaustControlF >= high) {
            return sys.clampMinPercent;   // HOT → LESS FAN
        }

        long d = map((long)exhaustControlF,
                     (long)low, (long)high,
                     (long)sys.clampMaxPercent,
                     (long)sys.clampMinPercent);
        return (int)d;
    }

    /* ============================================================
     *  MODE 0: FAN ALLOWED OFF (UI option 2)
     * ============================================================ */
    if (sys.deadzoneFanMode == 0) {

        // In band → OFF
        if (exhaustControlF >= low && exhaustControlF <= high) {
            return 0;
        }

        // Below band → ramp up toward 100%
        if (exhaustControlF < low) {
            double span = bandHalf;
            double e    = low - exhaustControlF;
            if (e >= span) return 100;
            double pct = (double)sys.clampMaxPercent * (e / span);
            if (pct < sys.clampMinPercent) pct = sys.clampMinPercent;
            return (int)pct;
        }

        // Above band → ramp down toward 0
        if (exhaustControlF > high) {
            double span = bandHalf;
            double e    = exhaustControlF - high;
            if (e >= span) return 0;
            double pct = (double)sys.clampMinPercent * (1.0 - (e / span));
            if (pct < 0) pct = 0;
            return (int)pct;
        }
    }

    return 0;
}

/* ============================================================
 *  SHARED GUARDIAN + DAMPER + FAN APPLY
 * ============================================================ */
static int burnengine_finalize(int demand,
                               double exhaustGuardF,
                               unsigned long now)
{
    /* EMBER GUARDIAN TIMER + LATCH */
    if (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD) {

        if (!sys.emberGuardianTimerActive &&
            !isnan(exhaustGuardF) &&
            exhaustGuardF < sys.flueLowThreshold)
        {
            sys.emberGuardianActive      = false;
            sys.emberGuardianStartMs     = now;
            sys.emberGuardianTimerActive = true;
        }

        if (sys.emberGuardianTimerActive) {

            unsigned long elapsed = now - sys.emberGuardianStartMs;
            unsigned long limitMs = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;

            bool timerExpired  = (elapsed >= limitMs);
            bool flueRecovered = (!isnan(exhaustGuardF) &&
                                  exhaustGuardF >= sys.flueRecoveryThreshold);

            if (flueRecovered) {
                sys.emberGuardianTimerActive = false;
                sys.emberGuardianActive      = false;
                sys.emberGuardianStartMs     = 0;
            }
            else if (timerExpired) {
                sys.burnState                = BURN_EMBER_GUARD;
                sys.boostActive              = false;
                sys.rampTimerActive          = false;
                sys.holdTimerActive          = false;

                sys.emberGuardianActive      = true;
                sys.emberGuardianLatched     = true;
                sys.emberGuardianTimerActive = false;

                demand = 0;
            }
        }
    }

    /* GUARDIAN RETURN PATH (LATCHED SHUTDOWN) */
    if (sys.emberGuardianLatched) {
        sys.burnState = BURN_EMBER_GUARD;
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.fanFinal = 0;
        return 0;
    }

    /* DAMPER LOGIC (Version B, INVERTED POLARITY) */
    if (sys.burnState == BURN_BOOST ||
        sys.burnState == BURN_RAMP  ||
        sys.burnState == BURN_HOLD)
    {
        digitalWrite(PIN_DAMPER, LOW);    // OPEN
    }
    else {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
    }

    /* Clamp only when fan is ON */
    if (demand > 0) {
        demand = constrain(demand, sys.clampMinPercent, sys.clampMaxPercent);
    } else {
        demand = 0;
    }

    /* APPLY FAN */
    sys.fanFinal = fancontrol_apply(demand);
    return sys.fanFinal;
}

/* ============================================================
 *  AUTO TANK ENGINE (v2.3-style behavior)
 * ============================================================ */
static int burnengine_computeAutoTank() {
    unsigned long now = millis();

    double exhaustControlF = sys.exhaustSmoothF;
    double exhaustGuardF   = sys.exhaustRawF;

    int tankIndex = (sys.probeRoleMap[PROBE_TANK] < sys.waterProbeCount)
                    ? sys.probeRoleMap[PROBE_TANK]
                    : 0;
    double tankF = sys.waterTempF[tankIndex];

    /* AUTO START */
    if (sys.burnState == BURN_IDLE) {
        if (!isnan(tankF) && tankF < sys.tankLowSetpointF) {
            burnengine_startBoost();
        }
    }

    /* AUTO STOP */
    if (sys.burnState == BURN_BOOST ||
        sys.burnState == BURN_RAMP  ||
        sys.burnState == BURN_HOLD)
    {
        if (!isnan(tankF) && tankF >= sys.tankHighSetpointF) {
            sys.burnState                = BURN_IDLE;
            sys.boostActive              = false;
            sys.rampTimerActive          = false;
            sys.holdTimerActive          = false;
            sys.emberGuardianActive      = false;
            sys.emberGuardianTimerActive = false;
            holdLocked                   = false;
        }
    }

    /* BOOST → RAMP */
    if (sys.burnState == BURN_BOOST) {
        unsigned long elapsed = now - sys.boostStartMs;
        if (!sys.boostActive ||
            elapsed >= (unsigned long)sys.boostTimeSeconds * 1000UL)
        {
            sys.boostActive = false;
            sys.burnState   = BURN_RAMP;
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }
    }

    /* RAMP → HOLD (early entry) */
    if (sys.burnState == BURN_RAMP) {
        if (!sys.rampTimerActive) {
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }

        if (!isnan(exhaustControlF) &&
            exhaustControlF >= (sys.exhaustSetpoint - 25))
        {
            sys.burnState       = BURN_HOLD;
            sys.holdTimerActive = true;
            sys.holdStartMs     = now;

            sys.emberGuardianActive      = false;
            sys.emberGuardianTimerActive = false;
        }
    }

    /* FAN DEMAND */
    int demand = 0;

    switch (sys.burnState) {
        case BURN_BOOST:
            demand = 100;
            break;

        case BURN_RAMP:
            if (isnan(exhaustControlF)) {
                demand = 0;
            } else {
                double low  = sys.exhaustSetpoint - 200.0;
                double high = sys.exhaustSetpoint;
                if (exhaustControlF <= low) {
                    demand = 100;
                } else if (exhaustControlF >= high) {
                    demand = sys.clampMinPercent;
                } else {
                    long d = map((long)exhaustControlF,
                                 (long)low, (long)high,
                                 100L,
                                 (long)sys.clampMinPercent);
                    demand = (int)d;
                }
            }
            break;

        case BURN_HOLD:
            demand = burnengine_computeHoldDemand(exhaustControlF, now);
            break;

        case BURN_IDLE:
        default:
            demand = 0;
            break;
    }

    return burnengine_finalize(demand, exhaustGuardF, now);
}

/* ============================================================
 *  CONTINUOUS ENGINE (v2.3-style behavior)
 * ============================================================ */
static int burnengine_computeContinuous() {
    unsigned long now = millis();

    double exhaustControlF = sys.exhaustSmoothF;
    double exhaustGuardF   = sys.exhaustRawF;

    /* BOOST → RAMP */
    if (sys.burnState == BURN_BOOST) {
        unsigned long elapsed = now - sys.boostStartMs;
        if (!sys.boostActive ||
            elapsed >= (unsigned long)sys.boostTimeSeconds * 1000UL)
        {
            sys.boostActive = false;
            sys.burnState   = BURN_RAMP;
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }
    }

    /* RAMP → HOLD (early entry) */
    if (sys.burnState == BURN_RAMP) {
        if (!sys.rampTimerActive) {
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }

        if (!isnan(exhaustControlF) &&
            exhaustControlF >= (sys.exhaustSetpoint - 25))
        {
            sys.burnState       = BURN_HOLD;
            sys.holdTimerActive = true;
            sys.holdStartMs     = now;

            sys.emberGuardianActive      = false;
            sys.emberGuardianTimerActive = false;
        }
    }

    /* FAN DEMAND */
    int demand = 0;

    switch (sys.burnState) {
        case BURN_BOOST:
            demand = 100;
            break;

        case BURN_RAMP:
            if (isnan(exhaustControlF)) {
                demand = 0;
            } else {
                double low  = sys.exhaustSetpoint - 200.0;
                double high = sys.exhaustSetpoint;
                if (exhaustControlF <= low) {
                    demand = 100;
                } else if (exhaustControlF >= high) {
                    demand = sys.clampMinPercent;
                } else {
                    long d = map((long)exhaustControlF,
                                 (long)low, (long)high,
                                 100L,
                                 (long)sys.clampMinPercent);
                    demand = (int)d;
                }
            }
            break;

        case BURN_HOLD:
            demand = burnengine_computeHoldDemand(exhaustControlF, now);
            break;

        case BURN_IDLE:
        default:
            demand = 0;
            break;
    }

    return burnengine_finalize(demand, exhaustGuardF, now);
}
