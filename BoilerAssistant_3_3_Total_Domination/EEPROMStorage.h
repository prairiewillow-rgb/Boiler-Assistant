/*
 * ============================================================
 *  Boiler Assistant â€“ EEPROM Storage API (v3.3.4 "Total Domination")
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for all EEPROM-backed configuration storage.
 *    This module exposes deterministic save/load entry points for:
 *
 *      â€¢ Combustion settings (setpoint, deadband, clamps)
 *      â€¢ Ember Guardian thresholds and timer
 *      â€¢ Environmental logic (season starts, hysteresis, setpoints)
 *      â€¢ Boiler control (tank low/high, run mode)
 *      â€¢ Probe role mapping
 *      â€¢ Runtime WiFi credentials
 *
 *    All persistent values follow the Total Domination Architecture:
 *      - SystemData is the single source of truth
 *      - EEPROM layout is fixed and version-stable
 *      - Multibyte values use explicit little-endian encoding
 *
 *  Architectural Notes:
 *      - This header exposes only the public API; implementation
 *        resides in EEPROMStorage.cpp.
 *      - No UI or control logic belongs here.
 *
 *  Version:
 *      Boiler Assistant v3.3.4 "Total Domination"
 * ============================================================
 */

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>

/* ============================================================
 *  INIT
 * ============================================================ */
void eeprom_init();

/* ============================================================
 *  COMBUSTION SETTINGS
 * ============================================================ */
void eeprom_saveSetpoint(int v);
void eeprom_saveBoostTime(int v);
void eeprom_saveDeadband(int v);
void eeprom_saveClampMin(int v);
void eeprom_saveClampMax(int v);
void eeprom_saveDeadzone(int v);
float eeprom_loadAdaptiveSlope();
void eeprom_saveAdaptiveSlope(float value);

/* ============================================================
 *  EMBER GUARDIAN
 * ============================================================ */
void eeprom_saveEmberGuardianMinutes(int v);
void eeprom_saveFlueLow(int v);
void eeprom_saveFlueRecovery(int v);

/* ============================================================
 *  ENVIRONMENTAL LOGIC
 * ============================================================ */
void eeprom_saveEnvSeasonMode(uint8_t mode);
void eeprom_saveEnvAutoSeason(bool en);
void eeprom_saveEnvLockoutHours(uint8_t hours);
void eeprom_saveEnvUnits(uint8_t metric);

void eeprom_saveEnvSeasonStarts();
void eeprom_saveEnvSeasonHyst();
void eeprom_saveEnvSeasonSetpoints();

/* NEW â€” seasonal TankHigh/TankLow/ClampMax */
void eeprom_saveEnvSeasonTankValues();
void eeprom_saveEnvSeasonClampValues();
void eeprom_saveSelfClean();

/* ============================================================
 *  BOILER CONTROL
 * ============================================================ */
void eeprom_saveTankLow(int v);
void eeprom_saveTankHigh(int v);
void eeprom_saveRunMode(uint8_t mode);

/* ============================================================
 *  PROBE ROLES
 * ============================================================ */
void eeprom_saveProbeRoles();
void eeprom_loadProbeNames();
void eeprom_saveProbeName(uint8_t index);

/* ============================================================
 *  RUNTIME CREDENTIALS
 * ============================================================ */
void eeprom_saveRuntimeCreds();

/* ============================================================
 *  PUSH NOTIFICATIONS
 * ============================================================ */
void eeprom_savePushNotify();

#endif

