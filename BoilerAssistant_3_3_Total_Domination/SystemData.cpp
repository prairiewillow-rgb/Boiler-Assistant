/*
 * Boiler Assistant - SystemData
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "SystemData.h"
#include "SystemState.h"
#include <Arduino.h>

/* ============================================================
 *  GLOBAL INSTANCE
 * ============================================================ */
SystemData sys;

/* ============================================================
 *  POINTER HELPERS FOR UI
 * ============================================================ */
int16_t* ui_getSeasonStartPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envSummerStartF;
        case ENV_SEASON_SPRING_FALL: return &sys.envSpringFallStartF;
        case ENV_SEASON_WINTER:      return &sys.envWinterStartF;
        case ENV_SEASON_EXTREME:     return &sys.envExtremeStartF;
        default:                     return nullptr;
    }
}

int16_t* ui_getSeasonBufferPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envHystSummerF;
        case ENV_SEASON_SPRING_FALL: return &sys.envHystSpringFallF;
        case ENV_SEASON_WINTER:      return &sys.envHystWinterF;
        case ENV_SEASON_EXTREME:     return &sys.envHystExtremeF;
        default:                     return nullptr;
    }
}

int16_t* ui_getSeasonSetpointPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envSetpointSummerF;
        case ENV_SEASON_SPRING_FALL: return &sys.envSetpointSpringFallF;
        case ENV_SEASON_WINTER:      return &sys.envSetpointWinterF;
        case ENV_SEASON_EXTREME:     return &sys.envSetpointExtremeF;
        default:                     return nullptr;
    }
}

int16_t* ui_getSeasonTankHighPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envTankHighSummerF;
        case ENV_SEASON_SPRING_FALL: return &sys.envTankHighSpringFallF;
        case ENV_SEASON_WINTER:      return &sys.envTankHighWinterF;
        case ENV_SEASON_EXTREME:     return &sys.envTankHighExtremeF;
        default:                     return nullptr;
    }
}

int16_t* ui_getSeasonTankLowPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envTankLowSummerF;
        case ENV_SEASON_SPRING_FALL: return &sys.envTankLowSpringFallF;
        case ENV_SEASON_WINTER:      return &sys.envTankLowWinterF;
        case ENV_SEASON_EXTREME:     return &sys.envTankLowExtremeF;
        default:                     return nullptr;
    }
}

uint8_t* ui_getSeasonClampMaxPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return &sys.envClampMaxSummerPercent;
        case ENV_SEASON_SPRING_FALL: return &sys.envClampMaxSpringFallPercent;
        case ENV_SEASON_WINTER:      return &sys.envClampMaxWinterPercent;
        case ENV_SEASON_EXTREME:     return &sys.envClampMaxExtremePercent;
        default:                     return nullptr;
    }
}

/* ============================================================
 *  SYSTEM DEFAULTS
 * ============================================================ */
void systemdata_init()
{
    /* WATER PROBES */
    sys.waterProbeCount = 0;
    for (uint8_t i = 0; i < MAX_WATER_PROBES; i++) {
        sys.waterTempF[i]  = NAN;
        sys.waterTempLastGoodMs[i] = 0;
        sys.probeRoleMap[i] = 0;   // default role index 0 (tank or first role)
        snprintf(sys.waterProbeNames[i], PROBE_NAME_LENGTH, "Probe %u", i + 1);
    }

    /* EXHAUST */
    sys.exhaustSensorOK = false;
    sys.exhaustLastGoodMs = 0;
    sys.exhaustSmoothF  = NAN;
    sys.exhaustRawF     = NAN;
    sys.exhaustSetpoint = 450;
    sys.exhaustFallbackActive = false;

    /* FAN CONTROL */
    sys.clampMinPercent = 10;
    sys.clampMaxPercent = 60;
    sys.deadbandF       = 20;
    sys.deadzoneFanMode = 0;
    sys.fanDemand       = 0;
    sys.fanFinal        = 0;

    /* BOOST */
    sys.boostActive      = false;
    sys.boostStartMs     = 0;
    sys.boostTimeSeconds = 90;
    sys.boostFanPercent = sys.clampMaxPercent;
    sys.activeBoostFanPercent = 0;
    sys.damperDelaySeconds = 5;
    sys.damperDelayActive = false;

    /* SAFETY */
    sys.safetyState = SAFETY_OK;
    sys.sensorFaultMask = 0;

    /* BURN ENGINE */
    sys.burnState        = BURN_IDLE;
    sys.rampTimerActive  = false;
    sys.rampStartMs      = 0;
    sys.holdTimerActive  = false;
    sys.holdStartMs      = 0;

    /* GLOBAL TANK SETPOINTS */
    sys.tankLowSetpointF  = 150;
    sys.tankHighSetpointF = 170;

    /* CONTROL MODE */
    sys.controlMode = RUNMODE_AUTO_TANK;

    /* ENVIRONMENTAL SENSOR */
    sys.envSensorOK = false;
    sys.envTempF    = NAN;
    sys.envHumidity = NAN;
    sys.envPressure = NAN;
    sys.envUnitsMetric = 0;

    /* SEASONAL START TEMPS */
    sys.envSummerStartF      = 75;
    sys.envSpringFallStartF  = 45;
    sys.envWinterStartF      = 20;
    sys.envExtremeStartF     = 0;

    /* SEASONAL HYSTERESIS */
    sys.envHystSummerF      = 5;
    sys.envHystSpringFallF  = 5;
    sys.envHystWinterF      = 5;
    sys.envHystExtremeF     = 5;

    /* SEASONAL EXHAUST SETPOINTS */
    sys.envSetpointSummerF      = 450;
    sys.envSetpointSpringFallF  = 475;
    sys.envSetpointWinterF      = 500;
    sys.envSetpointExtremeF     = 525;

    for (uint8_t i = 0; i < 4; ++i) sys.envBoostTimeSeconds[i] = sys.boostTimeSeconds;
    for (uint8_t i = 0; i < 4; ++i) sys.envDamperDelaySeconds[i] = sys.damperDelaySeconds;

    /* SEASONAL PROFILES */
    sys.envTankHighSummerF        = 170;
    sys.envTankLowSummerF         = 150;
    sys.envClampMaxSummerPercent  = 40;

    sys.envTankHighSpringFallF        = 175;
    sys.envTankLowSpringFallF         = 155;
    sys.envClampMaxSpringFallPercent  = 50;

    sys.envTankHighWinterF        = 180;
    sys.envTankLowWinterF         = 160;
    sys.envClampMaxWinterPercent  = 60;

    sys.envTankHighExtremeF        = 185;
    sys.envTankLowExtremeF         = 165;
    sys.envClampMaxExtremePercent  = 70;
    for (uint8_t i = 0; i < 4; ++i)
        sys.envBoostFanPercent[i] = *ui_getSeasonClampMaxPtr(static_cast<EnvSeason>(i));

    /* AUTO-SEASON */
    sys.envAutoSeasonEnabled = false;
    sys.envSeasonMode        = 0;
    sys.envModeLockoutSec    = 0;

    /* ACTIVE ENVIRONMENT STATE */
    sys.envActiveSeason        = ENV_SEASON_NONE;
    sys.envSeasonChangedMs     = 0;
    sys.envActiveSetpointF     = sys.exhaustSetpoint;
    sys.envActiveClampPercent  = sys.clampMaxPercent;
    sys.envActiveTankHighF     = sys.tankHighSetpointF;
    sys.envActiveTankLowF      = sys.tankLowSetpointF;
    sys.envActiveRampProfile   = 0;
    sys.envFanBiasPercent      = 0;

    /* EMBER GUARDIAN */
    sys.emberGuardianActive       = false;
    sys.emberGuardianLatched      = false;
    sys.emberGuardianTimerActive  = false;
    sys.emberGuardianStartMs      = 0;
    sys.emberGuardianTimerMinutes = 30;
    sys.flueLowThreshold          = 120;
    sys.flueRecoveryThreshold     = 180;

    /* FAN OUTPUT / TELEMETRY */
    sys.fanFinal      = 0;
    sys.remoteChanged = false;

    sys.burnActiveStartMs = 0;
    sys.burnLastStartMs   = 0;

    /* UPTIME */
    sys.uptimeMs = 0;
    sys.loopLastMs = sys.loopMaxMs = 0;
    sys.networkLastMs = sys.networkMaxMs = 0;

    sys.selfCleanEnabled = false;
    sys.selfCleanDue = false;
    sys.selfCleanActive = false;
    sys.selfCleanManualRequested = false;
    sys.selfCleanBurnCount = 0;
    sys.selfCleanIntervalBurns = 30;
    sys.selfCleanStartHour = 23;
    sys.selfCleanEndHour = 6;
    sys.selfCleanUtcOffsetMinutes = -360;
    sys.selfCleanDstEnabled = false;

    /* PUSH NOTIFICATIONS */
    sys.pushEnabled = false;
    sys.pushTopic[0] = '\0';

    /* NETWORK / WIFI */
    sys.wifiOK = false;
 
    /* UI */
    sys.uiNeedsRefresh = true;
}
