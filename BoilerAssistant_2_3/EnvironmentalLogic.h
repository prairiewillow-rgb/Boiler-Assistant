/*
 * ============================================================
 *  Boiler Assistant – Environmental Logic API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: EnvironmentalLogic.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the Environmental Logic subsystem.
 *
 *      This module determines the *active* seasonal configuration
 *      used by the Burn Engine, based on:
 *        • Outdoor temperature (envTempF)
 *        • Seasonal thresholds (Summer / Spring-Fall / Winter / Extreme)
 *        • Seasonal hysteresis bands
 *        • Operator-selected mode (OFF / USER / AUTO)
 *        • Lockout timer to prevent rapid season switching
 *
 *      The output of this module is a unified set of parameters:
 *        • envActiveSetpointF
 *        • envActiveClampPercent
 *        • envActiveRampProfile
 *        • envFanBiasPercent
 *
 *      These values are consumed by BurnEngine.cpp every cycle.
 *
 *  v2.3‑Environmental Notes:
 *      - Seasonal logic is now fully EEPROM-backed.
 *      - AUTO mode uses hysteresis + lockout to avoid oscillation.
 *      - USER mode forces a specific season regardless of temperature.
 *      - OFF mode disables seasonal overrides entirely.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#pragma once
#include <Arduino.h>
#include "SystemState.h"

/* ============================================================
 *  ACTIVE ENVIRONMENT STATE (runtime output to Burn Engine)
 * ============================================================ */

extern EnvSeason envActiveSeason;        // Current season (SUMMER/SPF/WINTER/EXTREME)
extern int16_t   envActiveSetpointF;     // Active exhaust setpoint
extern uint8_t   envActiveClampPercent;  // Active clamp limit
extern uint8_t   envActiveRampProfile;   // Active ramp profile
extern int8_t    envFanBiasPercent;      // Fan bias adjustment

/* ============================================================
 *  CONFIGURATION (persisted in EEPROM)
 * ============================================================ */

extern uint8_t  envSeasonMode;           // 0=OFF, 1=USER, 2=AUTO
extern bool     envAutoSeasonEnabled;    // AUTO mode enable flag
extern uint32_t envModeLockoutSec;       // Lockout to prevent rapid switching

// Environmental sensor readings
extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;

/* ============================================================
 *  SEASONAL CONFIGURATION (persisted in EEPROM)
 * ============================================================ */

// Temperature thresholds for season selection
extern int16_t envSummerStartF;
extern int16_t envSpringFallStartF;
extern int16_t envWinterStartF;
extern int16_t envExtremeStartF;

// Hysteresis bands for stable season switching
extern int16_t envHystSummerF;
extern int16_t envHystSpringFallF;
extern int16_t envHystWinterF;
extern int16_t envHystExtremeF;

// Seasonal exhaust setpoints
extern int16_t envSetpointSummerF;
extern int16_t envSetpointSpringFallF;
extern int16_t envSetpointWinterF;
extern int16_t envSetpointExtremeF;

/* ============================================================
 *  GLOBAL DEFAULTS (used when ENV_MODE_OFF)
 * ============================================================ */

extern int16_t exhaustSetpoint;          // Default exhaust setpoint
extern int16_t clampMinPercent;          // Default clamp min
extern int16_t clampMaxPercent;          // Default clamp max
extern uint8_t globalRampProfile;        // Default ramp profile

/* ============================================================
 *  API
 * ============================================================ */

/**
 * Initialize environmental logic.
 * Loads sensor state and prepares seasonal runtime variables.
 */
void env_logic_init();

/**
 * Update environmental logic.
 * Called every loop() to compute the active seasonal parameters.
 *
 * @param nowMs  Current timestamp in milliseconds.
 */
void env_logic_update(unsigned long nowMs);
