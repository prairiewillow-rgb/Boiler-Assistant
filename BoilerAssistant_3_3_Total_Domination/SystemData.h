/*
 * Boiler Assistant - SystemData
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef SYSTEMDATA_H
#define SYSTEMDATA_H

#include <Arduino.h>
#include "SystemState.h"


/* ============================================================
 *  SYSTEM DATA STRUCTURE
 * ============================================================ */
struct SystemData
{
    /* ------------------------------
     *  WATER PROBE DATA
     * ------------------------------ */
    uint8_t waterProbeCount;
    float   waterTempF[MAX_WATER_PROBES];
    unsigned long waterTempLastGoodMs[MAX_WATER_PROBES];
    uint8_t probeRoleMap[PROBE_ROLE_COUNT];
    char    waterProbeNames[MAX_WATER_PROBES][PROBE_NAME_LENGTH];

    /* ------------------------------
     *  EXHAUST SENSOR
     * ------------------------------ */
    bool  exhaustSensorOK;
    unsigned long exhaustLastGoodMs;
    float exhaustSmoothF;
    float exhaustRawF;        // raw flue temp for Guardian
    int   exhaustSetpoint;
    bool  exhaustFallbackActive;

    /* ------------------------------
     *  FAN CONTROL
     * ------------------------------ */
    int clampMinPercent;
    int clampMaxPercent;
    int deadbandF;
    uint8_t deadzoneFanMode;  // 0 = fan allowed OFF, 1 = fan always ON
    int fanDemand;

    /* ------------------------------
     *  BOOST
     * ------------------------------ */
    bool          boostActive;
    unsigned long boostStartMs;
    int           boostTimeSeconds;
    uint16_t      envBoostTimeSeconds[4];
    uint8_t       boostFanPercent;
    uint8_t       envBoostFanPercent[4];
    uint8_t       activeBoostFanPercent;
    uint16_t      damperDelaySeconds;
    uint16_t      envDamperDelaySeconds[4];
    bool          damperDelayActive;

    /* ------------------------------
     *  SAFETY
     * ------------------------------ */
    SafetyState safetyState;
    uint8_t sensorFaultMask;

    /* ------------------------------
     *  BURN ENGINE
     * ------------------------------ */
    BurnState    burnState;
    bool         rampTimerActive;
    unsigned long rampStartMs;
    bool         holdTimerActive;
    unsigned long holdStartMs;

    /* ------------------------------
     *  TANK SETPOINTS (GLOBAL)
     * ------------------------------ */
    int16_t tankLowSetpointF;
    int16_t tankHighSetpointF;

    /* ------------------------------
     *  CONTROL MODE
     * ------------------------------ */
    RunMode controlMode;

    /* ------------------------------
     *  ENVIRONMENTAL SENSOR
     * ------------------------------ */
    bool  envSensorOK;
    float envTempF;
    float envHumidity;
    float envPressure;
    uint8_t envUnitsMetric;   // outdoor temp/pressure display only: 0 = F/inHg, 1 = C/kPa

    /* ------------------------------
     *  ENVIRONMENTAL SEASONAL LOGIC
     * ------------------------------ */

    // Start temperatures
    int16_t envSummerStartF;
    int16_t envSpringFallStartF;
    int16_t envWinterStartF;
    int16_t envExtremeStartF;

    // Hysteresis
    int16_t envHystSummerF;
    int16_t envHystSpringFallF;
    int16_t envHystWinterF;
    int16_t envHystExtremeF;

    // Exhaust setpoints
    int16_t envSetpointSummerF;
    int16_t envSetpointSpringFallF;
    int16_t envSetpointWinterF;
    int16_t envSetpointExtremeF;

    /* ============================================================
     *  FULL 6-PARAMETER SEASONAL SYSTEM
     * ============================================================ */

    // Tank High
    int16_t envTankHighSummerF;
    int16_t envTankHighSpringFallF;
    int16_t envTankHighWinterF;
    int16_t envTankHighExtremeF;

    // Tank Low
    int16_t envTankLowSummerF;
    int16_t envTankLowSpringFallF;
    int16_t envTankLowWinterF;
    int16_t envTankLowExtremeF;

    // ClampMax
    uint8_t envClampMaxSummerPercent;
    uint8_t envClampMaxSpringFallPercent;
    uint8_t envClampMaxWinterPercent;
    uint8_t envClampMaxExtremePercent;

    /* ------------------------------
     *  AUTO-SEASON MODE
     * ------------------------------ */
    bool     envAutoSeasonEnabled;
    uint8_t  envSeasonMode;     // 0=OFF, 1=USER, 2=AUTO
    uint32_t envModeLockoutSec;

    /* ------------------------------
    *  ACTIVE ENVIRONMENT STATE (v3.3)
     * ------------------------------ */

    // Active season selection
    EnvSeason envActiveSeason;
    unsigned long envSeasonChangedMs;

    // Active exhaust control
    int16_t envActiveSetpointF;
    uint8_t envActiveClampPercent;

    // Active water control
    int16_t envActiveTankHighF;
    int16_t envActiveTankLowF;

    // Optional future use
    uint8_t envActiveRampProfile;
    int8_t  envFanBiasPercent;

    /* ------------------------------
     *  EMBER GUARDIAN
     * ------------------------------ */
    bool          emberGuardianActive;
    bool          emberGuardianLatched;
    bool          emberGuardianTimerActive;
    unsigned long emberGuardianStartMs;
    int           emberGuardianTimerMinutes;
    int16_t       flueLowThreshold;
    int16_t       flueRecoveryThreshold;

    /* ------------------------------
     *  FAN OUTPUT / TELEMETRY
     * ------------------------------ */
    int  fanFinal;
    bool remoteChanged;

    /* ------------------------------
     *  CURRENT BURN TIMING (history is stored on SD)
     * ------------------------------ */
    unsigned long burnActiveStartMs;
    unsigned long burnLastStartMs;

    /* ------------------------------
     *  UPTIME
     * ------------------------------ */
    unsigned long uptimeMs;

    /* ------------------------------
     *  NETWORK / WIFI
     * ------------------------------ */
    bool wifiOK;
    unsigned long loopLastMs;
    unsigned long loopMaxMs;
    unsigned long networkLastMs;
    unsigned long networkMaxMs;

    /* ------------------------------
     *  UI
     * ------------------------------ */
    bool uiNeedsRefresh;

    /* ------------------------------
     *  SELF-CLEANING BURN
     * ------------------------------ */
    bool selfCleanEnabled;
    bool selfCleanDue;
    bool selfCleanActive;
    bool selfCleanManualRequested;
    uint16_t selfCleanBurnCount;
    uint16_t selfCleanIntervalBurns;
    uint8_t selfCleanStartHour;
    uint8_t selfCleanEndHour;
    int16_t selfCleanUtcOffsetMinutes;
    bool selfCleanDstEnabled;

    /* ------------------------------
     *  PUSH NOTIFICATIONS (ntfy)
     * ------------------------------ */
    bool pushEnabled;
    char pushTopic[33];
};

/* ============================================================
 *  GLOBAL INSTANCE
 * ============================================================ */
extern SystemData sys;

/* ============================================================
 *  POINTER HELPERS FOR UI
 * ============================================================ */
int16_t* ui_getSeasonStartPtr(EnvSeason s);
int16_t* ui_getSeasonBufferPtr(EnvSeason s);
int16_t* ui_getSeasonSetpointPtr(EnvSeason s);

int16_t* ui_getSeasonTankHighPtr(EnvSeason s);
int16_t* ui_getSeasonTankLowPtr(EnvSeason s);
uint8_t* ui_getSeasonClampMaxPtr(EnvSeason s);

/* ============================================================
 *  CORE SYSTEMDATA API
 * ============================================================ */
void systemdata_init();

/* ============================================================
 *  EEPROM LOAD/SAVE DECLARATIONS
 * ============================================================ */
void eeprom_loadAll();
void eeprom_saveEnvSeasonStarts();
void eeprom_saveEnvSeasonHyst();
void eeprom_saveEnvSeasonSetpoints();
void eeprom_saveEnvSeasonTankValues();
void eeprom_saveEnvSeasonClampValues();

#endif
