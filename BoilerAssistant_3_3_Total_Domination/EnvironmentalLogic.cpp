/*
 * Boiler Assistant - EnvironmentalLogic
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "EnvironmentalLogic.h"
#include "SystemData.h"
#include <Arduino.h>

extern SystemData sys;

/* ============================================================
 *  DETERMINE ACTIVE SEASON
 * ============================================================ */
static EnvSeason classifySeason(float temperatureF)
{
    if (temperatureF <= sys.envExtremeStartF) return ENV_SEASON_EXTREME;
    if (temperatureF <= sys.envWinterStartF) return ENV_SEASON_WINTER;
    if (temperatureF <= sys.envSpringFallStartF) return ENV_SEASON_SPRING_FALL;
    return ENV_SEASON_SUMMER;
}

static EnvSeason determineSeason()
{
    if (!sys.envSensorOK || isnan(sys.envTempF)) {
        return sys.envActiveSeason == ENV_SEASON_NONE
            ? ENV_SEASON_SUMMER
            : sys.envActiveSeason;
    }

    float temperatureF = sys.envTempF;
    if (sys.envActiveSeason == ENV_SEASON_NONE) {
        return classifySeason(temperatureF);
    }

    switch (sys.envActiveSeason) {
        case ENV_SEASON_SUMMER:
            return temperatureF <= sys.envSpringFallStartF - sys.envHystSpringFallF
                ? ENV_SEASON_SPRING_FALL : ENV_SEASON_SUMMER;
        case ENV_SEASON_SPRING_FALL:
            if (temperatureF > sys.envSpringFallStartF + sys.envHystSummerF)
                return ENV_SEASON_SUMMER;
            if (temperatureF <= sys.envWinterStartF - sys.envHystWinterF)
                return ENV_SEASON_WINTER;
            return ENV_SEASON_SPRING_FALL;
        case ENV_SEASON_WINTER:
            if (temperatureF > sys.envWinterStartF + sys.envHystSpringFallF)
                return ENV_SEASON_SPRING_FALL;
            if (temperatureF <= sys.envExtremeStartF - sys.envHystExtremeF)
                return ENV_SEASON_EXTREME;
            return ENV_SEASON_WINTER;
        case ENV_SEASON_EXTREME:
            return temperatureF > sys.envExtremeStartF + sys.envHystWinterF
                ? ENV_SEASON_WINTER : ENV_SEASON_EXTREME;
        default:
            return classifySeason(temperatureF);
    }
}

/* ============================================================
 *  APPLY SEASONAL OVERRIDES (Model B)
 * ============================================================ */
static void applySeasonalOverrides(EnvSeason s)
{
    if (sys.envAutoSeasonEnabled) {
    if (s >= ENV_SEASON_SUMMER && s <= ENV_SEASON_EXTREME) {
        sys.boostTimeSeconds = sys.envBoostTimeSeconds[static_cast<uint8_t>(s)];
        sys.boostFanPercent = sys.envBoostFanPercent[static_cast<uint8_t>(s)];
        sys.damperDelaySeconds = sys.envDamperDelaySeconds[static_cast<uint8_t>(s)];
    }

    /* Exhaust Setpoint */
    switch (s) {
        case ENV_SEASON_SUMMER:
            sys.exhaustSetpoint = sys.envSetpointSummerF;
            break;
        case ENV_SEASON_SPRING_FALL:
            sys.exhaustSetpoint = sys.envSetpointSpringFallF;
            break;
        case ENV_SEASON_WINTER:
            sys.exhaustSetpoint = sys.envSetpointWinterF;
            break;
        case ENV_SEASON_EXTREME:
            sys.exhaustSetpoint = sys.envSetpointExtremeF;
            break;
    }

    /* Tank High / Low / ClampMax */
    switch (s) {
        case ENV_SEASON_SUMMER:
            sys.tankHighSetpointF = sys.envTankHighSummerF;
            sys.tankLowSetpointF  = sys.envTankLowSummerF;
            sys.clampMaxPercent   = sys.envClampMaxSummerPercent;
            break;

        case ENV_SEASON_SPRING_FALL:
            sys.tankHighSetpointF = sys.envTankHighSpringFallF;
            sys.tankLowSetpointF  = sys.envTankLowSpringFallF;
            sys.clampMaxPercent   = sys.envClampMaxSpringFallPercent;
            break;

        case ENV_SEASON_WINTER:
            sys.tankHighSetpointF = sys.envTankHighWinterF;
            sys.tankLowSetpointF  = sys.envTankLowWinterF;
            sys.clampMaxPercent   = sys.envClampMaxWinterPercent;
            break;

        case ENV_SEASON_EXTREME:
            sys.tankHighSetpointF = sys.envTankHighExtremeF;
            sys.tankLowSetpointF  = sys.envTankLowExtremeF;
            sys.clampMaxPercent   = sys.envClampMaxExtremePercent;
            break;
    }

    }

    sys.envActiveSetpointF = sys.exhaustSetpoint;
    sys.envActiveClampPercent = sys.clampMaxPercent;
    sys.envActiveTankHighF = sys.tankHighSetpointF;
    sys.envActiveTankLowF = sys.tankLowSetpointF;
}

/* ============================================================
 *  PUBLIC: INIT ENVIRONMENTAL LOGIC
 * ============================================================ */
void env_logic_init()
{
    // Force initial season evaluation
    EnvSeason s = determineSeason();
    applySeasonalOverrides(s);

    // Store active season for UI
    sys.envActiveSeason = s;
    sys.envSeasonChangedMs = millis();
}

/* ============================================================
 *  PUBLIC: UPDATE ENVIRONMENTAL LOGIC
 * ============================================================ */
void env_logic_update(unsigned long nowMs)
{
    EnvSeason candidate = determineSeason();

    if (candidate != sys.envActiveSeason) {
        unsigned long lockoutMs = (unsigned long)sys.envModeLockoutSec * 1000UL;
        bool lockoutActive = sys.envModeLockoutSec > 0 &&
                             nowMs - sys.envSeasonChangedMs < lockoutMs;

        if (!lockoutActive) {
            sys.envActiveSeason = candidate;
            sys.envSeasonChangedMs = nowMs;
        }
    }

    applySeasonalOverrides(sys.envActiveSeason);
}
