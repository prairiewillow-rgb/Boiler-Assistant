/*
 * ============================================================
 *  Boiler Assistant – Shared System Data (v2.0)
 *  ------------------------------------------------------------
 *  File: SystemData.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized live data model for all subsystems:
 *        • BurnEngine
 *        • Sensors
 *        • FanControl
 *        • UI
 *        • WiFi / LoRa / MQTT / Home Assistant
 *
 *      All modules read/write through this struct to ensure
 *      perfect synchronization between UI, telemetry, and
 *      remote control interfaces.
 *
 * ============================================================
 */

#ifndef SYSTEM_DATA_H
#define SYSTEM_DATA_H

#include <Arduino.h>
#include "SystemState.h"

struct SystemData {

    // ========================================================
    //  Exhaust Temperature
    // ========================================================
    double exhaustRawF = NAN;       // MAX31855 raw
    double exhaustSmoothF = NAN;    // smoothed for UI + BurnEngine

    // ========================================================
    //  Fan
    // ========================================================
    int fanRaw = 0;                 // BurnEngine output
    int fanFinal = 0;               // FanControl output

    // ========================================================
    //  Burn State + Timers
    // ========================================================
    BurnState burnState = BURN_RAMP;

    bool boostActive = false;
    unsigned long boostStartMs = 0;

    bool holdTimerActive = false;
    unsigned long holdStartMs = 0;
    unsigned long holdStabilityMs = 5000;

    bool rampTimerActive = false;
    unsigned long rampStartMs = 0;
    unsigned long rampStabilityMs = 3000;

    bool coalbedTimerActive = false;
    unsigned long coalbedStartMs = 0;

    // ========================================================
    //  Environmental (BME280)
    // ========================================================
    float envTempF = NAN;
    float envHumidity = NAN;
    float envPressure = NAN;
    bool envSensorOK = false;

    // ========================================================
    //  User Settings (EEPROM-backed)
    // ========================================================
    int16_t exhaustSetpoint = 350;
    int16_t deadbandF = 25;
    int16_t boostTimeSeconds = 30;

    int16_t clampMinPercent = 10;
    int16_t clampMaxPercent = 100;
    int16_t deadzoneFanMode = 0;

    int16_t coalBedTimerMinutes = 30;
    int16_t flueLowThreshold = 250;
    int16_t flueRecoveryThreshold = 300;

    // ========================================================
    //  Flags for UI redraw or remote sync
    // ========================================================
    bool uiNeedsRefresh = false;    // UI should redraw
    bool remoteChanged = false;     // MQTT/WiFi changed a value
};

// Global instance
extern SystemData sys;

#endif
