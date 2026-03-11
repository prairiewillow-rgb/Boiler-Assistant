/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the EEPROM storage subsystem.
 *
 *      This module provides:
 *        • Initialization and magic‑based first‑boot detection
 *        • Loading all operator‑editable settings from EEPROM
 *        • Saving individual parameters without rewriting blocks
 *        • Full persistence for EnvironmentalLogic v2.3
 *        • Ember Guardian v2.3 timer persistence
 *        • Water probe role mapping
 *
 *      EEPROM layout is fixed and deterministic to ensure
 *      backward compatibility across firmware versions.
 *
 *  v2.3‑Environmental Notes:
 *      - Seasonal thresholds, hysteresis, and setpoints are now
 *        fully EEPROM‑backed.
 *      - Ember Guardian uses unified naming and storage.
 *      - Probe role map is stored as a contiguous byte block.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#pragma once
#include <Arduino.h>
#include "SystemState.h"

/* ============================================================
 *  GLOBALS (defined in EEPROMStorage.cpp or .ino)
 * ============================================================ */

// Active environment state (runtime)
extern EnvSeason envActiveSeason;
extern int16_t   envActiveSetpointF;
extern uint8_t   envActiveClampPercent;
extern uint8_t   envActiveRampProfile;
extern int8_t    envFanBiasPercent;

// User settings (EEPROM-backed)
extern int16_t exhaustSetpoint;
extern int16_t deadbandF;
extern int16_t boostTimeSeconds;

extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

// Ember Guardian
extern int16_t emberGuardianTimerMinutes;

// Flue thresholds
extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

// Environmental mode
extern uint8_t  envSeasonMode;          // 0=OFF,1=USER,2=AUTO
extern bool     envAutoSeasonEnabled;
extern uint32_t envModeLockoutSec;

// Seasonal thresholds
extern int16_t envSummerStartF;
extern int16_t envSpringFallStartF;
extern int16_t envWinterStartF;
extern int16_t envExtremeStartF;

// Seasonal hysteresis
extern int16_t envHystSummerF;
extern int16_t envHystSpringFallF;
extern int16_t envHystWinterF;
extern int16_t envHystExtremeF;

// Seasonal setpoints
extern int16_t envSetpointSummerF;
extern int16_t envSetpointSpringFallF;
extern int16_t envSetpointWinterF;
extern int16_t envSetpointExtremeF;

/* ============================================================
 *  EEPROM API
 * ============================================================ */

void eeprom_init();
void eeprom_saveAll();

/* Core settings */
void eeprom_saveSetpoint(int v);
void eeprom_saveBoostTime(int v);
void eeprom_saveDeadband(int v);
void eeprom_saveDeadzone(int v);
void eeprom_saveClampMin(int v);
void eeprom_saveClampMax(int v);

/* Ember Guardian */
void eeprom_saveEmberGuardMinutes(int v);

/* Flue thresholds */
void eeprom_saveFlueLow(int v);
void eeprom_saveFlueRecovery(int v);

/* Probe roles */
void eeprom_saveProbeRoles();

/* Environmental mode + seasonal config */
void eeprom_saveEnvSeasonMode(uint8_t mode);
void eeprom_saveEnvAutoSeason(bool en);
void eeprom_saveEnvLockoutHours(uint8_t hr);

void eeprom_saveEnvSeasonStarts();
void eeprom_saveEnvSeasonHyst();
void eeprom_saveEnvSeasonSetpoints();
