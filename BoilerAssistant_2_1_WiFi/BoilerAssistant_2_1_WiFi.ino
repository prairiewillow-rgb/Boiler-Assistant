/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v2.1 – WiFi API Edition)
 *  ------------------------------------------------------------
 *  File: main.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Central application loop for Boiler Assistant 2.1‑WiFi.
 *    Coordinates:
 *      - Sensor acquisition (BME280 + water probes)
 *      - BurnEngine v2.1 (BOOST, RAMP, HOLD, EMBER GUARDIAN, SAFETY)
 *      - Stability timers (HOLD, RAMP, Ember Guardian entry)
 *      - Fan control (clamps, deadzone, BOOST, soft caps)
 *      - SystemData live telemetry bus
 *      - Non-blocking UI rendering + keypad input
 *      - WiFiAPI module (non-blocking JSON API + HA Discovery)
 *
 *  v2.1 Major Changes:
 *      - NEW: Ember Guardian
 *      - NEW: Full WiFiAPI module (non-blocking)
 *      - NEW: Home Assistant MQTT Discovery auto-publishing 
 *      - NEW: Live JSON telemetry endpoint (local API)
 *      - NEW: Background WiFi task loop (no blocking in main)
 *      - NEW: SystemData struct for unified telemetry transport
 *      - NEW: Version tracking + metadata for HA dashboards
 *      - NEW: Clean separation of concerns (UI, WiFi, Engine)
 *      - NEW: Real-time safe main loop (no WiFi.begin() calls)
 *      - NEW: Fan percent + exhaust smoothing exported to API
 *      - NEW: Water probe array exported to API + UI
 *
 *  Architectural Notes:
 *      - All networking is isolated inside WiFiAPI.cpp
 *      - Main loop remains deterministic and real-time safe.
 *      - BOOST always starts on power-up for operator clarity.
 *      - Ember Guardian shutdown logic.
 *      - All legacy globals retained for module compatibility.
 *
 *  Version:
 *      Boiler Assistant v2.1 – WiFi API Edition
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include "Pinout.h"
#include "SystemState.h"
#include "Sensors.h"
#include "BurnEngine.h"
#include "FanControl.h"
#include "UI.h"
#include "Keypad_I2C.h"
#include "EEPROMStorage.h"
#include "SystemData.h"
#include "WiFiAPI.h" 

// =====================================================================
//  LEGACY GLOBALS – REQUIRED BY MULTIPLE MODULES
// =====================================================================

BurnState burnState = BURN_IDLE;

int16_t smoothExh = 0;
int16_t lastFanPercent = 0;
int16_t lastExhaustF = 0;

bool holdTimerActive = false;
unsigned long holdStartMs = 0;
unsigned long HOLD_STABILITY_MS = 300000UL;

bool rampTimerActive = false;
unsigned long rampStartMs = 0;
unsigned long RAMP_STABILITY_MS = 300000UL;

bool coalbedTimerActive = false;
unsigned long coalbedStartMs = 0;

unsigned long boostStartMs = 0;

// Ember Guard globals
int16_t emberGuardMinutes = 30;
unsigned long emberGuardStartMs = 0;
bool emberGuardActive = false;

// User settings (EEPROM-backed)
int16_t exhaustSetpoint       = 350;
int16_t boostTimeSeconds      = 30;
int16_t deadbandF             = 25;
int16_t clampMinPercent       = 10;
int16_t clampMaxPercent       = 100;
int16_t deadzoneFanMode       = 0;

int16_t coalBedTimerMinutes   = 30;
int16_t flueLowThreshold      = 100;
int16_t flueRecoveryThreshold = 300;

// UI edit buffers
String newSetpointValue;
String boostTimeEditValue;
String deadbandEditValue;
String clampMinEditValue;
String clampMaxEditValue;
String coalBedTimerEditValue;
String flueLowEditValue;
String flueRecEditValue;

// =====================================================================
//  UI STATE
// =====================================================================

UIState uiState = UI_HOME;
bool uiNeedRedraw = true;

// Forward declarations
double exhaust_readF_cached();
double smoothExhaustF(double raw);

// =====================================================================
//  SETUP – INITIALIZATION SEQUENCE
// =====================================================================

void setup() {
    Wire.begin();
    Wire.setClock(400000);

    pinMode(PIN_FAN_PWM, OUTPUT);
    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, LOW);

    eeprom_init();
    sensors_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    // --------------------------------------------------------
    // WiFi + JSON API (non-blocking)
    // --------------------------------------------------------
    wifiapi_init();  

    // --------------------------------------------------------
    // FORCE BOOST ON EVERY POWER-UP
    // --------------------------------------------------------
    burnengine_startBoost();
}

// =====================================================================
//  MAIN LOOP – REAL‑TIME SAFE EXECUTION
// =====================================================================

void loop() {

    unsigned long now = millis();

    // --------------------------------------------------------
    // 0) Keypad
    // --------------------------------------------------------
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        smoothExh = (int16_t)smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothExh, lastFanPercent);
        uiNeedRedraw = true;
    }

    // --------------------------------------------------------
    // 1) Sensors
    // --------------------------------------------------------
    static unsigned long lastBME = 0;
    if (now - lastBME > 3000) {
        sensors_readBME280();
        lastBME = now;
    }

    static unsigned long lastWaterRead = 0;
    if (now - lastWaterRead > 500) {
        sensors_readWaterProbes();
        lastWaterRead = now;
    }

    // --------------------------------------------------------
    // 2) Burn engine
    // --------------------------------------------------------
    double rawExh = exhaust_readF_cached();
    smoothExh = (int16_t)smoothExhaustF(rawExh);

    int demand = burnengine_compute();

    // --------------------------------------------------------
    // 3) Fan control
    // --------------------------------------------------------
    int fanPercent = fancontrol_apply(demand);
    lastFanPercent = fanPercent;

    int pwm = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwm);

    // --------------------------------------------------------
    // 4) Update SystemData
    // --------------------------------------------------------
    sys.exhaustSmoothF = smoothExh;
    sys.fanFinal       = fanPercent;
    sys.burnState      = burnState;

    sys.envTempF       = envTempF;
    sys.envHumidity    = envHumidity;
    sys.envPressure    = envPressure;

    sys.waterProbeCount = probeCount;
    for (uint8_t i = 0; i < probeCount; i++) {
        sys.waterTempF[i] = waterTempF[i];
    }

    // --------------------------------------------------------
    // 5) WiFi JSON API (non-blocking)
    // --------------------------------------------------------
    wifiapi_loop(); 

    // --------------------------------------------------------
    // 6) UI
    // --------------------------------------------------------
    ui_showScreen(uiState, smoothExh, fanPercent);
}
