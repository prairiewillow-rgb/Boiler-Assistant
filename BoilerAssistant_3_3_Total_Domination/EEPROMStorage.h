/*
 * Boiler Assistant - EEPROMStorage
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>
#include "NetworkSettings.h"
#include "RuntimeCredentials.h"

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
bool eeprom_loadAdaptiveLearning();
void eeprom_saveAdaptiveLearning(bool enabled);
// Learned predictive-HOLD lookahead in seconds (0..PREDICT_LOOKAHEAD_MAX_SEC).
static const uint8_t PREDICT_LOOKAHEAD_DEFAULT_SEC = 15;
static const uint8_t PREDICT_LOOKAHEAD_MAX_SEC = 30;
uint8_t eeprom_loadPredictLookahead();
void eeprom_savePredictLookahead(uint8_t seconds);

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

/* NEW - seasonal TankHigh/TankLow/ClampMax */
void eeprom_saveEnvSeasonTankValues();
void eeprom_saveEnvSeasonClampValues();
bool eeprom_saveEnvSeasonBoostTimes(const uint16_t* seconds);
bool eeprom_saveBoostFanPercent(int percent);
bool eeprom_saveEnvBoostFanPercent(const uint8_t* percent);
bool eeprom_saveDamperDelay(int seconds);
bool eeprom_saveEnvDamperDelays(const uint16_t* seconds);
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
// Probe table keyed by the probe's 8-byte serial number (ROM address).
bool eeprom_loadTankRom(uint8_t* rom);
void eeprom_saveTankRom(const uint8_t* rom);
bool eeprom_loadProbeName(const uint8_t* rom, char* name);
void eeprom_saveProbeName(const uint8_t* rom, const char* name,
                          const uint8_t (*present)[8], uint8_t presentCount);

/* ============================================================
 *  RUNTIME CREDENTIALS
 * ============================================================ */
bool eeprom_saveRuntimeCreds();
bool eeprom_saveRuntimeCreds(const RuntimeCredentials& credentials);
bool eeprom_loadNetworkSettings();
bool eeprom_saveNetworkSettings(const NetworkSettings& settings);
bool eeprom_saveProvisioning(const RuntimeCredentials& credentials,
                             const NetworkSettings& settings);

/* ============================================================
 *  PUSH NOTIFICATIONS
 * ============================================================ */
void eeprom_savePushNotify();

#endif
