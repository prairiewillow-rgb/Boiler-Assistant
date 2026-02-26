/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v1.3.1 – Fan Fix)
 *  ------------------------------------------------------------
 *  File: BoilerAssistant_1_3_1.ino
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Main entry point for the Boiler Assistant controller.
 *    Coordinates:
 *      - Sensor acquisition
 *      - Burn logic (Adaptive or PID)
 *      - Fan control (clamp, deadzone, BOOST)
 *      - v1.3.1: Deterministic fan OFF gate (in FanControl.cpp)
 *      - Damper ALWAYS OPEN (in FanControl.cpp)
 *      - UI rendering + keypad input
 *
 * ============================================================
 */

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>

#include "Pinout.h"
#include "SystemState.h"
#include "Sensors.h"
#include "FanControl.h"
#include "BurnLogic.h"
#include "Adaptive.h"
#include "UI.h"
#include "EEPROMStorage.h"

// ============================================================
// LED MATRIX
// ============================================================
ArduinoLEDMatrix matrix;

// Fire buffers
uint8_t fireBuf[8][12];
unsigned long lastFire = 0;

// ============================================================
// GLOBAL STATE (1.3 ORIGINAL)
// ============================================================
UIState uiState = UI_HOME;
bool uiNeedRedraw = true;

BurnState burnState = BURN_ADAPTIVE;

int exhaustSetpoint     = 350;
int burnLogicMode       = 0;
int boostTimeSeconds    = 30;
int deadbandF           = 25;

float pidBelowKp = 0.8, pidBelowKi = 0.02, pidBelowKd = 0.0;
float pidNormKp  = 0.6, pidNormKi  = 0.01, pidNormKd  = 0.0;
float pidAboveKp = 0.4, pidAboveKi = 0.00, pidAboveKd = 0.0;

float adaptiveSlope = 1.0;

int clampMinPercent = 10;
int clampMaxPercent = 100;

int deadzoneFanMode = 0;
bool fanIsOff = false;

double lastRate = 0;
double lastT = 0;
unsigned long lastTtime = 0;

unsigned long burnBoostStart = 0;

float envTempF = NAN;
float envHumidity = NAN;
float envPressure = NAN;
bool envSensorOK = false;

int waterSensorCount = 0;
double waterTemps[8] = {NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN};

String newSetpointValue = "";
String boostTimeEditValue = "";
String deadbandEditValue = "";
String pidEditValue = "";
String clampMinEditValue = "";
String clampMaxEditValue = "";

int burnLogicSelected = 0;
int pidProfileSelected = 1;
int pidParamSelected = 1;

char lastKey = 0;

// Anti‑stutter
int lastRawFan = 0;

// ============================================================
// FIRE ENGINE
// ============================================================
void updateFire() {

    for (int x = 0; x < 12; x++)
        fireBuf[7][x] = random(0, 2);

    for (int y = 6; y >= 0; y--) {
        for (int x = 0; x < 12; x++) {

            int decay = random(0, 2);
            int below = fireBuf[y + 1][x];

            int newVal = below - decay;
            if (newVal < 0) newVal = 0;

            int shift = random(-1, 2);
            int nx = constrain(x + shift, 0, 11);

            fireBuf[y][nx] = newVal;
        }
    }
}

void drawFire() {
    matrix.renderBitmap(fireBuf, 8, 12);
}

// ============================================================
// KEYPAD READER
// ============================================================
char readKeypad() {
    static const char keys[4][4] = {
        {'1','2','3','A'},
        {'4','5','6','B'},
        {'7','8','9','C'},
        {'*','0','#','D'}
    };

    for (int col = 0; col < 4; col++) {
        pinMode(PIN_KEYPAD_COL1 + col, OUTPUT);
        digitalWrite(PIN_KEYPAD_COL1 + col, LOW);

        for (int row = 0; row < 4; row++) {
            pinMode(PIN_KEYPAD_ROW1 + row, INPUT_PULLUP);
            if (digitalRead(PIN_KEYPAD_ROW1 + row) == LOW) {
                digitalWrite(PIN_KEYPAD_COL1 + col, HIGH);
                pinMode(PIN_KEYPAD_COL1 + col, INPUT);
                return keys[row][col];
            }
        }

        digitalWrite(PIN_KEYPAD_COL1 + col, HIGH);
        pinMode(PIN_KEYPAD_COL1 + col, INPUT);
    }

    return 0;
}

// ============================================================
// SETUP
// ============================================================
void setup() {

    eeprom_init();
    sensors_init();
    fancontrol_init();
    burnlogic_init();
    ui_init();

    // >>> AUTO‑BOOST ON POWER‑UP <<<
    burnState = BURN_BOOST;
    burnBoostStart = millis();

    matrix.begin();
}


// ============================================================
// LOOP
// ============================================================
void loop() {

    double rawExhaust = exhaust_readF_cached();
    double smoothExh  = smoothExhaustF(rawExhaust);

    int rawFan = burnlogic_compute(smoothExh);

    rawFan = (rawFan + lastRawFan) / 2;
    lastRawFan = rawFan;

    fancontrol_updateBoost();
    int finalFan = fancontrol_apply(rawFan);

    char key = readKeypad();

    if (key != 0) {
        if (key != lastKey) {
            ui_handleKey(key, smoothExh, finalFan);
        }
        lastKey = key;
    }
    else {
        lastKey = 0;
    }

    if (uiNeedRedraw) {
        uiNeedRedraw = false;
        ui_showScreen(uiState, smoothExh, finalFan);
    }

    if (millis() - lastFire > 50) {
        updateFire();
        drawFire();
        lastFire = millis();
    }

    delay(20);
}
