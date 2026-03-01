/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage Module (v2.0)
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the EEPROM storage subsystem.
 *    Provides:
 *      - Initialization with version checking and auto‑repair
 *      - Full save of all parameters
 *      - Individual save functions for UI-driven updates
 *
 *  v2.0 Notes:
 *      - Supports COALBED, FLUE LOW, FLUE REC thresholds
 *      - Ensures compatibility with BurnEngine v2.0 parameters
 *      - All writes validated before committing to EEPROM
 *
 * ============================================================
 */

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>

// ============================================================
//  Initialization
// ============================================================

// Load all parameters from EEPROM (with validation + repair)
void eeprom_init();

// Save all parameters back to EEPROM (after validation)
void eeprom_saveAll();

// ============================================================
//  Individual Save Functions
// ============================================================

void eeprom_saveSetpoint(int v);
void eeprom_saveBoostTime(int v);
void eeprom_saveDeadband(int v);

void eeprom_saveClampMin(int v);
void eeprom_saveClampMax(int v);

// Deadzone fan mode is stored as a byte internally
// (0 = hysteresis/gated, 1 = clamp/always-on)
void eeprom_saveDeadzone(int v);

// COAL BED SAVER parameters
void eeprom_saveCoalBedTimer(int v);
void eeprom_saveFlueLow(int v);
void eeprom_saveFlueRecovery(int v);

#endif
