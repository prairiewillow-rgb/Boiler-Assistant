/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage API (v2.2)
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the EEPROM storage subsystem.
 *      Provides safe, validated, versioned access to all
 *      persistent system parameters, including:
 *          • Exhaust setpoint
 *          • BOOST time
 *          • Deadband + clamp limits
 *          • Flue thresholds
 *          • Ember Guard timing
 *          • DS18B20 probe role mapping (8 bytes)
 *
 *      All EEPROM operations are:
 *          - Versioned (auto‑repair on mismatch)
 *          - Range‑validated before saving
 *          - 16‑bit aligned for int16_t values
 *          - Backward compatible with earlier layouts
 *
 *  v2.2 Additions:
 *      - Standardized v2.2 header format (no codename)
 *      - Updated terminology for Ember Guard timing
 *      - Clarified API responsibilities and layout rules
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>
#include "SystemState.h"   // Provides PROBE_ROLE_COUNT / MAX_WATER_PROBES

/* ============================================================
 *  INITIALIZATION + SAVE ALL
 * ============================================================ */

void eeprom_init();
void eeprom_saveAll();

/* ============================================================
 *  INDIVIDUAL SAVE FUNCTIONS
 * ============================================================ */

void eeprom_saveSetpoint(int v);
void eeprom_saveBoostTime(int v);
void eeprom_saveDeadband(int v);

void eeprom_saveClampMin(int v);
void eeprom_saveClampMax(int v);
void eeprom_saveDeadzone(int v);

void eeprom_saveFlueLow(int v);
void eeprom_saveFlueRecovery(int v);

void eeprom_saveEmberGuardMinutes(int v);

/* ============================================================
 *  DS18B20 PROBE ROLE MAPPING
 * ============================================================ */

// Save all 8 probe roles
void eeprom_saveProbeRoles();

// Load all persistent values (including probe roles)
void eeprom_loadAll();

// Load only probe roles
void eeprom_loadProbeRoles();

#endif
