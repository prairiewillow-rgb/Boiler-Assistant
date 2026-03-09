/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v2.2)
 *  ------------------------------------------------------------
 *  File: main.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core real‑time firmware for the Boiler Assistant controller.
 *    Coordinates all subsystems including:
 *      - Sensor acquisition (BME280 + DS18B20)
 *      - Burn engine logic and demand computation
 *      - Fan control and PWM output
 *      - UI rendering and keypad input
 *      - EEPROM‑backed configuration management
 *      - WiFi stack, HTTP API, and MQTT telemetry
 *
 *  v2.2 Additions:
 *      - Standardized v2.2 header format (no codename)
 *      - Unified module metadata across firmware
 *      - Improved documentation for initialization sequence
 *      - Clarified subsystem ordering in main loop
 *
 *  Architectural Notes:
 *      - Main loop is strictly deterministic and non‑blocking
 *      - All subsystems operate on timed or event‑driven cadence
 *      - WiFi + MQTT run asynchronously to avoid blocking control logic
 *      - UI always executes last to ensure stable system state
 *
 *  Version:
 *      Boiler Assistant v2.2
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
#include "WiFiS3.h"
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "secrets.h"

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
    // WiFi + MQTT (non-blocking)
    // --------------------------------------------------------
    wifiapi_init();     // Start WiFi stack + HTTP API
    mqtt_init();        // Prepare MQTT client

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
    // 0) Keypad (highest priority)
    // --------------------------------------------------------
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        smoothExh = (int16_t)smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothExh, lastFanPercent);
        uiNeedRedraw = true;
    }

    // --------------------------------------------------------
    // 1) Sensors (timed, non-blocking)
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
    // 2) Burn engine (pure math, deterministic)
    // --------------------------------------------------------
    double rawExh = exhaust_readF_cached();
    smoothExh = (int16_t)smoothExhaustF(rawExh);

    int demand = burnengine_compute();

    // --------------------------------------------------------
    // 3) Fan control (deterministic)
    // --------------------------------------------------------
    int fanPercent = fancontrol_apply(demand);
    lastFanPercent = fanPercent;

    int pwm = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwm);

    // --------------------------------------------------------
    // 4) Update SystemData (telemetry bus)
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
    // 5) WiFi + MQTT (non-blocking, deterministic)
    // --------------------------------------------------------

    wifiapi_loop();

    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
        sys.wifiOK = true;
    } else {
        sys.wifiOK = false;
    }

    mqtt_loop();

    // --------------------------------------------------------
    // 6) UI (always last)
    // --------------------------------------------------------
    ui_showScreen(uiState, smoothExh, fanPercent);
}
