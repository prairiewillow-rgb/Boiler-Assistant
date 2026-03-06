/*
 * ============================================================
 *  Boiler Assistant – UI Module (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: UI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Final 20×4 LCD operator interface with:
 *        • Exhaust + water temperature dashboard
 *        • BOOST countdown
 *        • Ember Guardian countdown + full lockout screen
 *        • Water probe role assignment UI
 *        • BME280 environment screen
 *        • Network info (IP / WiFi / RSSI)
 *        • Full settings editor (setpoint, deadband, clamps, flue thresholds)
 *
 *      Notes:
 *        - Rendering is RAM‑safe (diff‑only updates)
 *        - Keypad input is fully non‑blocking
 *        - UI state machine defined in SystemState.h
 *        - EEPROMStorage handles persistence
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#include "UI.h"
#include "SystemState.h"
#include "EEPROMStorage.h"

#include <LiquidCrystal_PCF8574.h>
#include <Arduino.h>
#include <WiFiS3.h>   // For IP + WiFi status + RSSI

/* ============================================================
 *  EXTERNALS FROM .ino
 * ============================================================ */

extern BurnState burnState;

extern int16_t exhaustSetpoint;
extern int16_t boostTimeSeconds;
extern int16_t deadbandF;
extern int16_t clampMinPercent;
extern int16_t clampMaxPercent;
extern int16_t deadzoneFanMode;

extern int16_t flueLowThreshold;
extern int16_t flueRecoveryThreshold;

extern int16_t emberGuardMinutes;
extern bool    emberGuardActive;
extern unsigned long emberGuardStartMs;

extern unsigned long boostStartMs;

extern int16_t lastFanPercent;
extern int16_t smoothExh;

// BME280
extern float envTempF;
extern float envHumidity;
extern float envPressure;
extern bool  envSensorOK;

// Water probes
extern float   waterTempF[MAX_WATER_PROBES];
extern uint8_t probeCount;
extern uint8_t probeRoleMap[PROBE_ROLE_COUNT];

// UI globals
extern UIState uiState;
extern bool    uiNeedRedraw;

char uiLine4Buffer[32] = {0};   // REQUIRED DEFINITION

// BurnEngine hook
extern void burnengine_startBoost();

// Edit buffers
extern String newSetpointValue;
extern String boostTimeEditValue;
extern String deadbandEditValue;
extern String clampMinEditValue;
extern String clampMaxEditValue;
extern String coalBedTimerEditValue;
extern String flueLowEditValue;
extern String flueRecEditValue;

/* ============================================================
 *  LCD REFERENCE
 * ============================================================ */

static LiquidCrystal_PCF8574 *lcdRef = nullptr;

/* Forward declaration */
static void showBootScreen();

/* ============================================================
 *  WATER PROBE ROLE UI HELPERS
 * ============================================================ */

const char* roleNames[PROBE_ROLE_COUNT] = {
    "Tank",
    "L1 Supply",
    "L1 Return",
    "L2 Supply",
    "L2 Return",
    "L3 Supply",
    "L3 Return",
    "Spare"
};

static uint8_t selectedRole = 0;
static uint8_t selectedPhys = 0;

/* ============================================================
 *  RAM‑SAFE 4‑LINE RENDERER
 * ============================================================ */

static void lcd4(const char* l1, const char* l2, const char* l3, const char* l4) {
    static char last[4][21] = {"","","",""};
    const char* lines[4] = { l1, l2, l3, l4 };

    for (int i = 0; i < 4; i++) {
        if (strncmp(lines[i], last[i], 20) != 0) {
            lcdRef->setCursor(0, i);
            lcdRef->print("                    ");
            lcdRef->setCursor(0, i);
            lcdRef->print(lines[i]);
            strncpy(last[i], lines[i], 20);
            last[i][20] = '\0';
        }
    }
}

/* ============================================================
 *  UI INIT
 * ============================================================ */

void ui_init() {
    static LiquidCrystal_PCF8574 lcd(0x27);

    lcdRef = &lcd;

    lcd.begin(20,4);
    lcd.setBacklight(255);

    showBootScreen();

    uiState = UI_HOME;
    uiNeedRedraw = true;
}

/* ============================================================
 *  BOOT SCREEN
 * ============================================================ */

static void showBootScreen() {
    lcdRef->clear();
    lcdRef->setCursor(0,0); lcdRef->print("  BOILER ASSISTANT  ");
    delay(300);
    lcdRef->setCursor(0,1); lcdRef->print("    Initializing    ");
    delay(300);

    const char* bar[] = {
        "                    ","#                   ","##                  ",
        "###                 ","####                ","#####               ",
        "######              ","#######             ","########            ",
        "#########           ","##########          ","###########         ",
        "############        ","#############       ","##############      ",
        "###############     ","################    ","#################   ",
        "##################  ","################### ","********************"
    };

    for (int i = 0; i < 21; i++) {
        lcdRef->setCursor(0,2);
        lcdRef->print(bar[i]);
        delay(70);
    }

    lcdRef->setCursor(0,3);
    lcdRef->print("System Check OK");
    delay(800);

    lcdRef->clear();
    lcdRef->setCursor(0,0); lcdRef->print(" Preparing for burn ");
    lcdRef->setCursor(0,1); lcdRef->print("    Loading wifi    ");
    lcdRef->setCursor(0,2); lcdRef->print("    and sensors     ");
    lcdRef->setCursor(0,3); lcdRef->print("  v2.1-wifi-sensor  ");
    delay(1200);
}

/* ============================================================
 *  FINAL 20×4 HOME SCREEN
 * ============================================================ */

static void ui_showHome(double exhaustF, int fanPercent) {
    char l1[21], l2[21], l3[21], l4[21];

    // FULL EMBER GUARDIAN LOCKOUT
    if (emberGuardActive && burnState == BURN_EMBER_GUARD) {
        lcd4(
            "   EMBER GUARDIAN   ",
            "   Damper/Fan Off   ",
            "      Press *       ",
            "     TO RESET       "
        );
        return;
    }

    int tankIndex = (probeRoleMap[PROBE_TANK] < probeCount) ? probeRoleMap[PROBE_TANK] : 0;
    int tankF = (int)(waterTempF[tankIndex] + 0.5);

    const int wHighF = 0;
    const int wLowF  = 0;

    snprintf(l1,21,"E/SET:%3dF  W/H:%03dF", exhaustSetpoint, wHighF);

    if (exhaustF < 0)
        snprintf(l2,21,"E/CUR:---F  W/L:%03dF", wLowF);
    else
        snprintf(l2,21,"E/CUR:%3dF  W/L:%03dF", (int)(exhaustF + 0.5), wLowF);

    if (fanPercent <= 0)
        snprintf(l3,21,"FAN:OFF    W/C:%03dF", tankF);
    else
        snprintf(l3,21,"FAN:%3d%%    W/C:%03dF", fanPercent, tankF);

    // BOOST COUNTDOWN
    if (burnState == BURN_BOOST) {
        unsigned long now = millis();
        unsigned long elapsed = (now - boostStartMs) / 1000UL;
        long remain = boostTimeSeconds - elapsed;
        if (remain < 0) remain = 0;

        snprintf(l4,21,"BOOST: %2lds remain", remain);
        lcd4(l1,l2,l3,l4);
        return;
    }

    // EMBER GUARDIAN COUNTDOWN
    if (emberGuardStartMs > 0 && !emberGuardActive) {
        unsigned long now = millis();
        unsigned long elapsed = now - emberGuardStartMs;
        unsigned long totalMs = (unsigned long)emberGuardMinutes * 60000UL;

        long remainMs = totalMs - elapsed;
        if (remainMs < 0) remainMs = 0;

        int remainMin = (remainMs / 60000UL) + 1;

        snprintf(l4,21,"EMBER GUARDIAN in%2dm", remainMin);
    }
    else {
        switch (burnState) {
            case BURN_IDLE:  snprintf(l4,21,"MODE: IDLE         "); break;
            case BURN_RAMP:  snprintf(l4,21,"MODE: RAMP         "); break;
            case BURN_HOLD:  snprintf(l4,21,"MODE: HOLD         "); break;
            case BURN_BOOST: snprintf(l4,21,"MODE: BOOST        "); break;
            default:         snprintf(l4,21,"MODE: UNKNOWN      "); break;
        }
    }

    lcd4(l1,l2,l3,l4);
}

/* ============================================================
 *  NETWORK INFO SCREEN
 * ============================================================ */

static void ui_showNetworkInfo() {
    char l1[21], l2[21], l3[21], l4[21];

    IPAddress ip = WiFi.localIP();
    int rssi = WiFi.RSSI();

    snprintf(l1,21,"NETWORK INFO");

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(l2,21,"IP:%3d.%3d.%3d.%3d", ip[0], ip[1], ip[2], ip[3]);
        snprintf(l3,21,"WiFi: Connected");
        snprintf(l4,21,"RSSI:%4ddBm *=BACK", rssi);
    } else {
        snprintf(l2,21,"IP: ---");
        snprintf(l3,21,"WiFi: Not Conn");
        snprintf(l4,21,"*=BACK");
    }

    lcd4(l1,l2,l3,l4);
}

/* ============================================================
 *  WATER SENSOR MENUS
 * ============================================================ */

static void ui_showWaterMenu() {
    lcd4(
        "WATER SENSORS",
        "1: PROBE ROLES",
        "2: BME280",
        "*=BACK"
    );
}

static void ui_showProbeRoleScreen(uint8_t role, uint8_t physIndex) {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1,21,"Assign Probe:");
    snprintf(l2,21,"%-16s", roleNames[role]);

    snprintf(l3,21,"Phys: %d", physIndex);

    if (physIndex < probeCount) {
        snprintf(l4,21,"Temp: %5.1fF", waterTempF[physIndex]);
    } else {
        snprintf(l4,21,"Temp: ---.-F");
    }

    lcd4(l1,l2,l3,l4);
}

static void ui_showBME() {
    char l1[21], l2[21], l3[21], l4[21];

    if (!envSensorOK) {
        snprintf(l1,21,"BME280 SENSOR ERR ");
        snprintf(l2,21,"Check wiring      ");
        snprintf(l3,21,"                  ");
        snprintf(l4,21,"*=BACK            ");
    } else {
        snprintf(l1,21,"BME280 OUTDOOR");
        snprintf(l2,21,"T:%5.1fF H:%4.1f%%", envTempF, envHumidity);
        snprintf(l3,21,"P:%7.1fhPa", envPressure);
        snprintf(l4,21,"*=BACK            ");
    }

    lcd4(l1,l2,l3,l4);
}

/* ============================================================
 *  MENU SCREENS
 * ============================================================ */

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
        "1: DEADBAND",
        "2: CLAMP",
        "3: EMBER GUARDIAN",
        "4: NETWORK INFO"
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
    snprintf(l4,21,"3: Deadzone Fan:%s", deadzoneFanMode ? "ON":"OFF");
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

static void ui_showEmberGuardTimer() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"EMBER GUARDIAN TM");
    snprintf(l2,21,"Current: %2d min", emberGuardMinutes);
    snprintf(l3,21,"New: %s", coalBedTimerEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showFlueLow() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"FLUE LOW THRESH");
    snprintf(l2,21,"Current: %3dF", flueLowThreshold);
    snprintf(l3,21,"New: %s", flueLowEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

static void ui_showFlueRec() {
    char l1[21],l2[21],l3[21],l4[21];
    snprintf(l1,21,"FLUE REC THRESH");
    snprintf(l2,21,"Current: %3dF", flueRecoveryThreshold);
    snprintf(l3,21,"New: %s", flueRecEditValue.c_str());
    snprintf(l4,21,"*=CANCEL   #=SAVE");
    lcd4(l1,l2,l3,l4);
}

/* ============================================================
 *  PUBLIC: SHOW SCREEN
 * ============================================================ */

void ui_showScreen(UIState st, double exhaustF, int fanPercent) {
    switch (st) {
        case UI_HOME:             ui_showHome(exhaustF, fanPercent); break;
        case UI_SETPOINT:         ui_showSetpoint(); break;
        case UI_BOOSTTIME:        ui_showBoostTime(); break;
        case UI_SYSTEM:           ui_showSystem(); break;
        case UI_DEADBAND:         ui_showDeadband(); break;
        case UI_CLAMP_MENU:       ui_showClampMenu(); break;
        case UI_CLAMP_MIN:        ui_showClampMin(); break;
        case UI_CLAMP_MAX:        ui_showClampMax(); break;
        case UI_COAL_SAVER_TIMER: ui_showEmberGuardTimer(); break;
        case UI_FLUE_LOW:         ui_showFlueLow(); break;
        case UI_FLUE_REC:         ui_showFlueRec(); break;
        case UI_WATER_MENU:       ui_showWaterMenu(); break;
        case UI_WATER_PROBE_MENU: ui_showProbeRoleScreen(selectedRole, selectedPhys); break;
        case UI_BME_SCREEN:       ui_showBME(); break;
        case UI_NETWORK_INFO:     ui_showNetworkInfo(); break;
        default:                  ui_showHome(exhaustF, fanPercent); break;
    }
}
void ui_handleKey(char k, double exhaustF, int fanPercent) {
    if (!k) return;

    uiNeedRedraw = true;

    // GLOBAL EMBER GUARDIAN RESET HANDLER
    if (emberGuardActive && burnState == BURN_EMBER_GUARD) {
        if (k == '*') {
            emberGuardActive  = false;
            emberGuardStartMs = 0;
            burnengine_startBoost();   // full reset back to BOOST
            uiState = UI_HOME;
            return;
        }
    }

    switch (uiState) {

        case UI_HOME:
            if (k=='A') {
                uiState = UI_SETPOINT;
                newSetpointValue = "";
            }
            else if (k=='B') {
                uiState = UI_BOOSTTIME;
                boostTimeEditValue = "";
            }
            else if (k=='C') {
                uiState = UI_SYSTEM;
            }
            else if (k=='D') {
                uiState = UI_WATER_MENU;
            }
            break;

        case UI_SETPOINT:
            if (k>='0' && k<='9') newSetpointValue += k;
            else if (k=='#') {
                if (newSetpointValue.length() == 0) break;
                int v = newSetpointValue.toInt();
                if (v < 200) v = 200;
                if (v > 900) v = 900;
                exhaustSetpoint = v;
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
                boostTimeSeconds = v;
                eeprom_saveBoostTime(v);
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
                uiState = UI_COAL_SAVER_TIMER;
            }
            else if (k=='4') {
                uiState = UI_NETWORK_INFO;
            }
            else if (k=='*') uiState = UI_HOME;
            break;

        case UI_NETWORK_INFO:
            if (k=='*' || k=='B') uiState = UI_SYSTEM;
            break;

        case UI_DEADBAND:
            if (k>='0' && k<='9') deadbandEditValue += k;
            else if (k=='#') {
                if (deadbandEditValue.length() == 0) break;
                int v = deadbandEditValue.toInt();
                if (v < 1) v = 1;
                if (v > 100) v = 100;
                deadbandF = v;
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
                clampMinPercent = v;
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
                clampMaxPercent = v;
                eeprom_saveClampMax(v);
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k=='*') {
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        case UI_COAL_SAVER_TIMER:
            if (k>='0' && k<='9') coalBedTimerEditValue += k;
            else if (k=='#') {
                if (coalBedTimerEditValue.length() == 0) break;
                int v = coalBedTimerEditValue.toInt();
                if (v < 5) v = 5;
                if (v > 60) v = 60;
                emberGuardMinutes = v;
                eeprom_saveEmberGuardMinutes(v);
                coalBedTimerEditValue = "";
                flueLowEditValue = "";
                uiState = UI_FLUE_LOW;
            }
            else if (k=='*') {
                coalBedTimerEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        case UI_FLUE_LOW:
            if (k>='0' && k<='9') flueLowEditValue += k;
            else if (k=='#') {
                if (flueLowEditValue.length() == 0) break;
                int v = flueLowEditValue.toInt();
                if (v < 50) v = 50;
                if (v > 900) v = 900;
                flueLowThreshold = v;
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

        case UI_FLUE_REC:
            if (k>='0' && k<='9') flueRecEditValue += k;
            else if (k=='#') {
                if (flueRecEditValue.length() == 0) break;
                int v = flueRecEditValue.toInt();
                if (v < 50) v = 50;
                if (v > 900) v = 900;
                flueRecoveryThreshold = v;
                eeprom_saveFlueRecovery(v);
                flueRecEditValue = "";
                uiState = UI_SYSTEM;
            }
            else if (k=='*') {
                flueRecEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        case UI_WATER_MENU:
            if (k=='1') {
                selectedRole = 0;
                selectedPhys = probeRoleMap[0];
                uiState = UI_WATER_PROBE_MENU;
            }
            else if (k=='2') {
                uiState = UI_BME_SCREEN;
            }
            else if (k=='*') {
                uiState = UI_HOME;
            }
            break;

        case UI_WATER_PROBE_MENU:
            if (k=='U') {
                if (selectedRole > 0) selectedRole--;
                selectedPhys = probeRoleMap[selectedRole];
            }
            else if (k=='D') {
                if (selectedRole < PROBE_ROLE_COUNT - 1) selectedRole++;
                selectedPhys = probeRoleMap[selectedRole];
            }
            else if (k=='L') {
                if (selectedPhys > 0) selectedPhys--;
            }
            else if (k=='R') {
                if (selectedPhys + 1 < probeCount) selectedPhys++;
            }
            else if (k=='O' || k=='#') {
                probeRoleMap[selectedRole] = selectedPhys;
                eeprom_saveProbeRoles();
            }
            else if (k=='*' || k=='B') {
                uiState = UI_HOME;
            }
            break;

        case UI_BME_SCREEN:
            if (k=='*' || k=='B') {
                uiState = UI_HOME;
            }
            break;

        default:
            uiState = UI_HOME;
            break;
    }
}
