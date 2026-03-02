/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage Module (v2.0)
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Safe, validated EEPROM storage for all persistent system
 *    parameters. Ensures:
 *      - Versioned EEPROM layout
 *      - Automatic repair of corrupted or out-of-range values
 *      - 16-bit aligned storage for all int16_t parameters
 *      - Centralized read/write helpers
 *
 *  v2.0 Notes:
 *      - Updated to support new COALBED, FLUE LOW, FLUE REC thresholds
 *      - Ensures stability with BurnEngine v2.0 parameters
 *      - Maintains backward compatibility via version byte
 *      - All values validated before writing to EEPROM
 *
 * ============================================================
 */

#include "EEPROMStorage.h"
#include <EEPROM.h>
#include <stdint.h>

// ============================================================
//  Extern globals from .ino
// ============================================================
extern int16_t exhaustSetpoint;
extern int16_t boostTimeSeconds;
extern int16_t deadbandF;

extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern int16_t coalBedTimerMinutes;
extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

// ============================================================
// SAFE EEPROM Address Map (all int16_t, 2‑byte aligned)
// ============================================================
//
//  0–1   : exhaustSetpoint
//  2–3   : boostTimeSeconds
//  4–5   : deadbandF
//  6–7   : clampMinPercent
//  8–9   : clampMaxPercent
// 10–11  : deadzoneFanMode
// 12–13  : coalBedTimerMinutes
// 14–15  : flueLowThreshold
// 16–17  : flueRecoveryThreshold
// 18     : VERSION BYTE
//
//  TOTAL: 19 bytes
// ============================================================

static const uint8_t EEPROM_VERSION = 1;

static const int ADDR_SETPOINT       = 0;
static const int ADDR_BOOST          = 2;
static const int ADDR_DEADBAND       = 4;
static const int ADDR_CLAMP_MIN      = 6;
static const int ADDR_CLAMP_MAX      = 8;
static const int ADDR_DEADZONE       = 10;
static const int ADDR_COALBED_TIMER  = 12;
static const int ADDR_FLUE_LOW       = 14;
static const int ADDR_FLUE_REC       = 16;
static const int ADDR_VERSION        = 18;

// ============================================================
// Helpers (always 16‑bit)
// ============================================================

static int16_t readInt16(int addr) {
    int16_t v = 0;
    EEPROM.get(addr, v);
    return v;
}

static void writeInt16(int addr, int16_t v) {
    EEPROM.put(addr, v);
}

static uint8_t readByte(int addr) {
    uint8_t v = 0;
    EEPROM.get(addr, v);
    return v;
}

static void writeByte(int addr, uint8_t v) {
    EEPROM.put(addr, v);
}

// ============================================================
// Validation / Repair
// ============================================================

static void validateSettings() {
    if (exhaustSetpoint < 100 || exhaustSetpoint > 900)
        exhaustSetpoint = 350;

    if (boostTimeSeconds < 0 || boostTimeSeconds > 1800)
        boostTimeSeconds = 30;

    if (deadbandF < 0 || deadbandF > 200)
        deadbandF = 25;

    if (clampMinPercent < 0 || clampMinPercent > 100)
        clampMinPercent = 10;

    if (clampMaxPercent < 0 || clampMaxPercent > 100)
        clampMaxPercent = 100;

    if (clampMinPercent > clampMaxPercent)
        clampMinPercent = clampMaxPercent;

    if (deadzoneFanMode != 0 && deadzoneFanMode != 1)
        deadzoneFanMode = 0;

    if (coalBedTimerMinutes < 0 || coalBedTimerMinutes > 720)
        coalBedTimerMinutes = 30;

    if (flueLowThreshold < 0 || flueLowThreshold > 900)
        flueLowThreshold = 250;

    if (flueRecoveryThreshold < 0 || flueRecoveryThreshold > 900)
        flueRecoveryThreshold = flueLowThreshold + 50;

    if (flueRecoveryThreshold < flueLowThreshold + 10)
        flueRecoveryThreshold = flueLowThreshold + 50;
}

// ============================================================
// Initialization
// ============================================================

void eeprom_init() {
    uint8_t ver = readByte(ADDR_VERSION);

    if (ver != EEPROM_VERSION) {
        exhaustSetpoint       = 350;
        boostTimeSeconds      = 30;
        deadbandF             = 25;
        clampMinPercent       = 10;
        clampMaxPercent       = 100;
        deadzoneFanMode       = 0;
        coalBedTimerMinutes   = 30;
        flueLowThreshold      = 250;
        flueRecoveryThreshold = 300;

        validateSettings();
        eeprom_saveAll();
        return;
    }

    exhaustSetpoint       = readInt16(ADDR_SETPOINT);
    boostTimeSeconds      = readInt16(ADDR_BOOST);
    deadbandF             = readInt16(ADDR_DEADBAND);
    clampMinPercent       = readInt16(ADDR_CLAMP_MIN);
    clampMaxPercent       = readInt16(ADDR_CLAMP_MAX);
    deadzoneFanMode       = readInt16(ADDR_DEADZONE);
    coalBedTimerMinutes   = readInt16(ADDR_COALBED_TIMER);
    flueLowThreshold      = readInt16(ADDR_FLUE_LOW);
    flueRecoveryThreshold = readInt16(ADDR_FLUE_REC);

    validateSettings();
    eeprom_saveAll();
}

// ============================================================
// Save All
// ============================================================

void eeprom_saveAll() {
    validateSettings();

    writeInt16(ADDR_SETPOINT,      exhaustSetpoint);
    writeInt16(ADDR_BOOST,         boostTimeSeconds);
    writeInt16(ADDR_DEADBAND,      deadbandF);
    writeInt16(ADDR_CLAMP_MIN,     clampMinPercent);
    writeInt16(ADDR_CLAMP_MAX,     clampMaxPercent);
    writeInt16(ADDR_DEADZONE,      deadzoneFanMode);
    writeInt16(ADDR_COALBED_TIMER, coalBedTimerMinutes);
    writeInt16(ADDR_FLUE_LOW,      flueLowThreshold);
    writeInt16(ADDR_FLUE_REC,      flueRecoveryThreshold);

    writeByte(ADDR_VERSION, EEPROM_VERSION);
}

// ============================================================
// Individual Saves
// ============================================================

void eeprom_saveSetpoint(int v) {
    exhaustSetpoint = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_SETPOINT, exhaustSetpoint);
}

void eeprom_saveBoostTime(int v) {
    boostTimeSeconds = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_BOOST, boostTimeSeconds);
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
    writeInt16(ADDR_CLAMP_MAX, clampMaxPercent);
}

void eeprom_saveClampMax(int v) {
    clampMaxPercent = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_CLAMP_MIN, clampMinPercent);
    writeInt16(ADDR_CLAMP_MAX, clampMaxPercent);
}

void eeprom_saveDeadzone(int v) {
    deadzoneFanMode = (int16_t)(v ? 1 : 0);
    validateSettings();
    writeInt16(ADDR_DEADZONE, deadzoneFanMode);
}

void eeprom_saveCoalBedTimer(int v) {
    coalBedTimerMinutes = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_COALBED_TIMER, coalBedTimerMinutes);
}

void eeprom_saveFlueLow(int v) {
    flueLowThreshold = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_FLUE_LOW, flueLowThreshold);
    writeInt16(ADDR_FLUE_REC, flueRecoveryThreshold);
}

void eeprom_saveFlueRecovery(int v) {
    flueRecoveryThreshold = (int16_t)v;
    validateSettings();
    writeInt16(ADDR_FLUE_LOW, flueLowThreshold);
    writeInt16(ADDR_FLUE_REC, flueRecoveryThreshold);
}
