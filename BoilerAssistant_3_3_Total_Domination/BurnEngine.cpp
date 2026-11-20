/*
 * Boiler Assistant - BurnEngine
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
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
#include "WiFiProvisioning.h"
#include "OTAUpdater.h"
#include "DataLogger.h"

extern SystemData sys;
static unsigned long activeBoostDurationMs = 0;
static unsigned long activeDamperDelayMs = 0;

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
static unsigned long guardianLowSinceMs = 0;
static unsigned long guardianRecoveredSinceMs = 0;
static bool guardianLowPending = false;
static bool guardianRecoveryPending = false;
static bool guardianPaused = false;
static bool guardianEvaluated = false;
static unsigned long guardianLastEvalMs = 0;

static void resetGuardianTiming() {
    guardianLowSinceMs = guardianRecoveredSinceMs = 0;
    guardianLowPending = guardianRecoveryPending = false;
    guardianPaused = guardianEvaluated = false;
    guardianLastEvalMs = 0;
}

bool burnengine_guardianPaused() {
    return guardianPaused && !sys.exhaustFallbackActive &&
           (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD);
}
// Degrees below the band over which HOLD fan demand scales from min to max clamp.
static const double HOLD_RECOVERY_SPAN_F = 50.0;

static float adaptiveSlope = 1.0f;
static bool adaptiveLearningEnabled = false;
static unsigned long adaptiveSampleMs = 0;
static double adaptiveLowErrorF = 0.0;
static double adaptiveHighErrorF = 0.0;
static double adaptiveFanDuty = 0.0;
static double adaptiveRecoverySec = 0.0;
static double adaptiveMaxHighF = 0.0;
static double adaptiveValidSec = 0.0;
static double adaptiveHoldSec = 0.0;
static double adaptiveInBandSec = 0.0;
static bool adaptiveSeenInBand = false;
static bool adaptiveDisqualified = false;
static char adaptiveLastResult[48] = "Waiting for a qualified burn";

/* Predictive (PD) HOLD: control on where the exhaust will be in a few seconds,
 * exhaust + slope * lookahead. Equivalent to Kd = Kp * lookahead. */
static const uint8_t SLOPE_WINDOW_SAMPLES = 21;   // 1 s samples -> 20 s slope window
static const double PREDICT_MIN_LIMIT_F = 25.0;   // prediction shift cap (or half band if larger)
static double slopeSampleF[SLOPE_WINDOW_SAMPLES];
static unsigned long slopeSampleMs[SLOPE_WINDOW_SAMPLES];
static uint8_t slopeHead = 0;
static uint8_t slopeCount = 0;
static bool slopeValid = false;
static double exhaustSlopeFpm = 0.0;
static uint8_t predictLookaheadSec = PREDICT_LOOKAHEAD_DEFAULT_SEC;
static const uint8_t PREDICT_LOOKAHEAD_STEP_SEC = 5;
static const float ADAPTIVE_GAIN_STEP = 1.05f;
static const unsigned long SELF_CLEAN_MAX_MS = 3600000UL;
static unsigned long selfCleanStartMs = 0;

static bool selfCleanNightAllowed() {
    unsigned long utc = wifi_prov_cachedUTC();
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

bool burnengine_getAdaptiveLearning() {
    return adaptiveLearningEnabled;
}

void burnengine_setAdaptiveLearning(bool enabled) {
    adaptiveLearningEnabled = enabled;
    if (!enabled) {
        adaptiveDisqualified = true;
        strncpy(adaptiveLastResult, "Learning disabled", sizeof(adaptiveLastResult) - 1);
    }
    eeprom_saveAdaptiveLearning(enabled);
}

void burnengine_resetAdaptiveLearning() {
    adaptiveSlope = 1.0f;
    predictLookaheadSec = PREDICT_LOOKAHEAD_DEFAULT_SEC;
    adaptiveDisqualified = true;
    eeprom_saveAdaptiveSlope(adaptiveSlope);
    eeprom_savePredictLookahead(predictLookaheadSec);
    snprintf(adaptiveLastResult, sizeof(adaptiveLastResult),
             "Learning reset: gain 1.000, look %us", (unsigned)predictLookaheadSec);
}

static uint8_t burnengine_activeLookahead() {
    return datalogger_ready() ? predictLookaheadSec : PREDICT_LOOKAHEAD_DEFAULT_SEC;
}

void burnengine_getAdaptiveDiagnostics(char* out, size_t size) {
    if (!out || !size) return;
    bool cardReady = datalogger_ready();
    snprintf(out, size, "%s; gain %.3f%s; look %us; slope %+.0fF/min; in %.0f%%; under %.1fF; over %.1fF; recovery %.0fs; fan %.0f%%",
             !cardReady ? "SD absent: fixed fallback" :
             adaptiveLearningEnabled ? adaptiveLastResult : "Learning disabled",
             cardReady ? adaptiveSlope : 1.0f,
             !cardReady && adaptiveSlope != 1.0f ? " (saved gain held)" : "",
             (unsigned)burnengine_activeLookahead(),
             slopeValid ? exhaustSlopeFpm : 0.0,
             adaptiveValidSec > 0 ? adaptiveInBandSec * 100.0 / adaptiveValidSec : 0,
             adaptiveValidSec > 0 ? adaptiveLowErrorF / adaptiveValidSec : 0,
             adaptiveValidSec > 0 ? adaptiveHighErrorF / adaptiveValidSec : 0,
             adaptiveRecoverySec,
             adaptiveValidSec > 0 ? adaptiveFanDuty / adaptiveValidSec : 0);
}

static void burnengine_resetAdaptiveBurn(unsigned long now) {
    adaptiveSampleMs = now;
    adaptiveLowErrorF = 0.0;
    adaptiveHighErrorF = 0.0;
    adaptiveFanDuty = 0.0;
    adaptiveRecoverySec = 0.0;
    adaptiveMaxHighF = 0.0;
    adaptiveValidSec = 0.0;
    adaptiveHoldSec = 0.0;
    adaptiveInBandSec = 0.0;
    adaptiveSeenInBand = false;
    adaptiveDisqualified = !adaptiveLearningEnabled;
}

static void burnengine_finishAdaptiveBurn() {
    // Burns run only 5-10 minutes, so two minutes of clean HOLD is enough to score.
    const double MIN_HOLD_SEC = 120.0;
    if (!adaptiveLearningEnabled) {
        strncpy(adaptiveLastResult, "Skipped: learning disabled", sizeof(adaptiveLastResult) - 1);
        return;
    }
    if (adaptiveDisqualified) {
        strncpy(adaptiveLastResult, "Skipped: interrupted or override", sizeof(adaptiveLastResult) - 1);
        return;
    }
    if (adaptiveHoldSec < MIN_HOLD_SEC ||
        adaptiveValidSec < adaptiveHoldSec * 0.90) {
        strncpy(adaptiveLastResult, "Skipped: insufficient valid HOLD", sizeof(adaptiveLastResult) - 1);
        return;
    }

    float before = adaptiveSlope;
    uint8_t lookBefore = predictLookaheadSec;
    double lowAverage = adaptiveLowErrorF / adaptiveValidSec;
    double highAverage = adaptiveHighErrorF / adaptiveValidSec;
    double bandRatio = adaptiveInBandSec / adaptiveValidSec;
    double halfBand = sys.deadbandF / 2.0;
    // Learn from this burn only, and only when it was clearly wrong; one 5% gain
    // or 5 s lookahead step per burn, undoing the opposite adjustment first.
    bool overshoot = highAverage > 2.0 || adaptiveMaxHighF > max(halfBand, 10.0);
    bool sluggish = lowAverage > 2.0 && bandRatio < 0.75 && adaptiveMaxHighF <= 5.0;
    if (overshoot) {
        if (adaptiveSlope > 1.0f || predictLookaheadSec >= PREDICT_LOOKAHEAD_MAX_SEC) {
            adaptiveSlope = constrain(adaptiveSlope / ADAPTIVE_GAIN_STEP, 0.85f, 1.20f);
            strncpy(adaptiveLastResult, "Gain lowered after overshoot", sizeof(adaptiveLastResult) - 1);
        } else {
            predictLookaheadSec += PREDICT_LOOKAHEAD_STEP_SEC;
            snprintf(adaptiveLastResult, sizeof(adaptiveLastResult),
                     "Lookahead raised to %us after overshoot", (unsigned)predictLookaheadSec);
        }
    } else if (sluggish) {
        if (predictLookaheadSec > PREDICT_LOOKAHEAD_DEFAULT_SEC ||
            (adaptiveSlope >= 1.20f && predictLookaheadSec > 0)) {
            predictLookaheadSec -= PREDICT_LOOKAHEAD_STEP_SEC;
            snprintf(adaptiveLastResult, sizeof(adaptiveLastResult),
                     "Lookahead cut to %us after slow recovery", (unsigned)predictLookaheadSec);
        } else {
            adaptiveSlope = constrain(adaptiveSlope * ADAPTIVE_GAIN_STEP, 0.85f, 1.20f);
            strncpy(adaptiveLastResult, "Gain raised after slow recovery", sizeof(adaptiveLastResult) - 1);
        }
    } else {
        strncpy(adaptiveLastResult, "No change: burn stayed balanced", sizeof(adaptiveLastResult) - 1);
    }
    if (fabsf(adaptiveSlope - before) >= 0.0001f) {
        eeprom_saveAdaptiveSlope(adaptiveSlope);
    }
    if (predictLookaheadSec != lookBefore) {
        eeprom_savePredictLookahead(predictLookaheadSec);
    }
    adaptiveDisqualified = true;
}

static void burnengine_sampleSlope(double exhaustF, bool fault, unsigned long now) {
    if (fault || !isfinite(exhaustF)) {
        slopeCount = 0;
        slopeValid = false;
        exhaustSlopeFpm = 0.0;
        return;
    }
    if (slopeCount) {
        uint8_t newest = (slopeHead + SLOPE_WINDOW_SAMPLES - 1) % SLOPE_WINDOW_SAMPLES;
        unsigned long gap = now - slopeSampleMs[newest];
        if (gap < 1000UL) return;
        if (gap > 3000UL) {
            slopeCount = 0;   // stalled loop: restart the window
            slopeValid = false;
        }
    }
    slopeSampleF[slopeHead] = exhaustF;
    slopeSampleMs[slopeHead] = now;
    slopeHead = (slopeHead + 1) % SLOPE_WINDOW_SAMPLES;
    if (slopeCount < SLOPE_WINDOW_SAMPLES) ++slopeCount;
    if (slopeCount < SLOPE_WINDOW_SAMPLES) return;
    // Buffer full: slopeHead now points at the oldest sample.
    unsigned long span = now - slopeSampleMs[slopeHead];
    if (span == 0) return;
    exhaustSlopeFpm = (exhaustF - slopeSampleF[slopeHead]) * 60000.0 / (double)span;
    slopeValid = true;
}

static double burnengine_predictExhaust(double exhaustF) {
    if (!slopeValid || !isfinite(exhaustF)) return exhaustF;
    double shift = exhaustSlopeFpm * burnengine_activeLookahead() / 60.0;
    double limit = max(sys.deadbandF / 2.0, PREDICT_MIN_LIMIT_F);
    return exhaustF + constrain(shift, -limit, limit);
}

static void burnengine_sampleAdaptive(double exhaustControlF,
                                      unsigned long now) {
    if (adaptiveSampleMs == 0) adaptiveSampleMs = now;
    double elapsed = (now - adaptiveSampleMs) / 1000.0;
    adaptiveSampleMs = now;
    if (elapsed <= 0 || elapsed > 2.0) return;
    if (sys.burnState != BURN_HOLD) return;

    adaptiveHoldSec += elapsed;
    if (!sys.exhaustSensorOK || now - sys.exhaustLastGoodMs > 1000UL ||
        !isfinite(exhaustControlF) || sys.exhaustFallbackActive) {
        adaptiveDisqualified = true;
        return;
    }
    double halfBand = sys.deadbandF / 2.0;
    double low = sys.exhaustSetpoint - halfBand;
    double high = sys.exhaustSetpoint + halfBand;
    adaptiveValidSec += elapsed;
    adaptiveFanDuty += sys.fanFinal * elapsed;
    if (exhaustControlF >= low && exhaustControlF <= high) {
        adaptiveInBandSec += elapsed;
        adaptiveSeenInBand = true;
    } else if (exhaustControlF < low) {
        adaptiveLowErrorF += (low - exhaustControlF) * elapsed;
        if (!adaptiveSeenInBand) adaptiveRecoverySec += elapsed;
    } else {
        double over = exhaustControlF - high;
        adaptiveHighErrorF += over * elapsed;
        if (over > adaptiveMaxHighF) adaptiveMaxHighF = over;
    }
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
        burnengine_resetAdaptiveBurn(now);
    } else if (historyLastState != BURN_HOLD && sys.burnState == BURN_HOLD) {
        burnengine_resetAdaptiveBurn(now);
    } else if (wasActive && !isActive && sys.burnActiveStartMs != 0) {
        if (sys.safetyState != SAFETY_OK || sys.exhaustFallbackActive ||
            sys.emberGuardianLatched || sys.selfCleanActive) {
            adaptiveDisqualified = true;
        }
        burnengine_finishAdaptiveBurn();
        uint32_t duration = (uint32_t)((now - sys.burnActiveStartMs) / 1000UL);
        uint32_t interval = sys.burnLastStartMs == 0
                                ? 0
                                : (uint32_t)((sys.burnActiveStartMs - sys.burnLastStartMs) / 1000UL);

        datalogger_recordHistoryBurn(duration, interval);
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
    if (sys.burnState == BURN_HOLD && (sys.selfCleanActive ||
                     sys.emberGuardianActive || sys.emberGuardianLatched ||
                     sys.exhaustFallbackActive || !sys.exhaustSensorOK ||
                     sys.deadzoneFanMode != 0 || !datalogger_ready() ||
                     sys.safetyState != SAFETY_OK ||
                     ota_isActive())) {
        adaptiveDisqualified = true;
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
    resetGuardianTiming();
    sys.boostActive = false;
    sys.damperDelayActive = false;
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
    predictLookaheadSec = eeprom_loadPredictLookahead();
    slopeCount = 0;
    slopeValid = false;
    adaptiveLearningEnabled = eeprom_loadAdaptiveLearning();
    adaptiveSampleMs = 0;
    adaptiveDisqualified = true;
    strncpy(adaptiveLastResult,
            adaptiveLearningEnabled ? "Waiting for a qualified burn" : "Learning disabled",
            sizeof(adaptiveLastResult) - 1);
    exhaustFaultSinceMs = 0;
    tankFaultSinceMs = 0;

    sys.boostActive        = false;
    sys.damperDelayActive  = false;
    sys.holdTimerActive    = false;
    sys.rampTimerActive    = false;

    sys.emberGuardianActive      = false;
    sys.emberGuardianStartMs     = 0;
    sys.emberGuardianLatched     = false;
    sys.emberGuardianTimerActive = false;
    resetGuardianTiming();

    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, HIGH);   // BOOT = CLOSED
}

/* ============================================================
 *  BOOST START
 * ============================================================ */
void burnengine_startBoost() {
    sys.boostActive  = true;
    sys.boostStartMs = millis();
    activeBoostDurationMs = (unsigned long)sys.boostTimeSeconds * 1000UL;
    int delaySeconds = constrain((int)sys.damperDelaySeconds, 0, 300);
    if (delaySeconds != sys.damperDelaySeconds)
        Serial.println("Burn: damper delay limited to 0-300 seconds");
    activeDamperDelayMs = (unsigned long)delaySeconds * 1000UL;
    sys.damperDelayActive = activeDamperDelayMs > 0;
    if (sys.clampMinPercent > sys.clampMaxPercent) {
        sys.activeBoostFanPercent = 0;
        Serial.println("Boost: invalid fan limits; fan disabled");
    } else {
        sys.activeBoostFanPercent = constrain((int)sys.boostFanPercent,
                                             sys.clampMinPercent, sys.clampMaxPercent);
        if (sys.activeBoostFanPercent != sys.boostFanPercent)
            Serial.println("Boost: speed limited to starting fan limits");
    }

    sys.emberGuardianActive      = false;
    sys.emberGuardianStartMs     = 0;
    sys.emberGuardianLatched     = false;
    sys.emberGuardianTimerActive = false;
    resetGuardianTiming();

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
        sys.damperDelayActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        return 0;
    }

    if (ota_isActive()) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.damperDelayActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        return 0;
    }

    if (sys.safetyState == SAFETY_SENSOR_FAULT) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.damperDelayActive = false;
        sys.rampTimerActive = false;
        sys.holdTimerActive = false;
        return 0;
    }

    if (sys.safetyState == SAFETY_HIGHTEMP) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.damperDelayActive = false;
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
    burnengine_sampleSlope(sys.exhaustSmoothF, exhaustFault, now);

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
            sys.damperDelayActive = false;
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
            sys.damperDelayActive = false;
            sys.rampTimerActive = false;
            sys.holdTimerActive = false;
            return 0;
    }

    if (sys.safetyState != SAFETY_OK) {
        digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
        sys.burnState = BURN_IDLE;
        sys.boostActive = false;
        sys.damperDelayActive = false;
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
 *  COLDER -> MORE fan, HOTTER -> LESS fan
 * ============================================================ */
static int burnengine_computeHoldDemand(double exhaustControlF,
                                        unsigned long now)
{
    if (isnan(exhaustControlF)) return 0;

    burnengine_sampleAdaptive(exhaustControlF, now);

    float curveGain = datalogger_ready() ? adaptiveSlope : 1.0f;
    double bandHalf = sys.deadbandF / 2.0;
    if (bandHalf <= 0) bandHalf = 1.0;

    double low  = sys.exhaustSetpoint - bandHalf;
    double high = sys.exhaustSetpoint + bandHalf;

    /* ============================================================
     *  * NEW FIX: EXIT HOLD -> RAMP WHEN EXHAUST DROPS BELOW BAND
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

    // Fan decisions use the predicted exhaust; the HOLD exit above stays on the
    // measured value so prediction cannot drop the burn back to RAMP.
    exhaustControlF = burnengine_predictExhaust(exhaustControlF);

    /* ============================================================
     *  MODE 1: FAN ALWAYS ON (UI option 1)
     * ============================================================ */
    if (sys.deadzoneFanMode == 1) {
        if (exhaustControlF <= low) {
            return sys.clampMaxPercent;   // COLD -> MORE FAN
        }
        if (exhaustControlF >= high) {
            return sys.clampMinPercent;   // HOT -> LESS FAN
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

        // In band -> OFF, unless still recovering from below the band
        if (exhaustControlF >= low && exhaustControlF <= high) {
            return holdFanCalling ? sys.clampMinPercent : 0;
        }

        // Below band -> scale from min clamp toward max clamp
        if (exhaustControlF < low) {
            double span = bandHalf > HOLD_RECOVERY_SPAN_F ? bandHalf : HOLD_RECOVERY_SPAN_F;
            double frac = ((low - exhaustControlF) / span) * curveGain;
            if (frac > 1.0) frac = 1.0;
            double pct = sys.clampMinPercent +
                         (sys.clampMaxPercent - sys.clampMinPercent) * frac;
            return (int)pct;
        }

        // Above band -> ramp down toward 0
        if (exhaustControlF > high) {
            double span = bandHalf;
            double e    = exhaustControlF - high;
            if (e >= span) return 0;
            double pct = (double)sys.clampMinPercent *
                         (1.0 - (e / span)) * (2.0 - curveGain);
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

/* ============================================================
 *  SHARED GUARDIAN + DAMPER + FAN APPLY
 * ============================================================ */
static int burnengine_finalize(int demand,
                               double exhaustGuardF,
                               unsigned long now)
{
    /* EMBER GUARDIAN TIMER + LATCH */
    // A confirmed fault resets Guardian. Unconfirmed gaps pause it without
    // counting stale data or discarding already-qualified low-flue time.
    bool exhaustFresh = sys.exhaustSensorOK && isfinite(exhaustGuardF) &&
                        now - sys.exhaustLastGoodMs <= 1000UL;
    if (sys.exhaustFallbackActive) {
        sys.emberGuardianTimerActive = false;
        sys.emberGuardianStartMs     = 0;
        resetGuardianTiming();
    }
    else if (sys.burnState == BURN_RAMP || sys.burnState == BURN_HOLD) {
        unsigned long elapsed = guardianEvaluated ? now - guardianLastEvalMs : 0;
        bool wasPaused = guardianPaused;
        guardianLastEvalMs = now;
        guardianEvaluated = true;
        guardianPaused = !exhaustFresh;
        if (guardianPaused || wasPaused) {
            if (sys.emberGuardianTimerActive) sys.emberGuardianStartMs += elapsed;
            if (guardianLowPending) guardianLowSinceMs += elapsed;
            if (guardianRecoveryPending) guardianRecoveredSinceMs += elapsed;
        }
        if (exhaustFresh) {
            bool flueBelowLow = exhaustGuardF < sys.flueLowThreshold;
            bool flueAboveRec = exhaustGuardF >= sys.flueRecoveryThreshold;

            /* START: require qualified low-flue time. Unconfirmed invalid
             * intervals are excluded; valid readings above low restart it.
             */
            if (!sys.emberGuardianTimerActive) {
                if (flueBelowLow) {
                    if (!guardianLowPending) {
                        guardianLowSinceMs = now;
                        guardianLowPending = true;
                    }
                    if (now - guardianLowSinceMs >= GUARDIAN_START_CONFIRM_MS) {
                        sys.emberGuardianActive      = false;
                        sys.emberGuardianStartMs     = now;
                        sys.emberGuardianTimerActive = true;
                        guardianLowSinceMs           = 0;
                        guardianRecoveredSinceMs     = 0;
                        guardianLowPending           = false;
                        guardianRecoveryPending      = false;
                    }
                } else {
                    guardianLowSinceMs = 0;
                    guardianLowPending = false;
                }
            }

            if (sys.emberGuardianTimerActive) {
                unsigned long elapsed = now - sys.emberGuardianStartMs;
                unsigned long limitMs = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;
                bool timerExpired = elapsed >= limitMs;

                /* CANCEL: require qualified recovery time; invalid intervals
                 * neither prove recovery nor erase the pending countdown.
                 */
                if (flueAboveRec) {
                    if (!guardianRecoveryPending) {
                        guardianRecoveredSinceMs = now;
                        guardianRecoveryPending = true;
                    }
                } else {
                    guardianRecoveredSinceMs = 0;
                    guardianRecoveryPending = false;
                }

                bool recoveryConfirmed = guardianRecoveryPending &&
                    now - guardianRecoveredSinceMs >= GUARDIAN_RECOVER_CONFIRM_MS;

                if (recoveryConfirmed) {
                    sys.emberGuardianTimerActive = false;
                    sys.emberGuardianActive      = false;
                    sys.emberGuardianStartMs     = 0;
                    guardianRecoveredSinceMs     = 0;
                    guardianRecoveryPending      = false;
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
                    guardianLowPending           = false;
                    guardianRecoveryPending      = false;
                    demand = 0;
                }
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
        resetGuardianTiming();
    }

    /* GUARDIAN RETURN PATH (LATCHED SHUTDOWN) */
    if (sys.emberGuardianLatched) {
        sys.burnState = BURN_EMBER_GUARD;
        sys.damperDelayActive = false;
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

    /* BOOST already latched its starting limits; other demands use live limits. */
    if (demand > 0 && sys.burnState != BURN_BOOST) {
        demand = constrain(demand, sys.clampMinPercent, sys.clampMaxPercent);
    } else if (demand <= 0) {
        demand = 0;
    }

    /* Return demand; the main loop applies fan control and PWM once. */
    return demand;
}

static int burnengine_computeActive(unsigned long now,
                                    double exhaustControlF,
                                    double exhaustGuardF,
                                    bool selfCleaning) {
    /* BOOST -> RAMP */
    if (sys.burnState == BURN_BOOST) {
        unsigned long elapsed = now - sys.boostStartMs;
        if (sys.boostActive && sys.damperDelayActive) {
            if (elapsed < activeDamperDelayMs)
                return burnengine_finalize(0, exhaustGuardF, now);
            sys.damperDelayActive = false;
            sys.boostStartMs = now;
            elapsed = 0;
        }
        if (!sys.boostActive ||
            elapsed >= activeBoostDurationMs)
        {
            sys.boostActive = false;
            sys.damperDelayActive = false;
            sys.burnState   = BURN_RAMP;
            sys.rampTimerActive = true;
            sys.rampStartMs     = now;
        }
    }

    /* RAMP -> HOLD (early entry) */
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
            demand = sys.activeBoostFanPercent;
            break;

        case BURN_RAMP:
            if (selfCleaning) {
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
            demand = selfCleaning
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
            now = millis();
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
            sys.damperDelayActive        = false;
            sys.rampTimerActive          = false;
            sys.holdTimerActive          = false;
            sys.emberGuardianActive      = false;
            sys.emberGuardianTimerActive = false;
            holdLowSinceMs              = 0;
        }
    }

    return burnengine_computeActive(now, exhaustControlF, exhaustGuardF, sys.selfCleanActive);
}

/* ============================================================
 *  CONTINUOUS ENGINE
 * ============================================================ */
static int burnengine_computeContinuous() {
    unsigned long now = millis();
    // Power-switched install: self-start BOOST at power-up and run until the
    // main unit cuts power (3.3.9/3.3.12 behavior; does not follow Z-C).
    // A latched Ember Guardian never auto-restarts the fire.
    if (sys.burnState == BURN_IDLE && !sys.emberGuardianLatched) {
        burnengine_startBoost();
        now = millis();
    }
    return burnengine_computeActive(now, sys.exhaustSmoothF, sys.exhaustRawF, false);
}
