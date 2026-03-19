/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage Module (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    EEPROM-backed configuration storage for the Boiler Assistant
 *    controller. This module owns all persistent settings for:
 *
 *      • Combustion parameters (setpoint, deadband, clamps)
 *      • Ember Guardian thresholds and timer
 *      • Environmental logic (season starts, hysteresis, setpoints)
 *      • Boiler control (tank low/high, run mode)
 *      • Probe role mapping
 *      • Runtime WiFi credentials
 *
 *    Implements deterministic read/write helpers for multibyte
 *    values and enforces strict safety clamps to prevent invalid
 *    EEPROM data from destabilizing the burn engine.
 *
 *  Architectural Notes:
 *      - SystemData is the single source of truth for all fields.
 *      - EEPROM layout is fixed and version-stable.
 *      - All multibyte values use explicit little-endian encoding.
 *      - This module contains no UI or control logic.
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include "EEPROMStorage.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include <EEPROM.h>

extern SystemData sys;
extern RuntimeCredentials runtimeCreds;

/* ============================================================
 *  INTERNAL HELPERS FOR MULTIBYTE VALUES
 * ============================================================ */

static void eeprom_write16(int addr, int16_t value) {
    EEPROM.write(addr,     (uint8_t)(value & 0xFF));
    EEPROM.write(addr + 1, (uint8_t)((value >> 8) & 0xFF));
}

static int16_t eeprom_read16(int addr) {
    uint8_t lo = EEPROM.read(addr);
    uint8_t hi = EEPROM.read(addr + 1);
    return (int16_t)((hi << 8) | lo);
}

/* ============================================================
 *  INIT — LOAD ALL SETTINGS YOU SAVE
 * ============================================================ */

void eeprom_init() {

    // === COMBUSTION SETTINGS ===
    sys.exhaustSetpoint      = eeprom_read16(0);
    sys.boostTimeSeconds     = eeprom_read16(2);
    sys.deadbandF            = eeprom_read16(4);
    sys.clampMinPercent      = eeprom_read16(6);
    sys.clampMaxPercent      = eeprom_read16(8);
    sys.deadzoneFanMode      = EEPROM.read(10);

    // === EMBER GUARDIAN ===
    sys.emberGuardianTimerMinutes = eeprom_read16(12);
    sys.flueLowThreshold          = eeprom_read16(14);
    sys.flueRecoveryThreshold     = eeprom_read16(16);

    // === ENVIRONMENTAL LOGIC (ONLY FIELDS YOU ACTUALLY SAVE) ===
    sys.envSummerStartF      = eeprom_read16(22);
    sys.envSpringFallStartF  = eeprom_read16(24);
    sys.envWinterStartF      = eeprom_read16(26);
    sys.envExtremeStartF     = eeprom_read16(28);

    sys.envHystSummerF       = eeprom_read16(30);
    sys.envHystSpringFallF   = eeprom_read16(32);
    sys.envHystWinterF       = eeprom_read16(34);
    sys.envHystExtremeF      = eeprom_read16(36);

    sys.envSetpointSummerF     = eeprom_read16(38);
    sys.envSetpointSpringFallF = eeprom_read16(40);
    sys.envSetpointWinterF     = eeprom_read16(42);
    sys.envSetpointExtremeF    = eeprom_read16(44);

    // === BOILER CONTROL ===
    sys.tankLowSetpointF     = eeprom_read16(46);
    sys.tankHighSetpointF    = eeprom_read16(48);
    sys.controlMode          = (RunMode)EEPROM.read(50);

    // === PROBE ROLES ===
    // Default: Tank probe = physical probe 0
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        sys.probeRoleMap[i] = 0;   // everything maps to probe 0 by default
    }
    sys.probeRoleMap[PROBE_TANK] = 0;   // Tank = probe 0



    // === RUNTIME CREDENTIALS ===
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        ((uint8_t*)&runtimeCreds)[i] = EEPROM.read(100 + i);
    }

    /* ========================================================
     *  SAFETY CLAMPS — PREVENT INVALID EEPROM VALUES
     * ======================================================== */

    // BOOST TIME — critical for Guardian → BOOST behavior
    if (sys.boostTimeSeconds < 5 || sys.boostTimeSeconds > 600) {
        sys.boostTimeSeconds = 30;   // safe default
    }

    // Exhaust setpoint sanity
    if (sys.exhaustSetpoint < 200 || sys.exhaustSetpoint > 900) {
        sys.exhaustSetpoint = 400;
    }

    // Deadband sanity
    if (sys.deadbandF < 1 || sys.deadbandF > 100) {
        sys.deadbandF = 10;
    }

    // Fan clamp sanity
    if (sys.clampMinPercent < 0 || sys.clampMinPercent > 100) {
        sys.clampMinPercent = 10;
    }
    if (sys.clampMaxPercent < 0 || sys.clampMaxPercent > 100) {
        sys.clampMaxPercent = 90;
    }

    // Guardian thresholds sanity
    if (sys.emberGuardianTimerMinutes < 1 || sys.emberGuardianTimerMinutes > 120) {
        sys.emberGuardianTimerMinutes = 10;
    }
    if (sys.flueLowThreshold < 50 || sys.flueLowThreshold > 500) {
        sys.flueLowThreshold = 120;
    }
    if (sys.flueRecoveryThreshold < 50 || sys.flueRecoveryThreshold > 500) {
        sys.flueRecoveryThreshold = 180;
    }
}

/* ============================================================
 *  CORE COMBUSTION SAVES
 * ============================================================ */

void eeprom_saveSetpoint(int v) {
    eeprom_write16(0, (int16_t)v);
}

void eeprom_saveBoostTime(int v) {
    eeprom_write16(2, (int16_t)v);
}

void eeprom_saveDeadband(int v) {
    eeprom_write16(4, (int16_t)v);
}

void eeprom_saveClampMin(int v) {
    eeprom_write16(6, (int16_t)v);
}

void eeprom_saveClampMax(int v) {
    eeprom_write16(8, (int16_t)v);
}

void eeprom_saveDeadzone(int v) {
    EEPROM.write(10, (uint8_t)v);
}

/* ============================================================
 *  EMBER GUARDIAN SAVES
 * ============================================================ */

void eeprom_saveEmberGuardianMinutes(int v) {
    eeprom_write16(12, (int16_t)v);
}

void eeprom_saveFlueLow(int v) {
    eeprom_write16(14, (int16_t)v);
}

void eeprom_saveFlueRecovery(int v) {
    eeprom_write16(16, (int16_t)v);
}

/* ============================================================
 *  PROBE ROLES
 * ============================================================ */

void eeprom_saveProbeRoles() {
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        EEPROM.write(60 + i, sys.probeRoleMap[i]);
    }
}

/* ============================================================
 *  ENVIRONMENTAL LOGIC SAVES
 * ============================================================ */

void eeprom_saveEnvSeasonMode(uint8_t mode) {
    EEPROM.write(18, mode);
}

void eeprom_saveEnvAutoSeason(bool en) {
    EEPROM.write(19, en ? 1 : 0);
}

void eeprom_saveEnvLockoutHours(uint8_t hours) {
    EEPROM.write(20, hours);
}

void eeprom_saveEnvSeasonStarts() {
    eeprom_write16(22, sys.envSummerStartF);
    eeprom_write16(24, sys.envSpringFallStartF);
    eeprom_write16(26, sys.envWinterStartF);
    eeprom_write16(28, sys.envExtremeStartF);
}

void eeprom_saveEnvSeasonHyst() {
    eeprom_write16(30, sys.envHystSummerF);
    eeprom_write16(32, sys.envHystSpringFallF);
    eeprom_write16(34, sys.envHystWinterF);
    eeprom_write16(36, sys.envHystExtremeF);
}

void eeprom_saveEnvSeasonSetpoints() {
    eeprom_write16(38, sys.envSetpointSummerF);
    eeprom_write16(40, sys.envSetpointSpringFallF);
    eeprom_write16(42, sys.envSetpointWinterF);
    eeprom_write16(44, sys.envSetpointExtremeF);
}

/* ============================================================
 *  BOILER CONTROL SAVES
 * ============================================================ */

void eeprom_saveTankLow(int v) {
    eeprom_write16(46, (int16_t)v);
}

void eeprom_saveTankHigh(int v) {
    eeprom_write16(48, (int16_t)v);
}

void eeprom_saveRunMode(uint8_t mode) {
    EEPROM.write(50, mode);
}

/* ============================================================
 *  RUNTIME CREDENTIALS
 * ============================================================ */

void eeprom_saveRuntimeCreds() {
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        EEPROM.write(100 + i, ((uint8_t*)&runtimeCreds)[i]);
    }
}

