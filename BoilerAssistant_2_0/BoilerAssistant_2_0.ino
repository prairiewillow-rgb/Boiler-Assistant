/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v2.0 – BurnEngine)
 *  ------------------------------------------------------------
 *  File: BoilerAssistant_2_0_0.ino
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Main entry point for the Boiler Assistant controller.
 *    Coordinates:
 *      - Sensor acquisition
 *      - BurnEngine v2.0 (BOOST, RAMP, HOLD, COALBED, SAFETY)
 *      - Stability timers (HOLD, RAMP, COALBED entry)
 *      - Fan control (clamp, deadzone, BOOST, soft caps)
 *      - Non-blocking boot screen
 *      - UI rendering + keypad input
 *
 *  v2.0 Major Changes:
 *      - Added BOOST countdown timer
 *      - Added HOLD and RAMP stability timers
 *      - Added COALBED entry timer
 *      - Non-blocking boot animation (no UI freeze)
 *      - COALBED UI no longer hides data
 *      - Backlight pulse effects (slow for COALBED, fast for SAFETY)
 *      - RAM-safe lcd4() renderer (no String fragmentation)
 *      - Globalized stability timer variables for UI access
 *      - Cleaned BurnEngine state machine transitions
 *
 * ============================================================
 */

#include <Arduino.h>
#include "Pinout.h"
#include "SystemState.h"
#include "Sensors.h"
#include "FanControl.h"
#include "BurnEngine.h"
#include "UI.h"
#include "EEPROMStorage.h"
#include "Keypad.h"
#include <stdint.h>

// ============================================================
//  GLOBAL STATE DEFINITIONS (actual storage)
// ============================================================

UIState uiState = UI_HOME;
bool uiNeedRedraw = true;

BurnState burnState = BURN_RAMP;

int16_t exhaustSetpoint       = 350;
int16_t boostTimeSeconds      = 30;
int16_t deadbandF             = 25;

int16_t clampMinPercent       = 10;
int16_t clampMaxPercent       = 100;
int16_t deadzoneFanMode       = 0;

int16_t coalBedTimerMinutes   = 30;
int16_t flueLowThreshold      = 250;
int16_t flueRecoveryThreshold = 300;

float envTempF     = NAN;
float envHumidity  = NAN;
float envPressure  = NAN;
bool  envSensorOK  = false;

String newSetpointValue       = "";
String boostTimeEditValue     = "";
String deadbandEditValue      = "";
String clampMinEditValue      = "";
String clampMaxEditValue      = "";

String coalBedTimerEditValue  = "";
String flueLowEditValue       = "";
String flueRecEditValue       = "";

char lastKey = 0;

// ============================================================
//  setup()
// ============================================================
void setup() {

    Serial.begin(115200);
    delay(200);

    // 1. Burn engine sets safe defaults (in case EEPROM is blank)
    burnengine_init();

    // 2. EEPROM overwrites with saved values (if valid)
    eeprom_init();

    // 3. Hardware modules
    sensors_init();
    fancontrol_init();
    keypad_init();
    ui_init();

    uiNeedRedraw = true;
}

// ============================================================
//  loop()
// ============================================================
void loop() {

    // 1. Read exhaust temperature
    double rawExhaust = exhaust_readF_cached();
    double smoothExh  = smoothExhaustF(rawExhaust);

    // 2. BurnEngine → raw fan %
    int rawFan = burnengine_compute(smoothExh);

    // 3. FanControl → final fan %
    int finalFan = fancontrol_apply(rawFan);

    // 4. Keypad input
    char key = getKey();

    if (key != 0) {
        if (key != lastKey) {
            ui_handleKey(key, smoothExh, finalFan);
        }
        lastKey = key;
    } else {
        lastKey = 0;
    }

    // 5. UI rendering
    ui_showScreen(uiState, smoothExh, finalFan);

    // 6. Safe keypad scan timing
    delayMicroseconds(500);
}
