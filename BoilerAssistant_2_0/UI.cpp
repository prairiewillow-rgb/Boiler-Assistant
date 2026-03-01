/*
 * ============================================================
 *  Boiler Assistant – User Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: UI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      LCD UI and keypad-driven navigation system for the
 *      Boiler Assistant controller. Implements:
 *        • Non-blocking boot animation
 *        • Home screen with live exhaust, fan %, and phase
 *        • BOOST, RAMP, HOLD, COALBED, SAFETY displays
 *        • Stability timer readouts (HOLD, RAMP, COALBED)
 *        • RAM-safe lcd4() renderer (no String fragmentation)
 *        • Full settings editor (setpoint, boost, clamp, flue)
 *
 *  v2.0 Notes:
 *      - Integrated stability timers from BurnEngine v2.0
 *      - Added BOOST countdown timer
 *      - Added COALBED entry timer + saver UI
 *      - Added backlight pulse effects (slow/fast)
 *      - Non-blocking boot screen replaces blocking delays
 *      - UI state machine cleaned and expanded
 *
 * ============================================================
 */

#include "UI.h"
#include "SystemState.h"
#include "Pinout.h"
#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include "EEPROMStorage.h"
#include "BurnEngine.h"
#include <stdint.h>

// ============================================================
//  LCD Instance
// ============================================================
#define LCD_I2C_ADDRESS 0x27
static LiquidCrystal_PCF8574 lcd(LCD_I2C_ADDRESS);

// Extern globals from .ino
extern bool uiNeedRedraw;
extern UIState uiState;

extern int16_t exhaustSetpoint;
extern int16_t boostTimeSeconds;
extern int16_t deadbandF;
extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern int16_t coalBedTimerMinutes;
extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

extern String newSetpointValue;
extern String boostTimeEditValue;
extern String deadbandEditValue;
extern String clampMinEditValue;
extern String clampMaxEditValue;

extern String coalBedTimerEditValue;
extern String flueLowEditValue;
extern String flueRecEditValue;

extern BurnState burnState;

// ============================================================
//  (Optional) Stability timer externs from BurnEngine.cpp
// ============================================================
extern bool holdTimerActive;
extern unsigned long holdStartMs;
extern unsigned long HOLD_STABILITY_MS;

extern bool rampTimerActive;
extern unsigned long rampStartMs;
extern unsigned long RAMP_STABILITY_MS;

extern bool coalbedTimerActive;
extern unsigned long coalbedStartMs;

// BOOST start time (must be exposed from BurnEngine.cpp)
extern unsigned long boostStartMs;

// ============================================================
//  Internal LCD 4‑line renderer (String‑free, RAM‑safe)
// ============================================================
static void lcd4(const char* l1, const char* l2, const char* l3, const char* l4) {
    static char last[4][21] = {"","","",""};
    const char* lines[4] = { l1, l2, l3, l4 };

    for (int i = 0; i < 4; i++) {
        if (strncmp(lines[i], last[i], 20) != 0) {
            lcd.setCursor(0, i);
            lcd.print("                    ");
            lcd.setCursor(0, i);
            lcd.print(lines[i]);
            strncpy(last[i], lines[i], 20);
            last[i][20] = '\0';
        }
    }
}

// ============================================================
//  Non‑blocking Boot Screen State Machine
// ============================================================
static uint8_t bootStep = 0;
static unsigned long bootLast = 0;
static bool bootDone = false;

static const char* bootBar[] = {
    "                    ","#                   ","##                  ",
    "###                 ","####                ","#####               ",
    "######              ","#######             ","########            ",
    "#########           ","##########          ","###########         ",
    "############        ","#############       ","##############      ",
    "###############     ","################    ","#################   ",
    "##################  ","################### ","********************"
};

static void showBootScreen_nonblocking() {
    if (bootDone) return;

    unsigned long now = millis();

    switch (bootStep) {

        case 0:
            lcd.clear();
            lcd.setCursor(0,0); lcd.print("  BOILER ASSISTANT  ");
            bootLast = now;
            bootStep = 1;
            break;

        case 1:
            if (now - bootLast > 300) {
                lcd.setCursor(0,1); lcd.print("    Initializing    ");
                bootLast = now;
                bootStep = 2;
            }
            break;

        case 2:
            lcd.setCursor(0,2);
            lcd.print(bootBar[0]);
            bootLast = now;
            bootStep = 3;
            break;

        default:
            if (bootStep >= 3 && bootStep < 24) {
                if (now - bootLast > 70) {
                    uint8_t idx = bootStep - 3;
                    if (idx < 21) {
                        lcd.setCursor(0,2);
                        lcd.print(bootBar[idx]);
                    }
                    bootLast = now;
                    bootStep++;
                }
            }
            else if (bootStep == 24) {
                lcd.setCursor(0,3);
                lcd.print("   System Check OK  ");
                bootLast = now;
                bootStep = 25;
            }
            else if (bootStep == 25 && now - bootLast > 800) {
                lcd.clear();
                lcd.setCursor(0,1); lcd.print("        v2.0        ");
                bootLast = now;
                bootStep = 26;
            }
            else if (bootStep == 26 && now - bootLast > 1200) {
                bootDone = true;
            }
            break;
    }
}

// ============================================================
//  ui_init()
// ============================================================
void ui_init() {
    lcd.begin(20,4);
    lcd.setBacklight(255);
    bootStep = 0;
    bootLast = millis();
    bootDone = false;
}

// ============================================================
//  Home Screen
// ============================================================
void ui_showHome(double exhaustF, int fanPercent) {

    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1,21,"Exh Set: %3dF", exhaustSetpoint);

    if (exhaustF < 0) snprintf(l2,21,"Exh Cur: ----F");
    else snprintf(l2,21,"Exh Cur: %3dF", (int)(exhaustF + 0.5));

    if (fanPercent <= 0)
        snprintf(l3,21,"Fan: OFF");
    else
        snprintf(l3,21,"Fan: %3d%%", fanPercent);

    unsigned long now = millis();

    if (burnState == BURN_BOOST) {
        int remain = 0;
        if (now > boostStartMs) {
            long elapsed = (long)((now - boostStartMs) / 1000UL);
            remain = (int)boostTimeSeconds - (int)elapsed;
            if (remain < 0) remain = 0;
        }
        snprintf(l4,21,"BOOST: %2ds left", remain);
    }
    else if (burnState == BURN_RAMP) {
        if (holdTimerActive) {
            long msRemain = (long)HOLD_STABILITY_MS - (long)(now - holdStartMs);
            if (msRemain < 0) msRemain = 0;
            int secRemain = (int)(msRemain / 1000L);
            snprintf(l4,21,"RAMP (HOLD in %ds)", secRemain);
        } else {
            snprintf(l4,21,"Phase: RAMP");
        }
    }
    else if (burnState == BURN_HOLD) {
        if (rampTimerActive) {
            long msRemain = (long)RAMP_STABILITY_MS - (long)(now - rampStartMs);
            if (msRemain < 0) msRemain = 0;
            int secRemain = (int)(msRemain / 1000L);
            snprintf(l4,21,"HOLD (RAMP in %ds)", secRemain);
        } else {
            snprintf(l4,21,"Phase: HOLD");
        }
    }
    else if (burnState == BURN_COALBED) {
        if (coalbedTimerActive) {
            unsigned long requiredMs = (unsigned long)coalBedTimerMinutes * 60000UL;
            long msRemain = (long)requiredMs - (long)(now - coalbedStartMs);
            if (msRemain < 0) msRemain = 0;
            int minRemain = (int)(msRemain / 60000L);
            snprintf(l4,21,"COALBED in %2dm", minRemain);
        } else {
            snprintf(l4,21,"COAL BED SAVER");
        }
    }
    else if (burnState == BURN_IDLE) {
        snprintf(l4,21,"Phase: IDLE");
    }
    else if (burnState == BURN_SAFETY) {
        snprintf(l4,21,"!!! SAFETY MODE !!!");
    }
    else {
        snprintf(l4,21,"Phase: UNKNOWN");
    }

    static unsigned long lastFlash = 0;
    static bool backlightState = true;

    if (burnState == BURN_SAFETY) {
        if (now - lastFlash > 400) {
            backlightState = !backlightState;
            lcd.setBacklight(backlightState ? 255 : 0);
            lastFlash = now;
        }
    }
    else if (burnState == BURN_COALBED) {
        if (now - lastFlash > 1500) {
            backlightState = !backlightState;
            lcd.setBacklight(backlightState ? 255 : 80);
            lastFlash = now;
        }
    }
    else {
        lcd.setBacklight(255);
    }

    lcd4(l1,l2,l3,l4);
}

// ============================================================
//  Screen Renderers
// ============================================================
static void ui_showSetpoint() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"EXHAUST SETPOINT");
    snprintf(l2,21,"Current: %3dF", exhaustSetpoint);
    snprintf(l3,21,"New: %s", newSetpointValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showBoostTime() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"BOOST TIME (sec)");
    snprintf(l2,21,"Current: %3d", boostTimeSeconds);
    snprintf(l3,21,"New: %s", boostTimeEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showSystem() {
    lcd4(
        "SYSTEM SETTINGS",
        "1: DEADBAND",
        "2: CLAMP",
        "3: COALBED/FLUE"
    );
}

static void ui_showDeadband() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"DEADBAND (F)");
    snprintf(l2,21,"Current: %3d", deadbandF);
    snprintf(l3,21,"New: %s", deadbandEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showClampMenu() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"CLAMP SETTINGS");
    snprintf(l2,21,"1: Min: %3d%%", clampMinPercent);
    snprintf(l3,21,"2: Max: %3d%%", clampMaxPercent);
    snprintf(l4,21,"3: Deadzone Fan:%s", deadzoneFanMode?"ON":"OFF");
    lcd4(l1,l2,l3,l4);
}

static void ui_showClampMin() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"EDIT CLAMP MIN");
    snprintf(l2,21,"Current: %3d", clampMinPercent);
    snprintf(l3,21,"New: %s", clampMinEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showClampMax() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"EDIT CLAMP MAX");
    snprintf(l2,21,"Current: %3d", clampMaxPercent);
    snprintf(l3,21,"New: %s", clampMaxEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

// ============================================================
//  COAL BED TIMER
// ============================================================
static void ui_showCoalBedTimer() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"COAL BED TIMER");
    snprintf(l2,21,"Current: %2d min", coalBedTimerMinutes);
    snprintf(l3,21,"New: %s", coalBedTimerEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

// ============================================================
//  FLUE LOW THRESH
// ============================================================
static void ui_showFlueLow() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"FLUE LOW THRESH");
    snprintf(l2,21,"Current: %3dF", flueLowThreshold);
    snprintf(l3,21,"New: %s", flueLowEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

// ============================================================
//  FLUE RECOVERY THRESH
// ============================================================
static void ui_showFlueRec() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"FLUE REC THRESH");
    snprintf(l2,21,"Current: %3dF", flueRecoveryThreshold);
    snprintf(l3,21,"New: %s", flueRecEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

// ============================================================
//  ui_showScreen()
// ============================================================
void ui_showScreen(int state, double exhaustF, int fanPercent) {

    if (!bootDone) {
        showBootScreen_nonblocking();
        return;
    }

    switch(state) {
        case UI_HOME:              ui_showHome(exhaustF, fanPercent); break;
        case UI_SETPOINT:          ui_showSetpoint(); break;
        case UI_BOOSTTIME:         ui_showBoostTime(); break;
        case UI_SYSTEM:            ui_showSystem(); break;
        case UI_DEADBAND:          ui_showDeadband(); break;
        case UI_CLAMP_MENU:        ui_showClampMenu(); break;
        case UI_CLAMP_MIN:         ui_showClampMin(); break;
        case UI_CLAMP_MAX:         ui_showClampMax(); break;

        case UI_COALBED_TIMER:     ui_showCoalBedTimer(); break;
        case UI_FLUE_LOW:          ui_showFlueLow(); break;
        case UI_FLUE_REC:          ui_showFlueRec(); break;

        default:                   ui_showHome(exhaustF, fanPercent); break;
    }
}

// ============================================================
//  ui_handleKey()
// ============================================================
void ui_handleKey(char k, double exhaustF, int fanPercent) {
    if (!k) return;

    uiNeedRedraw = true;

    if (burnState == BURN_COALBED && k == '*') {
        burnengine_init();
        uiState = UI_HOME;
        return;
    }

    switch(uiState) {

        case UI_HOME:
            if (k=='A') { uiState=UI_SETPOINT; newSetpointValue=""; }
            else if (k=='B') { uiState=UI_BOOSTTIME; boostTimeEditValue=""; }
            else if (k=='C') { uiState=UI_SYSTEM; }   // ← this is lowercase 'c'
            break;

        case UI_SETPOINT:
            if (k>='0' && k<='9') newSetpointValue += k;
            else if (k=='#') {
                if (newSetpointValue.length() == 0) break;
                int v = newSetpointValue.toInt();
                if (v < 200) v = 200;
                if (v > 900) v = 900;
                exhaustSetpoint = (int16_t)v;
                eeprom_saveSetpoint(v);
                newSetpointValue = "";
                uiState = UI_HOME;
            }
            else if (k=='*') {
                newSetpointValue = "";
                uiState = UI_HOME;
            }
            break;

        case UI_BOOSTTIME:
            if (k>='0' && k<='9') boostTimeEditValue += k;
            else if (k=='#') {
                if (boostTimeEditValue.length() == 0) break;
                int v = boostTimeEditValue.toInt();
                if (v < 5) v = 5;
                if (v > 300) v = 300;
                boostTimeSeconds = (int16_t)v;
                eeprom_saveBoostTime(v);

                burnengine_startBoost();

                boostTimeEditValue = "";
                uiState = UI_HOME;
            }
            else if (k=='*') {
                boostTimeEditValue = "";
                uiState = UI_HOME;
            }
            break;

                case UI_SYSTEM:
            if (k=='1') uiState = UI_DEADBAND;
            else if (k=='2') uiState = UI_CLAMP_MENU;
            else if (k=='3') {
                coalBedTimerEditValue = "";
                uiState = UI_COALBED_TIMER;
            }
            else if (k=='*') uiState = UI_HOME;
            break;

        case UI_DEADBAND:
            if (k>='0' && k<='9') deadbandEditValue += k;
            else if (k=='#') {
                if (deadbandEditValue.length() == 0) break;
                int v = deadbandEditValue.toInt();
                if (v < 1) v = 1;
                if (v > 100) v = 100;
                deadbandF = (int16_t)v;
                eeprom_saveDeadband(v);
                deadbandEditValue = "";
                uiState = UI_SYSTEM;
            }
            else if (k=='*') {
                deadbandEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        case UI_CLAMP_MENU:
            if (k=='1') { clampMinEditValue=""; uiState = UI_CLAMP_MIN; }
            else if (k=='2') { clampMaxEditValue=""; uiState = UI_CLAMP_MAX; }
            else if (k=='3') {
                deadzoneFanMode = deadzoneFanMode ? 0 : 1;
                eeprom_saveDeadzone(deadzoneFanMode);
            }
            else if (k=='*') uiState = UI_SYSTEM;
            break;

        case UI_CLAMP_MIN:
            if (k>='0' && k<='9') clampMinEditValue += k;
            else if (k=='#') {
                if (clampMinEditValue.length() == 0) break;
                int v = clampMinEditValue.toInt();
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                clampMinPercent = (int16_t)v;
                eeprom_saveClampMin(v);
                clampMinEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k=='*') {
                clampMinEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        case UI_CLAMP_MAX:
            if (k>='0' && k<='9') clampMaxEditValue += k;
            else if (k=='#') {
                if (clampMaxEditValue.length() == 0) break;
                int v = clampMaxEditValue.toInt();
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                clampMaxPercent = (int16_t)v;
                eeprom_saveClampMax(v);
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k=='*') {
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        // =====================================================
        // COAL BED TIMER
        // =====================================================
        case UI_COALBED_TIMER:
            if (k>='0' && k<='9') coalBedTimerEditValue += k;
            else if (k=='#') {
                if (coalBedTimerEditValue.length() == 0) break;
                int v = coalBedTimerEditValue.toInt();
                if (v < 5) v = 5;
                if (v > 60) v = 60;
                coalBedTimerMinutes = (int16_t)v;
                eeprom_saveCoalBedTimer(v);
                coalBedTimerEditValue = "";
                flueLowEditValue = "";
                uiState = UI_FLUE_LOW;
            }
            else if (k=='*') {
                coalBedTimerEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        // =====================================================
        // FLUE LOW
        // =====================================================
        case UI_FLUE_LOW:
            if (k>='0' && k<='9') flueLowEditValue += k;
            else if (k=='#') {
                if (flueLowEditValue.length() == 0) break;
                int v = flueLowEditValue.toInt();
                if (v < 200) v = 200;
                if (v > 500) v = 500;
                flueLowThreshold = (int16_t)v;
                eeprom_saveFlueLow(v);
                flueLowEditValue = "";
                flueRecEditValue = "";
                uiState = UI_FLUE_REC;
            }
            else if (k=='*') {
                flueLowEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        // =====================================================
        // FLUE RECOVERY
        // =====================================================
        case UI_FLUE_REC:
            if (k>='0' && k<='9') flueRecEditValue += k;
            else if (k=='#') {
                if (flueRecEditValue.length() == 0) break;
                int v = flueRecEditValue.toInt();
                if (v < flueLowThreshold + 10) v = flueLowThreshold + 10;
                if (v > 600) v = 600;
                flueRecoveryThreshold = (int16_t)v;
                eeprom_saveFlueRecovery(v);
                flueRecEditValue = "";
                uiState = UI_HOME;
            }
            else if (k=='*') {
                flueRecEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        default:
            uiState = UI_HOME;
            break;
    }
}
