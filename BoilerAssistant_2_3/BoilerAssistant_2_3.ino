/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: BoilerAssistant.ino
 *  Maintainer: Karl (Embedded Systems Architect)
 *
 *  Description:
 *      This is the Arduino entry point for the Boiler Assistant.
 *      All modules (BurnEngine, EnvironmentalLogic, UI, Sensors,
 *      EEPROMStorage, WiFiAPI, MQTT, FanControl) are compiled as
 *      separate .cpp units, but setup() and loop() MUST live in
 *      the .ino file for the Arduino build system.
 *
 *      This file contains:
 *        • Global runtime state
 *        • setup() initialization sequence
 *        • loop() real‑time execution cycle
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
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
#include "EnvironmentalLogic.h"

/* ============================================================
 *  GLOBAL STATE (moved from main.cpp)
 * ============================================================ */

BurnState burnState = BURN_IDLE;

int16_t smoothExh      = 0;
int16_t lastFanPercent = 0;
int16_t lastExhaustF   = 0;

bool           holdTimerActive   = false;
unsigned long  holdStartMs       = 0;
unsigned long  HOLD_STABILITY_MS = 300000UL;

bool           rampTimerActive   = false;
unsigned long  rampStartMs       = 0;
unsigned long  RAMP_STABILITY_MS = 300000UL;

// Ember Guardian (v2.3 unified)
bool           emberGuardianTimerActive = false;
unsigned long  emberGuardianStartMs     = 0;

unsigned long  boostStartMs             = 0;

int16_t        emberGuardianTimerMinutes = 30;
bool           emberGuardianActive       = false;

// User settings (EEPROM-backed)
int16_t exhaustSetpoint       = 350;
int16_t boostTimeSeconds      = 30;
int16_t deadbandF             = 25;
int16_t clampMinPercent       = 10;
int16_t clampMaxPercent       = 100;
int16_t deadzoneFanMode       = 0;

int16_t flueLowThreshold      = 100;
int16_t flueRecoveryThreshold = 300;

// UI edit buffers
String newSetpointValue;
String boostTimeEditValue;
String deadbandEditValue;
String clampMinEditValue;
String clampMaxEditValue;
String emberGuardianEditValue;
String flueLowEditValue;
String flueRecEditValue;

uint8_t globalRampProfile = 1;

// Environmental config
uint8_t  envSeasonMode        = 0;
bool     envAutoSeasonEnabled = true;
uint32_t envModeLockoutSec    = 6UL * 3600UL;

int16_t envSummerStartF      = 70;
int16_t envSpringFallStartF  = 40;
int16_t envWinterStartF      = 10;
int16_t envExtremeStartF     = -20;

int16_t envHystSummerF       = 5;
int16_t envHystSpringFallF   = 5;
int16_t envHystWinterF       = 5;
int16_t envHystExtremeF      = 5;

uint8_t envClampSummer       = 100;
uint8_t envClampSpringFall   = 100;
uint8_t envClampWinter       = 100;
uint8_t envClampExtreme      = 100;

uint8_t envRampSummer        = 1;
uint8_t envRampSpringFall    = 1;
uint8_t envRampWinter        = 1;
uint8_t envRampExtreme       = 1;

int16_t envSetpointSummerF      = 450;
int16_t envSetpointSpringFallF  = 500;
int16_t envSetpointWinterF      = 550;
int16_t envSetpointExtremeF     = 600;

// UI state
UIState uiState      = UI_HOME;
bool    uiNeedRedraw = true;

// Sensor externs
extern float   waterTempF[MAX_WATER_PROBES];
extern uint8_t probeCount;
extern uint8_t probeRoleMap[PROBE_ROLE_COUNT];

extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;

// Forward declarations
double exhaust_readF_cached();
double smoothExhaustF(double raw);

/* ============================================================
 *  SETUP – INITIALIZATION SEQUENCE
 * ============================================================ */

void setup() {
    Wire.begin();
    Wire.setClock(400000);

    pinMode(PIN_FAN_PWM, OUTPUT);
    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, LOW);

    eeprom_init();
    sensors_init();
    env_logic_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    wifiapi_init();
    mqtt_init();

    burnengine_startBoost();
}

/* ============================================================
 *  MAIN LOOP – REAL‑TIME SAFE EXECUTION
 * ============================================================ */

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
    // 5) WiFi + MQTT
    // --------------------------------------------------------
    wifiapi_loop();

    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
        sys.wifiOK = true;
    } else {
        sys.wifiOK = false;
    }

    mqtt_loop();

    // --------------------------------------------------------
    // 6) UI
    // --------------------------------------------------------
    ui_showScreen(uiState, smoothExh, fanPercent);
}
