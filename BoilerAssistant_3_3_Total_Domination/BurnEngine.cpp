/*
 * ============================================================
 *  Boiler Assistant â€“ Burn Engine Module (v3.3.6 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BurnEngine.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core combustionâ€‘control logic for the Boiler Assistant controller.
 *    Implements the Total Domination Architecture (TDA) for all burn
 *    states, transitions, and safety pathways. This module owns:
 *
 *      - BOOST, RAMP, HOLD, IDLE, and EMBER GUARD state logic
 *      - Exhaustâ€‘based demand computation (smooth + raw pipelines)
 *      - Deadband fan control (Mode 0 and Mode 1)
 *      - Guardian timer, latch, and recovery logic
 *      - Dampers (inverted polarity, Version B)
 *      - Legacy v2.2 â†’ v3.x compatibility shims
 *
 *  v3.3 Additions:
 *      - Standardized state transitions under TDA
 *      - Unified exhaust smoothing + control pathways
 *      - Guardian latch behavior aligned with SystemData contract
 *      - Deterministic fan clamping and demand shaping
 *      - Expanded documentation for openâ€‘source contributors
 *
 *  v3.3.6 Additions:
 *      - Automatic self-cleaning burn: after a configurable number of
 *        completed burns, one full-output burn runs inside the
 *        configured overnight window (local time zone + DST), capped
 *        at one hour, AUTO TANK mode only; manual request supported
 *
 *  Architectural Notes:
 *      - This module never touches UI or WiFi logic
 *      - SystemData is the single source of truth for all parameters
 *      - Dampers are applied through this module; fan output is
 *        commanded through FanControl/FanDimmer
 *      - All timing uses millis() and remains strictly nonâ€‘blocking
 *
 *  Version:
 *      Boiler Assistant v3.3.6 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"
#include "FanControl.h"
#include "FanDimmer.h"
#include "Sensors.h"
#include "Pinout.h"
#include "EEPROMStorage.h"
#include <WiFiS3.h>
#include "OTAUpdater.h"

extern SystemData sys;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */
static int burnengine_computeAutoTank();
static int burnengine_computeContinuous();
static int burnengine_computeHoldDemand(double exhaustControlF,
                                        unsigned long now);

static double burnengine_holdEntryTemperature() {
    double halfBand = sys.deadbandF / 2.0;
    if (halfBand < 1.0) halfBand = 1.0;
    return sys.exhaustSetpoint - halfBand;
}

/* ============================================================
 *  HOLD STABILITY LOCK (v2.3-style)
 * ============================================================ */
static unsigned long holdLowSinceMs = 0;
static const unsigned long HOLD_LOW_CONFIRM_MS = 15000UL;
static const double HOLD_EXIT_HYSTERESIS_F = 10.0;
// Mode 0: fan latches on below the band and stays on until exhaust reaches setpoint.
static bool holdFanCalling = false;
// Degrees below the band over which HOLD fan demand scales from min to max clamp.
static const double HOLD_RECOVERY_SPAN_F = 50.0;

static float adaptiveSlope = 1.0f;
static double adaptiveLastTemperature = NAN;
static unsigned long adaptiveLastSampleMs = 0;
static unsigned long adaptiveLastSavedMs = 0;
static const unsigned long ADAPTIVE_SAVE_INTERVAL_MS = 3600000UL;
static const unsigned long SELF_CLEAN_MAX_MS = 3600000UL;
static unsigned long selfCleanStartMs = 0;

static bool selfCleanNightAllowed() {
    unsigned long utc = WiFi.getTime();
    if (utc == 0) return false;

    long localSeconds = (long)utc +
                        (long)sys.selfCleanUtcOffsetMinutes * 60L +
                        (sys.selfCleanDstEnabled ? 3600L : 0L);
    if (localSeconds < 0) localSeconds += 86400L;
    int hour = (int)((localSeconds / 3600L) % 24L);

    if (sys.selfCleanStartHour == sys.selfCleanEndHour) return true;
    if (sys.selfCleanStartHour < sys.selfCleanEndHour) {
        return hour >= sys.selfCleanStartHour && hour < sys.selfCleanEndHour;
    }
    return hour >= sys.selfCleanStartHour || hour < sys.selfCleanEndHour;
}

static bool selfCleanMayStart() {
    return sys.selfCleanManualRequested ||
           (sys.selfCleanDue && selfCleanNightAllowed());
}

float burnengine_getAdaptiveSlope() {
    return adaptiveSlope;
}

static void burnengine_updateAdaptiveSlope(double exhaustControlF,
                                           unsigned long now)
{
    if (isnan(exhaustControlF) ||
        adaptiveLastSampleMs != 0 && now - adaptiveLastSampleMs < 1000UL) {
        return;
    }

    double rate = 0.0;
    if (adaptiveLastSampleMs != 0 && !isnan(adaptiveLastTemperature)) {
        double elapsedSeconds = (now - adaptiveLastSampleMs) / 1000.0;
        if (elapsedSeconds > 0.0) {
            rate = (exhaustControlF - adaptiveLastTemperature) / elapsedSeconds;
        }
    }

    if (adaptiveLastSampleMs != 0 && sys.deadzoneFanMode == 0) {
        if (exhaustControlF < sys.exhaustSetpoint - 20 && rate < 0.05) {
            adaptiveSlope += 0.005f;
        }
        if (exhaustControlF > sys.exhaustSetpoint + 20 && rate > 0.05) {
            adaptiveSlope -= 0.005f;
        }
        adaptiveSlope = constrain(adaptiveSlope, 0.5f, 2.0f);

        if (now - adaptiveLastSavedMs >= ADAPTIVE_SAVE_INTERVAL_MS) {
            eeprom_saveAdaptiveSlope(adaptiveSlope);
            adaptiveLastSavedMs = now;
        }
    }

    adaptiveLastTemperature = exhaustControlF;
    adaptiveLastSampleMs = now;
}

// Verify this limit against the boiler manufacturer's approved high limit.
static const int TANK_HARD_LIMIT_F = 190;
static const unsigned long EXHAUST_FAULT_CONFIRM_MS = 60000UL;
static const unsigned long TANK_FAULT_CONFIRM_MS = 60000UL;
static unsigned long exhaustFaultSinceMs = 0;
static unsigned long tankFaultSinceMs = 0;
static BurnState historyLastState = BURN_IDLE;

static bool burnStateIsActive(BurnState state) {
    return state == BURN_BOOST || state == BURN_RAMP || state == BURN_HOLD;
}

static void updateBurnHistory(unsigned long now) {
    bool wasActive = burnStateIsActive(historyLastState);
    bool isActive = burnStateIsActive(sys.burnState);

    if (!wasActive && isActive) {
        sys.burnActiveStartMs = now;
    } else if (wasActive && !isActive && sys.burnActiveStartMs != 0) {
        uint32_t duration = (uint32_t)((now - sys.burnActiveStartMs) / 1000UL);
        uint32_t interval = sys.burnLastStartMs == 0
                                ? 0
                                : (uint32_t)((sys.burnActiveStartMs - sys.burnLastStartMs) / 1000UL);

        uint8_t limit = sys.burnHistoryCount < BURN_HISTORY_COUNT
                            ? sys.burnHistoryCount
                            : BURN_HISTORY_COUNT - 1;
        for (int i = limit; i > 0; i--) {
            sys.burnHistoryDurationSec[i] = sys.burnHistoryDurationSec[i - 1];
            sys.burnHistoryIntervalSec[i] = sys.burnHistoryIntervalSec[i - 1];
            sys.burnHistoryStartElapsedMin[i] = sys.burnHistoryStartElapsedMin[i - 1];
            sys.burnHistoryWaterTempF[i] = sys.burnHistoryWaterTempF[i - 1];
        }
        sys.burnHistoryDurationSec[0] = duration;
        sys.burnHistoryIntervalSec[0] = interval;
        sys.burnHistoryStartElapsedMin[0] = sys.burnActiveStartMs / 60000UL;
        sys.burnHistoryWaterTempF[0] = -1;
        if (sys.waterProbeCount > 0) {
            uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
            if (tankProbe < sys.waterProbeCount &&
                !isnan(sys.waterTempF[tankProbe])) {
                sys.burnHistoryWaterTempF[0] = (int16_t)(sys.waterTempF[tankProbe] + 0.5f);
            }
        }
        if (sys.burnHistoryCount < BURN_HISTORY_COUNT) sys.burnHistoryCount++;
        sys.burnLastStartMs = sys.burnActiveStartMs;
        sys.burnActiveStartMs = 0;

        if (sys.selfCleanActive) {
            sys.selfCleanActive = false;
            sys.selfCleanDue = false;
            sys.selfCleanManualRequested = false;
            sys.selfCleanBurnCount = 0;
            selfCleanStartMs = 0;
        } else if (sys.selfCleanEnabled &&
                   sys.selfCleanBurnCount < sys.selfCleanIntervalBurns) {
            sys.selfCleanBurnCount++;
            if (sys.selfCleanBurnCount >= sys.selfCleanIntervalBurns) {
                sys.selfCleanDue = true;
            }
        }
    }

    historyLastState = sys.burnState;
}

void burnengine_resetSensorFault() {
    exhaustFaultSinceMs = 0;
    tankFaultSinceMs = 0;
    sys.sensorFaultMask = 0;
}

void burnengine_resetAlarms() {
    burnengine_resetSensorFault();
    sys.exhaustFallbackActive = false;
    sys.safetyState = SAFETY_OK;
    sys.emberGuardianActive = false;
    sys.emberGuardianLatched = false;
    sys.emberGuardianTimerActive = false;
    sys.emberGuardianStartMs = 0;
    sys.boostActive = false;
    sys.rampTimerActive = false;
    sys.holdTimerActive = false;
    sys.burnState = BURN_IDLE;
}

static bool sensorFaultConfirmed(bool faultActive,
                                 unsigned long& faultSinceMs,
                                 unsigned long now,
                                 unsigned long confirmMs)
{
    if (!faultActive) {
        faultSinceMs = 0;
        return false;
    }

    if (faultSinceMs == 0) faultSinceMs = now;
    return now - faultSinceMs >= confirmMs;
}

/* ============================================================
 *  INIT
 * ============================================================ */
void burnengine_init() {
    sys.burnState = BURN_IDLE;
    historyLastState = BURN_IDLE;
    fan_dimmer_setPercent(0);
    adaptiveSlope = eeprom_loadAdaptiveSlope();
    adaptiveLastTemperature = NAN;
    adaptiveLastSampleMs = 0;
    adaptiveLastSavedMs = 0;
    exhaustFaultSinceMs = 0;
    tankFaultSinceMs = 0;

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
    unsigned long now = millis();

    updateBurnHistory(now);

    if (!sys.selfCleanEnabled) {
        sys.selfCleanDue = false;
        sys.selfCleanActive = false;
        sys.selfCleanManualRequested = false;
        sys.selfCleanBurnCount = 0;
        selfCleanStartMs = 0;
    }

    if (sys.selfCleanActive && selfCleanStartMs != 0 &&
        now - selfCleanStartMs >= SELF_CLEAN_MAX_MS) {
        sys.selfCleanActive = false;
        sys.selfCleanDue = false;
        sys.selfCleanManualRequested = false;
        sys.selfCleanBurnCount = 0;
        selfCleanStartMs = 0;
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        return 0;
    }

    if (ota_isActive()) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        return 0;
    }

    if (sys.safetyState == SAFETY_SENSOR_FAULT) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        return 0;
    }

    if (sys.safetyState == SAFETY_HIGHTEMP) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        return 0;
    }

    bool exhaustFault = !sys.exhaustSensorOK ||
                         now - sys.exhaustLastGoodMs > 1000UL;
    if (!exhaustFault) {
        sys.exhaustFallbackActive = false;
        exhaustFaultSinceMs = 0;
    }

    if (sensorFaultConfirmed(exhaustFault, exhaustFaultSinceMs, now,
                             EXHAUST_FAULT_CONFIRM_MS)) {
        sys.exhaustFallbackActive = true;
        sys.sensorFaultMask |= SENSOR_FAULT_EXHAUST;
    }

    bool tankFault = true;
    double tankF = NAN;

    // Auto Tank requires a valid tank probe; Continuous mode does not.
    if (sys.controlMode == RUNMODE_AUTO_TANK) {
        if (sys.waterProbeCount > 0) {
            uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
            if (tankProbe < sys.waterProbeCount) {
                tankF = sys.waterTempF[tankProbe];
                if (now - sys.waterTempLastGoodMs[tankProbe] > 3000UL) {
                    tankF = NAN;
                }
            }
            tankFault = isnan(tankF);
        }

        if (sensorFaultConfirmed(tankFault, tankFaultSinceMs, now,
                                 TANK_FAULT_CONFIRM_MS)) {
            sys.safetyState = SAFETY_SENSOR_FAULT;
            sys.sensorFaultMask = SENSOR_FAULT_TANK;
            digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
            sys.burnState = BURN_IDLE;
            sys.boostActive = false;
            sys.rampTimerActive = false;
            sys.holdTimerActive = false;
            return 0;
        }
    } else {
        tankFaultSinceMs = 0;
        sys.sensorFaultMask &= (uint8_t)~SENSOR_FAULT_TANK;
    }

    if (!tankFault && tankF >= TANK_HARD_LIMIT_F) {
            sys.safetyState = SAFETY_HIGHTEMP;
            digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
            sys.burnState = BURN_IDLE;
            sys.boostActive = false;
            sys.rampTimerActive = false;
            sys.holdTimerActive = false;
            return 0;
    }

    if (sys.safetyState != SAFETY_OK) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        return 0;
    }

    int demand = (sys.controlMode == RUNMODE_CONTINUOUS)
                     ? burnengine_computeContinuous()
                     : burnengine_computeAutoTank();

    // Run at max clamp while the exhaust probe is faulted or awaiting confirmation.
    if (exhaustFault &&
        (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD)) {
        demand = sys.clampMaxPercent;
    }

    return demand;
}

/* ============================================================
 *  HEAT-DEMAND HOLD DEMAND (v2.3-style)
 *  COLDER â†’ MORE fan, HOTTER â†’ LESS fan
 * ============================================================ */
static int burnengine_computeHoldDemand(double exhaustControlF,
                                        unsigned long now)
{
    if (isnan(exhaustControlF)) return 0;

    burnengine_updateAdaptiveSlope(exhaustControlF, now);

    double bandHalf = sys.deadbandF / 2.0;
    if (bandHalf <= 0) bandHalf = 1.0;

    double low  = sys.exhaustSetpoint - bandHalf;
    double high = sys.exhaustSetpoint + bandHalf;

    /* ============================================================
     *  â­ NEW FIX: EXIT HOLD â†’ RAMP WHEN EXHAUST DROPS BELOW BAND
     * ============================================================ */
    double holdExit = low - HOLD_EXIT_HYSTERESIS_F;
    if (sys.burnState == BURN_HOLD && exhaustControlF < holdExit) {
        if (holdLowSinceMs == 0) holdLowSinceMs = now;
        if (now - holdLowSinceMs >= HOLD_LOW_CONFIRM_MS) {
            sys.burnState = BURN_RAMP;
            holdLowSinceMs = 0;
            holdFanCalling = false;
            return sys.fanFinal;   // smooth transition at the previous fan level
        }
    } else {
        holdLowSinceMs = 0;
    }

    /* ============================================================
     *  MODE 1: FAN ALWAYS ON (UI option 1)
     * ============================================================ */
    if (sys.deadzoneFanMode == 1) {
        if (exhaustControlF <= low) {
            return sys.clampMaxPercent;   // COLD â†’ MORE FAN
        }
        if (exhaustControlF >= high) {
            return sys.clampMinPercent;   // HOT â†’ LESS FAN
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

        if (exhaustControlF < low) holdFanCalling = true;
        if (exhaustControlF >= sys.exhaustSetpoint) holdFanCalling = false;

        // In band â†’ OFF, unless still recovering from below the band
        if (exhaustControlF >= low && exhaustControlF <= high) {
            return holdFanCalling ? sys.clampMinPercent : 0;
        }

        // Below band â†’ scale from min clamp toward max clamp
        if (exhaustControlF < low) {
            double span = bandHalf > HOLD_RECOVERY_SPAN_F ? bandHalf : HOLD_RECOVERY_SPAN_F;
            double frac = ((low - exhaustControlF) / span) * adaptiveSlope;
            if (frac > 1.0) frac = 1.0;
            double pct = sys.clampMinPercent +
                         (sys.clampMaxPercent - sys.clampMinPercent) * frac;
            return (int)pct;
        }

        // Above band â†’ ramp down toward 0
        if (exhaustControlF > high) {
            double span = bandHalf;
            double e    = exhaustControlF - high;
            if (e >= span) return 0;
            double pct = (double)sys.clampMinPercent *
                         (1.0 - (e / span)) * adaptiveSlope;
            if (pct < 0) pct = 0;
            return (int)pct;
        }
    }

    return 0;
}

/* Ember Guardian debounce: the raw flue reading can bounce several
 * degrees near the thresholds. Require sustained conditions before the
 * countdown starts or cancels so it cannot restart/clear randomly. */
static const unsigned long GUARDIAN_START_CONFIRM_MS   = 15000UL;
static const unsigned long GUARDIAN_RECOVER_CONFIRM_MS = 30000UL;
static unsigned long guardianLowSinceMs       = 0;
static unsigned long guardianRecoveredSinceMs = 0;

/* ============================================================
 *  SHARED GUARDIAN + DAMPER + FAN APPLY
 * ============================================================ */
static int burnengine_finalize(int demand,
                               double exhaustGuardF,
                               unsigned long now)
{
    /* EMBER GUARDIAN TIMER + LATCH */
    // Exhaust reading is stale when the probe is faulted; don't let it trigger a shutdown.
    if (!sys.exhaustSensorOK || sys.exhaustFallbackActive) {
        sys.emberGuardianTimerActive = false;
        sys.emberGuardianStartMs     = 0;
        guardianLowSinceMs           = 0;
        guardianRecoveredSinceMs     = 0;
    }
    else if (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD) {

        bool flueBelowLow = (!isnan(exhaustGuardF) &&
                             exhaustGuardF < sys.flueLowThreshold);
        bool flueAboveRec = (!isnan(exhaustGuardF) &&
                             exhaustGuardF >= sys.flueRecoveryThreshold);

        /* START: the flue must hold below the low threshold for a
         * sustained window first. One noisy raw sample can no longer
         * start (or restart) the countdown.
         */
        if (!sys.emberGuardianTimerActive) {
            if (flueBelowLow) {
                if (guardianLowSinceMs == 0) guardianLowSinceMs = now;
                if (now - guardianLowSinceMs >= GUARDIAN_START_CONFIRM_MS) {
                    sys.emberGuardianActive      = false;
                    sys.emberGuardianStartMs     = now;
                    sys.emberGuardianTimerActive = true;
                    guardianLowSinceMs           = 0;
                    guardianRecoveredSinceMs     = 0;
                }
            } else {
                guardianLowSinceMs = 0;
            }
        }

        if (sys.emberGuardianTimerActive) {

            unsigned long elapsed = now - sys.emberGuardianStartMs;
            unsigned long limitMs = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;

            bool timerExpired = (elapsed >= limitMs);

            /* CANCEL: the flue must hold above the recovery threshold for
             * a sustained window. A single bouncing raw reading no longer
             * wipes the countdown (which made it look random).
             */
            if (flueAboveRec) {
                if (guardianRecoveredSinceMs == 0) guardianRecoveredSinceMs = now;
            } else {
                guardianRecoveredSinceMs = 0;
            }

            bool recoveryConfirmed =
                (guardianRecoveredSinceMs != 0) &&
                (now - guardianRecoveredSinceMs >= GUARDIAN_RECOVER_CONFIRM_MS);

            if (recoveryConfirmed) {
                sys.emberGuardianTimerActive = false;
                sys.emberGuardianActive      = false;
                sys.emberGuardianStartMs     = 0;
                guardianRecoveredSinceMs     = 0;
            }
            else if (timerExpired) {
                sys.burnState                = BURN_EMBER_GUARD;
                sys.boostActive              = false;
                sys.rampTimerActive          = false;
                sys.holdTimerActive          = false;

                sys.emberGuardianActive      = true;
                sys.emberGuardianLatched     = true;
                sys.emberGuardianTimerActive = false;
                guardianLowSinceMs           = 0;
                guardianRecoveredSinceMs     = 0;

                demand = 0;
            }
        }
    }
    else if (sys.burnState != BURN_EMBER_GUARD) {
        /* Left RAMP/HOLD without latching (auto-stop, mode change):
         * clear the timer so a stale countdown can't resume (or
         * instantly latch) on the next burn cycle.
         */
        sys.emberGuardianTimerActive = false;
        sys.emberGuardianStartMs     = 0;
        guardianLowSinceMs           = 0;
        guardianRecoveredSinceMs     = 0;
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

    /* Return demand; the main loop applies fan control and PWM once. */
    return demand;
}

/* ============================================================
 *  AUTO TANK ENGINE (v2.3-style behavior)
 * ============================================================ */
static int burnengine_computeAutoTank() {
    unsigned long now = millis();

    double exhaustControlF = sys.exhaustSmoothF;
    double exhaustGuardF   = sys.exhaustRawF;

    uint8_t tankIndex = sys.probeRoleMap[PROBE_TANK];
    double tankF = (tankIndex < sys.waterProbeCount)
                   ? sys.waterTempF[tankIndex]
                   : NAN;

    /* AUTO START */
    if (sys.burnState == BURN_IDLE) {
        if (!isnan(tankF) && tankF < sys.tankLowSetpointF) {
            burnengine_startBoost();
            if (sys.selfCleanEnabled && selfCleanMayStart()) {
                sys.selfCleanActive = true;
                selfCleanStartMs = now;
            }
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
            holdLowSinceMs              = 0;
        }
    }

    /* BOOST â†’ RAMP */
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

    /* RAMP â†’ HOLD (early entry) */
    if (sys.burnState == BURN_RAMP) {
        if (!sys.rampTimerActive) {
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }

        if (!isnan(exhaustControlF) &&
            exhaustControlF >= burnengine_holdEntryTemperature())
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
            demand = sys.selfCleanActive ? 100 : 100;
            break;

        case BURN_RAMP:
            if (sys.selfCleanActive) {
                demand = 100;
            } else if (isnan(exhaustControlF)) {
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
            demand = sys.selfCleanActive
                         ? 100
                         : burnengine_computeHoldDemand(exhaustControlF, now);
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

    /* AUTO-START: Continuous mode runs a power-switched install. On
     * power-up it must self-start a BOOST (no keypad press) and then run
     * the whole burn off exhaust temp, stopping only when the main unit
     * cuts power. IDLE is only reachable here at boot — Continuous never
     * transitions to IDLE on its own, and burnengine_resetAlarms() forces
     * SAFETY_OK before returning to IDLE — so a safety-latched shutdown
     * cannot auto-restart the fire.
     */
    if (sys.burnState == BURN_IDLE) {
        burnengine_startBoost();
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

    /* RAMP â†’ HOLD (early entry) */
    if (sys.burnState == BURN_RAMP) {
        if (!sys.rampTimerActive) {
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }

        if (!isnan(exhaustControlF) &&
            exhaustControlF >= burnengine_holdEntryTemperature())
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
