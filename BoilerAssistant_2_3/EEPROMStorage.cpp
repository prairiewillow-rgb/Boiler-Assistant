/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized non‑volatile storage for all operator‑editable
 *      configuration values. This module handles:
 *
 *        • Loading defaults on first boot (magic check)
 *        • Reading/writing all user settings to EEPROM
 *        • EnvironmentalLogic v2.3 seasonal parameters
 *        • Ember Guardian v2.3 timer persistence
 *        • Water probe role mapping
 *
 *      EEPROM layout is intentionally flat and deterministic.
 *      All addresses are fixed to guarantee backward compatibility
 *      across firmware versions and to avoid fragmentation.
 *
 *  v2.3‑Environmental Notes:
 *      - Seasonal setpoints, hysteresis, and start thresholds
 *        are now fully EEPROM‑backed.
 *      - Ember Guardian v2.3 uses unified naming and storage.
 *      - Probe role map is stored as a contiguous byte block.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#include <Arduino.h>
#include <EEPROM.h>
#include "EEPROMStorage.h"
#include "SystemState.h"
#include "EnvironmentalLogic.h"

/* ============================================================
 *  EXTERNS (runtime globals from main firmware)
 * ============================================================ */

extern int16_t exhaustSetpoint;
extern int16_t boostTimeSeconds;
extern int16_t deadbandF;
extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

extern int16_t emberGuardianTimerMinutes;

extern uint8_t  envSeasonMode;
extern bool     envAutoSeasonEnabled;
extern uint32_t envModeLockoutSec;

extern int16_t envSummerStartF;
extern int16_t envSpringFallStartF;
extern int16_t envWinterStartF;
extern int16_t envExtremeStartF;

extern int16_t envHystSummerF;
extern int16_t envHystSpringFallF;
extern int16_t envHystWinterF;
extern int16_t envHystExtremeF;

extern int16_t envSetpointSummerF;
extern int16_t envSetpointSpringFallF;
extern int16_t envSetpointWinterF;
extern int16_t envSetpointExtremeF;

extern uint8_t probeRoleMap[PROBE_ROLE_COUNT];

/* ============================================================
 *  EEPROM ADDRESS MAP (fixed layout)
 * ============================================================ */

#define ADDR_MAGIC            0
#define ADDR_EXH_SETPOINT     4
#define ADDR_BOOST_TIME       6
#define ADDR_DEADBAND         8
#define ADDR_CLAMP_MIN        10
#define ADDR_CLAMP_MAX        12
#define ADDR_DEADZONE_MODE    14
#define ADDR_EMBER_GUARD      16
#define ADDR_FLUE_LOW         18
#define ADDR_FLUE_REC         20

#define ADDR_ENV_MODE         30
#define ADDR_ENV_AUTO         31
#define ADDR_ENV_LOCKOUT_HR   32

#define ADDR_ENV_SUMMER_START   40
#define ADDR_ENV_SPF_START      42
#define ADDR_ENV_WINTER_START   44
#define ADDR_ENV_EXTREME_START  46

#define ADDR_ENV_HYST_SUMMER    50
#define ADDR_ENV_HYST_SPF       52
#define ADDR_ENV_HYST_WINTER    54
#define ADDR_ENV_HYST_EXTREME   56

#define ADDR_ENV_SP_SUMMER      60
#define ADDR_ENV_SP_SPF         62
#define ADDR_ENV_SP_WINTER      64
#define ADDR_ENV_SP_EXTREME     66

#define ADDR_PROBE_MAP          80

#define EEPROM_MAGIC 0xB023

/* ============================================================
 *  LOW‑LEVEL READ/WRITE HELPERS
 * ============================================================ */

static void writeInt16(int addr, int16_t v) {
    EEPROM.put(addr, v);
}

static int16_t readInt16(int addr, int16_t def) {
    int16_t v;
    EEPROM.get(addr, v);
    if (v == -32768) return def; // crude sanity
    return v;
}

static void writeByte(int addr, uint8_t v) {
    EEPROM.update(addr, v);
}

static uint8_t readByte(int addr, uint8_t def) {
    uint8_t v = EEPROM.read(addr);
    return v == 0xFF ? def : v;
}

/* ============================================================
 *  VALIDATION (bounds enforcement)
 * ============================================================ */

void validateSettings() {
    if (exhaustSetpoint < 200) exhaustSetpoint = 200;
    if (exhaustSetpoint > 900) exhaustSetpoint = 900;

    if (boostTimeSeconds < 5)  boostTimeSeconds = 5;
    if (boostTimeSeconds > 300) boostTimeSeconds = 300;

    if (deadbandF < 1)  deadbandF = 1;
    if (deadbandF > 100) deadbandF = 100;

    if (clampMinPercent < 0)   clampMinPercent = 0;
    if (clampMinPercent > 100) clampMinPercent = 100;

    if (clampMaxPercent < 0)   clampMaxPercent = 0;
    if (clampMaxPercent > 100) clampMaxPercent = 100;

    if (emberGuardianTimerMinutes < 5)  emberGuardianTimerMinutes = 5;
    if (emberGuardianTimerMinutes > 60) emberGuardianTimerMinutes = 60;

    if (envSeasonMode > 2) envSeasonMode = 0;
}

/* ============================================================
 *  INIT (load from EEPROM or write defaults)
 * ============================================================ */

void eeprom_init() {
    EEPROM.begin();

    uint16_t magic;
    EEPROM.get(ADDR_MAGIC, magic);

    if (magic != EEPROM_MAGIC) {
        // First boot: write defaults from globals
        eeprom_saveAll();
        EEPROM.put(ADDR_MAGIC, EEPROM_MAGIC);
        return;
    }

    // Load all values
    exhaustSetpoint       = readInt16(ADDR_EXH_SETPOINT, exhaustSetpoint);
    boostTimeSeconds      = readInt16(ADDR_BOOST_TIME,   boostTimeSeconds);
    deadbandF             = readInt16(ADDR_DEADBAND,     deadbandF);
    clampMinPercent       = readInt16(ADDR_CLAMP_MIN,    clampMinPercent);
    clampMaxPercent       = readInt16(ADDR_CLAMP_MAX,    clampMaxPercent);
    deadzoneFanMode       = readByte (ADDR_DEADZONE_MODE, deadzoneFanMode);

    emberGuardianTimerMinutes = readInt16(ADDR_EMBER_GUARD, emberGuardianTimerMinutes);
    flueLowThreshold          = readInt16(ADDR_FLUE_LOW,    flueLowThreshold);
    flueRecoveryThreshold     = readInt16(ADDR_FLUE_REC,    flueRecoveryThreshold);

    envSeasonMode        = readByte (ADDR_ENV_MODE, envSeasonMode);
    envAutoSeasonEnabled = readByte (ADDR_ENV_AUTO, envAutoSeasonEnabled ? 1 : 0) ? true : false;

    {
        uint8_t hr = readByte(ADDR_ENV_LOCKOUT_HR, (uint8_t)(envModeLockoutSec / 3600UL));
        envModeLockoutSec = (uint32_t)hr * 3600UL;
    }

    envSummerStartF      = readInt16(ADDR_ENV_SUMMER_START,  envSummerStartF);
    envSpringFallStartF  = readInt16(ADDR_ENV_SPF_START,     envSpringFallStartF);
    envWinterStartF      = readInt16(ADDR_ENV_WINTER_START,  envWinterStartF);
    envExtremeStartF     = readInt16(ADDR_ENV_EXTREME_START, envExtremeStartF);

    envHystSummerF       = readInt16(ADDR_ENV_HYST_SUMMER,   envHystSummerF);
    envHystSpringFallF   = readInt16(ADDR_ENV_HYST_SPF,      envHystSpringFallF);
    envHystWinterF       = readInt16(ADDR_ENV_HYST_WINTER,   envHystWinterF);
    envHystExtremeF      = readInt16(ADDR_ENV_HYST_EXTREME,  envHystExtremeF);

    envSetpointSummerF      = readInt16(ADDR_ENV_SP_SUMMER,  envSetpointSummerF);
    envSetpointSpringFallF  = readInt16(ADDR_ENV_SP_SPF,     envSetpointSpringFallF);
    envSetpointWinterF      = readInt16(ADDR_ENV_SP_WINTER,  envSetpointWinterF);
    envSetpointExtremeF     = readInt16(ADDR_ENV_SP_EXTREME, envSetpointExtremeF);

    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        probeRoleMap[i] = readByte(ADDR_PROBE_MAP + i, probeRoleMap[i]);
    }

    validateSettings();
}

/* ============================================================
 *  SAVE ALL (bulk write)
 * ============================================================ */

void eeprom_saveAll() {
    validateSettings();

    writeInt16(ADDR_EXH_SETPOINT, exhaustSetpoint);
    writeInt16(ADDR_BOOST_TIME,   boostTimeSeconds);
    writeInt16(ADDR_DEADBAND,     deadbandF);
    writeInt16(ADDR_CLAMP_MIN,    clampMinPercent);
    writeInt16(ADDR_CLAMP_MAX,    clampMaxPercent);
    writeByte (ADDR_DEADZONE_MODE, (uint8_t)deadzoneFanMode);

    writeInt16(ADDR_EMBER_GUARD,  emberGuardianTimerMinutes);
    writeInt16(ADDR_FLUE_LOW,     flueLowThreshold);
    writeInt16(ADDR_FLUE_REC,     flueRecoveryThreshold);

    writeByte (ADDR_ENV_MODE, envSeasonMode);
    writeByte (ADDR_ENV_AUTO, envAutoSeasonEnabled ? 1 : 0);
    writeByte (ADDR_ENV_LOCKOUT_HR, (uint8_t)(envModeLockoutSec / 3600UL));

    writeInt16(ADDR_ENV_SUMMER_START,  envSummerStartF);
    writeInt16(ADDR_ENV_SPF_START,     envSpringFallStartF);
    writeInt16(ADDR_ENV_WINTER_START,  envWinterStartF);
    writeInt16(ADDR_ENV_EXTREME_START, envExtremeStartF);

    writeInt16(ADDR_ENV_HYST_SUMMER,   envHystSummerF);
    writeInt16(ADDR_ENV_HYST_SPF,      envHystSpringFallF);
    writeInt16(ADDR_ENV_HYST_WINTER,   envHystWinterF);
    writeInt16(ADDR_ENV_HYST_EXTREME,  envHystExtremeF);

    writeInt16(ADDR_ENV_SP_SUMMER,     envSetpointSummerF);
    writeInt16(ADDR_ENV_SP_SPF,        envSetpointSpringFallF);
    writeInt16(ADDR_ENV_SP_WINTER,     envSetpointWinterF);
    writeInt16(ADDR_ENV_SP_EXTREME,    envSetpointExtremeF);

    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        writeByte(ADDR_PROBE_MAP + i, probeRoleMap[i]);
    }
}

/* ============================================================
 *  INDIVIDUAL SAVE HELPERS
 * ============================================================ */

void eeprom_saveSetpoint(int v) {
    exhaustSetpoint = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_EXH_SETPOINT, exhaustSetpoint);
}

void eeprom_saveBoostTime(int v) {
    boostTimeSeconds = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_BOOST_TIME, boostTimeSeconds);
}

void eeprom_saveDeadband(int v) {
    deadbandF = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_DEADBAND, deadbandF);
}

void eeprom_saveClampMin(int v) {
    clampMinPercent = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_CLAMP_MIN, clampMinPercent);
}

void eeprom_saveClampMax(int v) {
    clampMaxPercent = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_CLAMP_MAX, clampMaxPercent);
}

void eeprom_saveDeadzone(int v) {
    deadzoneFanMode = (int16_t)v;
    writeByte(ADDR_DEADZONE_MODE, (uint8_t)deadzoneFanMode);
}

void eeprom_saveEmberGuardMinutes(int v) {
    emberGuardianTimerMinutes = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_EMBER_GUARD, emberGuardianTimerMinutes);
}

void eeprom_saveFlueLow(int v) {
    flueLowThreshold = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_FLUE_LOW, flueLowThreshold);
}

void eeprom_saveFlueRecovery(int v) {
    flueRecoveryThreshold = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_FLUE_REC, flueRecoveryThreshold);
}

void eeprom_saveProbeRoles() {
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        writeByte(ADDR_PROBE_MAP + i, probeRoleMap[i]);
    }
}

/* ============================================================
 *  ENVIRONMENTAL SAVE HELPERS
 * ============================================================ */

void eeprom_saveEnvSeasonMode(uint8_t mode) {
    envSeasonMode = mode;
    writeByte(ADDR_ENV_MODE, envSeasonMode);
}

void eeprom_saveEnvAutoSeason(bool en) {
    envAutoSeasonEnabled = en;
    writeByte(ADDR_ENV_AUTO, envAutoSeasonEnabled ? 1 : 0);
}

void eeprom_saveEnvLockoutHours(uint8_t hr) {
    envModeLockoutSec = (uint32_t)hr * 3600UL;
    writeByte(ADDR_ENV_LOCKOUT_HR, hr);
}

void eeprom_saveEnvSeasonStarts() {
    writeInt16(ADDR_ENV_SUMMER_START,  envSummerStartF);
    writeInt16(ADDR_ENV_SPF_START,     envSpringFallStartF);
    writeInt16(ADDR_ENV_WINTER_START,  envWinterStartF);
    writeInt16(ADDR_ENV_EXTREME_START, envExtremeStartF);
}

void eeprom_saveEnvSeasonHyst() {
    writeInt16(ADDR_ENV_HYST_SUMMER,   envHystSummerF);
    writeInt16(ADDR_ENV_HYST_SPF,      envHystSpringFallF);
    writeInt16(ADDR_ENV_HYST_WINTER,   envHystWinterF);
    writeInt16(ADDR_ENV_HYST_EXTREME,  envHystExtremeF);
}

void eeprom_saveEnvSeasonSetpoints() {
    writeInt16(ADDR_ENV_SP_SUMMER,     envSetpointSummerF);
    writeInt16(ADDR_ENV_SP_SPF,        envSetpointSpringFallF);
    writeInt16(ADDR_ENV_SP_WINTER,     envSetpointWinterF);
    writeInt16(ADDR_ENV_SP_EXTREME,    envSetpointExtremeF);
}
