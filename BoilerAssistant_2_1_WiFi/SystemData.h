/*
 * ============================================================
 *  Boiler Assistant – Shared System Data (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: SystemData.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized live data model for all subsystems:
 *        • BurnEngine (state machine + timers)
 *        • Sensors (MAX31855, DS18B20, BME280)
 *        • FanControl (raw + final output)
 *        • UI (redraw flags, operator feedback)
 *        • WiFi / LoRa / MQTT / Home Assistant
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
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef SYSTEM_DATA_H
#define SYSTEM_DATA_H

#include <Arduino.h>
#include "SystemState.h"

struct SystemData {

    /* ========================================================
     *  Exhaust Temperature (MAX31855)
     * ======================================================== */
    double exhaustRawF     = NAN;   
    double exhaustSmoothF  = NAN;   

    /* ========================================================
     *  Water Temperature Probes (DS18B20)
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
    BurnState burnState = BURN_RAMP;

    bool          boostActive      = false;
    unsigned long boostStartMs     = 0;

    bool          holdTimerActive  = false;
    unsigned long holdStartMs      = 0;
    unsigned long holdStabilityMs  = 5000;

    bool          rampTimerActive  = false;
    unsigned long rampStartMs      = 0;
    unsigned long rampStabilityMs  = 3000;

    bool          coalbedTimerActive = false;
    unsigned long coalbedStartMs     = 0;

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

    int16_t coalBedTimerMinutes   = 30;
    int16_t flueLowThreshold      = 250;
    int16_t flueRecoveryThreshold = 300;

    /* ========================================================
     *  Flags for UI redraw or remote sync
     * ======================================================== */
    bool uiNeedsRefresh = false;    
    bool remoteChanged  = false;    

    /* ========================================================
     *  WiFi / MQTT runtime state
     * ======================================================== */
    bool wifiOK = false;            
};

// Global instance
extern SystemData sys;

#endif

