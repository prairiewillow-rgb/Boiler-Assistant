/*
 * ============================================================
 *  Boiler Assistant – Shared System Data (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: SystemData.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized live data model for all subsystems:
 *        • BurnEngine (state machine + timers)
 *        • Sensors (MAX31855, DS18B20, BME280)
 *        • FanControl (raw + final output)
 *        • UI (redraw flags, operator feedback)
 *        • WiFi / MQTT / Home Assistant
 *
 *      This struct is the single source of truth for the
 *      entire firmware. All modules read/write through this
 *      shared instance to guarantee perfect synchronization
 *      between UI, telemetry, automation, and remote control.
 *
 *      Notes:
 *        - No logic lives here; this is a pure data container.
 *        - EEPROMStorage handles persistence of user settings.
 *        - SystemData.cpp defines the global instance `sys`.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#ifndef SYSTEM_DATA_H
#define SYSTEM_DATA_H

#include <Arduino.h>
#include "SystemState.h"

struct SystemData {

    /* ========================================================
     *  Exhaust Temperature
     * ======================================================== */
    double exhaustRawF     = NAN;
    double exhaustSmoothF  = NAN;

    /* ========================================================
     *  Water Temperature Probes
     * ======================================================== */
    uint8_t waterProbeCount = 0;
    float   waterTempF[4]   = { NAN, NAN, NAN, NAN };

    /* ========================================================
     *  Fan Control
     * ======================================================== */
    int fanRaw   = 0;
    int fanFinal = 0;

    /* ========================================================
     *  Burn State + Timers
     * ======================================================== */
    BurnState burnState = BURN_IDLE;

    bool          boostActive        = false;
    unsigned long boostStartMs       = 0;

    bool          holdTimerActive    = false;
    unsigned long holdStartMs        = 0;

    bool          rampTimerActive    = false;
    unsigned long rampStartMs        = 0;

    // Ember Guardian (replaces coal‑bed)
    bool          emberGuardianActive = false;
    unsigned long emberGuardianStartMs = 0;

    /* ========================================================
     *  Environmental (BME280)
     * ======================================================== */
    float envTempF    = NAN;
    float envHumidity = NAN;
    float envPressure = NAN;
    bool  envSensorOK = false;

    /* ========================================================
     *  User Settings (EEPROM-backed)
     * ======================================================== */
    int16_t exhaustSetpoint       = 350;
    int16_t deadbandF             = 25;
    int16_t boostTimeSeconds      = 30;

    int16_t clampMinPercent       = 10;
    int16_t clampMaxPercent       = 100;
    int16_t deadzoneFanMode       = 0;

    // Ember Guardian (replaces coal‑bed)
    int16_t emberGuardianTimerMinutes = 30;

    int16_t flueLowThreshold      = 250;
    int16_t flueRecoveryThreshold = 300;

        /* ========================================================
     *  v2.3 Environmental Logic – ACTIVE RUNTIME STATE
     * ======================================================== */

    EnvSeason envActiveSeason      = ENV_SEASON_NONE;
    int16_t   envActiveSetpointF   = 0;
    uint8_t   envActiveClampPercent = 100;
    uint8_t   envActiveRampProfile  = 1;
    int8_t    envFanBiasPercent     = 0;

    // Internal lockout tracking (AUTO mode)
    EnvSeason     envLastSeason        = ENV_SEASON_NONE;
    unsigned long envLastSeasonChangeMs = 0;

    // Season mode: 0=OFF, 1=USER, 2=AUTO
    uint8_t  envSeasonMode        = 0;

    // Auto-season enabled flag
    bool     envAutoSeasonEnabled = true;

    // Lockout duration in seconds (1–24 hours)
    uint32_t envModeLockoutSec    = 6UL * 3600UL;

    // Seasonal start temperatures
    int16_t envSummerStartF       = 70;
    int16_t envSpringFallStartF   = 40;
    int16_t envWinterStartF       = 10;
    int16_t envExtremeStartF      = -20;

    // Seasonal hysteresis (buffers)
    int16_t envHystSummerF        = 5;
    int16_t envHystSpringFallF    = 5;
    int16_t envHystWinterF        = 5;
    int16_t envHystExtremeF       = 5;

    // Seasonal exhaust setpoints
    int16_t envSetpointSummerF      = 450;
    int16_t envSetpointSpringFallF  = 500;
    int16_t envSetpointWinterF      = 550;
    int16_t envSetpointExtremeF     = 600;

    /* ========================================================
     *  UI + Remote Sync Flags
     * ======================================================== */
    bool uiNeedsRefresh = false;
    bool remoteChanged  = false;

    /* ========================================================
     *  WiFi / MQTT runtime state
     * ======================================================== */
    bool    wifiOK   = false;   // Set in main.cpp
    bool    mqttOK   = false;   // Set in MQTT_Client.cpp
    int16_t wifiRSSI = 0;       // Updated by MQTT or WiFiAPI

    /* ========================================================
     *  System Metadata
     * ======================================================== */
    unsigned long uptimeMs = 0;     // For HA uptime sensor
    const char* firmwareVersion = "2.3‑Environmental";
};

// Global instance
extern SystemData sys;

#endif
