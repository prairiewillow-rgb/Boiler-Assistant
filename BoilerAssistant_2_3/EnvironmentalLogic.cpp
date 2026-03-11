/*
 * ============================================================
 *  Boiler Assistant – Environmental Logic (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: EnvironmentalLogic.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Implements the seasonal/environmental logic subsystem.
 *
 *      Responsibilities:
 *        • Determine the active season (SUMMER / SPF / WINTER / EXTREME)
 *        • Select the effective exhaust setpoint for Burn Engine
 *        • Select clamp and ramp profile overrides
 *        • Provide fan bias (future expansion)
 *
 *      Inputs:
 *        • Outdoor temperature (envTempF)
 *        • Seasonal thresholds + hysteresis (EEPROM-backed)
 *        • Operator mode (OFF / USER / AUTO)
 *
 *      Outputs (consumed by BurnEngine.cpp):
 *        • envActiveSeason
 *        • envActiveSetpointF
 *        • envActiveClampPercent
 *        • envActiveRampProfile
 *        • envFanBiasPercent
 *
 *  v2.3‑Environmental Notes:
 *      - AUTO mode uses simple boundary-based season selection.
 *      - Hysteresis + lockout timing will be added in v2.4.
 *      - USER mode forces global defaults (no seasonal override).
 *      - OFF mode disables environmental logic entirely.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#include <Arduino.h>
#include "EnvironmentalLogic.h"
#include "SystemState.h"

/* ============================================================
 *  ACTIVE ENVIRONMENT STATE (runtime outputs)
 * ============================================================ */

EnvSeason envActiveSeason        = ENV_SEASON_NONE;
int16_t   envActiveSetpointF     = 0;
uint8_t   envActiveClampPercent  = 100;
uint8_t   envActiveRampProfile   = 1;
int8_t    envFanBiasPercent      = 0;

/* ============================================================
 *  INTERNAL HELPERS
 * ============================================================ */

/**
 * Pick a season based purely on outdoor temperature.
 * Hysteresis and lockout will be added in v2.4.
 */
static EnvSeason env_pickSeason(float tF)
{
    if (!envSensorOK || isnan(tF)) {
        return ENV_SEASON_NONE;
    }

    if (tF >= envSummerStartF) {
        return ENV_SEASON_SUMMER;
    } else if (tF >= envSpringFallStartF) {
        return ENV_SEASON_SPRINGFALL;
    } else if (tF >= envWinterStartF) {
        return ENV_SEASON_WINTER;
    } else if (tF <= envExtremeStartF) {
        return ENV_SEASON_EXTREME;
    } else {
        // Between winter and extreme → treat as winter
        return ENV_SEASON_WINTER;
    }
}

/**
 * Return the seasonal exhaust setpoint for the given season.
 */
static int16_t env_getSeasonSetpoint(EnvSeason s)
{
    switch (s) {
        case ENV_SEASON_SUMMER:      return envSetpointSummerF;
        case ENV_SEASON_SPRINGFALL:  return envSetpointSpringFallF;
        case ENV_SEASON_WINTER:      return envSetpointWinterF;
        case ENV_SEASON_EXTREME:     return envSetpointExtremeF;
        default:                     return exhaustSetpoint; // fallback
    }
}

/* ============================================================
 *  API IMPLEMENTATION
 * ============================================================ */

void env_logic_init()
{
    // Start with no seasonal override
    envActiveSeason       = ENV_SEASON_NONE;
    envActiveSetpointF    = exhaustSetpoint;
    envActiveClampPercent = clampMaxPercent;
    envActiveRampProfile  = globalRampProfile;
    envFanBiasPercent     = 0;
}

void env_logic_update(unsigned long nowMs)
{
    (void)nowMs; // reserved for future lockout/hysteresis timing

    /* --------------------------------------------------------
     * MODE 0: OFF
     * --------------------------------------------------------
     * Ignore environmental logic entirely.
     * Use global defaults from EEPROM.
     */
    if (envSeasonMode == 0 || !envSensorOK) {
        envActiveSeason       = ENV_SEASON_NONE;
        envActiveSetpointF    = exhaustSetpoint;
        envActiveClampPercent = clampMaxPercent;
        envActiveRampProfile  = globalRampProfile;
        envFanBiasPercent     = 0;
        return;
    }

    /* --------------------------------------------------------
     * MODE 1: USER
     * --------------------------------------------------------
     * Operator chooses season manually (future expansion).
     * For now, USER behaves like OFF but is reserved for UI.
     */
    if (envSeasonMode == 1) {
        envActiveSeason       = ENV_SEASON_NONE;
        envActiveSetpointF    = exhaustSetpoint;
        envActiveClampPercent = clampMaxPercent;
        envActiveRampProfile  = globalRampProfile;
        envFanBiasPercent     = 0;
        return;
    }

    /* --------------------------------------------------------
     * MODE 2: AUTO
     * --------------------------------------------------------
     * Automatically choose season based on outdoor temperature.
     */
    if (envSeasonMode == 2 && envAutoSeasonEnabled && envSensorOK) {

        EnvSeason s = env_pickSeason(envTempF);

        envActiveSeason       = s;
        envActiveSetpointF    = env_getSeasonSetpoint(s);

        // Clamp + ramp profile currently mirror global defaults
        envActiveClampPercent = clampMaxPercent;
        envActiveRampProfile  = globalRampProfile;

        // Fan bias placeholder (future)
        envFanBiasPercent     = 0;
        return;
    }

    /* --------------------------------------------------------
     * FALLBACK
     * --------------------------------------------------------
     * Treat anything unexpected as OFF behavior.
     */
    envActiveSeason       = ENV_SEASON_NONE;
    envActiveSetpointF    = exhaustSetpoint;
    envActiveClampPercent = clampMaxPercent;
    envActiveRampProfile  = globalRampProfile;
    envFanBiasPercent     = 0;
}

