/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BoilerAssistant_3_Total Domination.ino
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core deterministic firmware for the Boiler Assistant controller.
 *    Version 3.0 introduces the Total Domination Architecture (TDA):
 *      - SystemData as the single source of truth
 *      - Deterministic, non-blocking main loop
 *      - Unified keypad-driven UI with numeric selection everywhere
 *      - Fully transparent operator-facing logic and documentation
 *
 *    Subsystems coordinated by this module:
 *      - Environmental sensing (BME280)
 *      - Temperature sensing (DS18B20)
 *      - Burn engine logic (demand, ramp, hold, safety)
 *      - Fan control (PWM, clamp logic, deadzone modes)
 *      - UI rendering (LCD) + keypad-driven operator interface
 *      - EEPROM-backed configuration and seasonal profiles
 *      - WiFi provisioning (STA-first, AP-fallback)
 *      - WiFi API + MQTT telemetry (async, non-blocking)
 *
 *  v3.0 Additions:
 *      - Total Domination Architecture (TDA) baseline
 *      - Strengthened SystemData propagation across all modules
 *      - Standardized LCD capitalization rules
 *      - Expanded UI edit buffers for operator clarity
 *      - Improved exhaust smoothing pipeline
 *      - Legacy compatibility shims for v2.2 → v3.x
 *
 *  Architectural Notes:
 *      - Main loop is strictly deterministic and non-blocking
 *      - All subsystems operate on timed or event-driven cadence
 *      - WiFi + MQTT run asynchronously to avoid blocking control logic
 *      - UI executes last to ensure stable system state before rendering
 *      - NamingManifest.h is the authoritative naming contract
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>

#include "SystemState.h"          // MUST be first project header
#include "EnvironmentalLogic.h"   // MUST be second
#include "SystemData.h"           // MUST be after both
#include "UI.h"

#include "EEPROMStorage.h"
#include "Sensors.h"
#include "BurnEngine.h"
#include "FanControl.h"
#include "Keypad_I2C.h"
#include "Pinout.h"

#include <WiFiS3.h>
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "WiFiProvisioning.h"

/* ============================================================
 *  COMPATIBILITY SHIMS (v2.2 → v3.x)
 * ============================================================ */
#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

/* ============================================================
 *  GLOBAL STATE (minimal shims + runtime)
 * ============================================================ */

BurnState   burnState   = BURN_IDLE;
RunMode     controlMode = RUNMODE_CONTINUOUS;   // shim for any legacy users
SafetyState safetyState = SAFETY_OK;

int16_t tankLowSetpointF  = 140;   // shim
int16_t tankHighSetpointF = 180;   // shim

int16_t smoothExh      = 0;
int16_t lastFanPercent = 0;
int16_t lastExhaustF   = 0;

bool           holdTimerActive   = false;
unsigned long  holdStartMs       = 0;

bool           rampTimerActive   = false;
unsigned long  rampStartMs       = 0;

bool           emberGuardianTimerActive = false;
unsigned long  emberGuardianStartMs     = 0;

unsigned long  boostStartMs             = 0;

uint8_t globalRampProfile = 1;

// UI state
UIState uiState      = UI_HOME;
bool    uiNeedRedraw = true;

// UI edit buffers
String newSetpointValue;
String boostTimeEditValue;
String deadbandEditValue;
String clampMinEditValue;
String clampMaxEditValue;
String emberGuardianEditValue;
String flueLowEditValue;
String flueRecEditValue;
String tankLowEditValue;
String tankHighEditValue;
String envSeasonEditValue;
String envSetpointEditValue;
String envLockoutEditValue;

/* Forward declarations */
double exhaust_readF_cached();

/* Local implementation of exhaust smoothing */
double smoothExhaustF(double rawF) {
    if (isnan(sys.exhaustSmoothF)) {
        sys.exhaustSmoothF = rawF;
    } else {
        sys.exhaustSmoothF = sys.exhaustSmoothF * 0.8 + rawF * 0.2;
    }
    return sys.exhaustSmoothF;
}

/* ============================================================
 *  SETUP
 * ============================================================ */

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, HIGH);   // default CLOSED

    pinMode(PIN_FAN_PWM, OUTPUT);

    Serial.println();
    Serial.println("=== Boiler Assistant v3.0 Boot ===");

    Wire.begin();
    Wire.setClock(400000);

    // SystemData must be initialized before EEPROM populates it
    systemdata_init();

    // Load all EEPROM-backed settings into sys.*
    eeprom_init();

    // Minimal shims for any legacy modules still using these globals
    controlMode       = sys.controlMode;
    tankLowSetpointF  = sys.tankLowSetpointF;
    tankHighSetpointF = sys.tankHighSetpointF;

    // Sensors + logic
    sensors_init();
    env_logic_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    // Provisioning: STA-first, AP-fallback
    wifi_prov_init();

    if (!wifi_prov_isAPMode()) {
        wifiapi_init();
        mqtt_init();
    }

    burnengine_startBoost();
}

/* ============================================================
 *  LOOP
 * ============================================================ */

void loop() {

    unsigned long now = millis();

    // 0) Keypad
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        smoothExh = (int16_t)smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothExh, lastFanPercent);
        uiNeedRedraw = true;
    }

    // 1) Sensors
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

    // 2) Burn engine – exhaust pipeline
    double rawExh = exhaust_readF_cached();
    sys.exhaustRawF = rawExh;                    // live raw flue temp for Guardian
    smoothExh = (int16_t)smoothExhaustF(rawExh);
    sys.exhaustSmoothF = smoothExh;             // live smoothed flue temp for control

    int demand = burnengine_compute();

    // 3) Fan control (single source of truth)
    int fanPercent = fancontrol_apply(demand);
    lastFanPercent = fanPercent;

    int pwm = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwm);

    // 4) Update SystemData snapshot for UI / WiFi / MQTT

    // Minimal shims: keep these globals in sync for any legacy users
    controlMode       = sys.controlMode;
    tankLowSetpointF  = sys.tankLowSetpointF;
    tankHighSetpointF = sys.tankHighSetpointF;

    sys.fanFinal = fanPercent;

    // Mirror from sys → legacy globals (never the other way)
    burnState   = sys.burnState;
    safetyState = sys.safetyState;

    sys.uptimeMs = now;

    // 5) WiFi + MQTT (only when NOT in AP mode)
    if (!wifi_prov_isAPMode()) {
        wifiapi_loop();
        mqtt_loop();
    }

    // 6) UI
    ui_showScreen(uiState, smoothExh, fanPercent);

    // 7) Provisioning AP handler
    wifi_prov_loop();
}
