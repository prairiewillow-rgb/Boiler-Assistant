/*
 * ============================================================
 *  Boiler Assistant – UI Module (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: UI.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *
 *  Description:
 *      20×4 LCD operator interface providing:
 *        • Exhaust + water temperature dashboard
 *        • BOOST countdown
 *        • Ember Guardian countdown + lockout screen
 *        • Water probe role assignment UI
 *        • BME280 environment screen
 *        • Network info (IP / WiFi / RSSI)
 *        • Full settings editor (setpoint, deadband, clamps, flue thresholds)
 *        • ENVIRONMENT menu (per‑season Start / Buffer / Setpoint)
 *        • Seasonal Mode OFF / USER / AUTO + BME280 auto-season UI
 *
 *      Notes:
 *        - Rendering is RAM‑safe (diff‑only updates)
 *        - Keypad input is fully non‑blocking
 *        - UI state machine defined in SystemState.h
 *        - EEPROMStorage handles persistence
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#include "UI.h"
#include "SystemState.h"
#include "EEPROMStorage.h"
#include "EnvironmentalLogic.h"

#include <LiquidCrystal_PCF8574.h>
#include <Arduino.h>
#include <WiFiS3.h>
#include "secrets.h"

/* ============================================================
 *  EXTERNALS FROM MAIN APPLICATION
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

// Ember Guardian (v2.3)
extern int16_t       emberGuardianTimerMinutes;
extern bool          emberGuardianActive;
extern unsigned long emberGuardianStartMs;

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

char uiLine4Buffer[32] = {0};

// BurnEngine hook
extern void burnengine_startBoost();

// Edit buffers
extern String newSetpointValue;
extern String boostTimeEditValue;
extern String deadbandEditValue;
extern String clampMinEditValue;
extern String clampMaxEditValue;
extern String emberGuardianEditValue;
extern String flueLowEditValue;
extern String flueRecEditValue;

/* ============================================================
 *  ENVIRONMENTAL CONFIG EXTERNS (v2.3)
 * ============================================================ */

extern uint8_t  envSeasonMode;
extern bool     envAutoSeasonEnabled;
extern uint32_t envModeLockoutSec;

extern int16_t envSummerStartF;
extern int16_t envSpringFallStartF;
extern int16_t envWinterStartF;
extern int16_t envExtremeStartF;

extern int16_t envHystSummerF;
extern int16_t envHystSpringFallF;
extern int16_t envHystWinterF;
extern int16_t envHystExtremeF;

extern int16_t envSetpointSummerF;
extern int16_t envSetpointSpringFallF;
extern int16_t envSetpointWinterF;
extern int16_t envSetpointExtremeF;

extern void eeprom_saveEnvSeasonStarts();
extern void eeprom_saveEnvSeasonHyst();
extern void eeprom_saveEnvSeasonSetpoints();
extern void eeprom_saveEnvSeasonMode(uint8_t mode);
extern void eeprom_saveEnvAutoSeason(bool en);
extern void eeprom_saveEnvLockoutHours(uint8_t hours);

/* ============================================================
 *  LCD REFERENCE
 * ============================================================ */

static LiquidCrystal_PCF8574* lcdRef = nullptr;

/* Forward declaration */
static void showBootScreen();

/* ============================================================
 *  WATER PROBE ROLE UI HELPERS
 * ============================================================ */

const char* roleNames[PROBE_ROLE_COUNT] = {
    "Boiler Tank",
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
 *  ENVIRONMENT UI HELPERS (v2.3)
 * ============================================================ */

static String envSeasonEditValue;
static String envSetpointEditValue;
static String envLockoutEditValue;

static EnvSeason uiEditSeason = ENV_SEASON_SUMMER;
static EnvSeason uiUserSeason = ENV_SEASON_SUMMER;

static const char* envSeasonShortName(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:     return "SUMMER";
        case ENV_SEASON_SPRINGFALL: return "SPRING/Fall";
        case ENV_SEASON_WINTER:     return "WINTER";
        case ENV_SEASON_EXTREME:    return "EXTREME";
        default:                    return "???";
    }
}

static const char* envSeasonLongName(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:     return "SUMMER";
        case ENV_SEASON_SPRINGFALL: return "SPRING/FALL";
        case ENV_SEASON_WINTER:     return "WINTER";
        case ENV_SEASON_EXTREME:    return "EXTREME";
        default:                    return "???";
    }
}

static int16_t* ui_getSeasonStartPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:     return &envSummerStartF;
        case ENV_SEASON_SPRINGFALL: return &envSpringFallStartF;
        case ENV_SEASON_WINTER:     return &envWinterStartF;
        case ENV_SEASON_EXTREME:    return &envExtremeStartF;
        default:                    return &envSummerStartF;
    }
}

static int16_t* ui_getSeasonBufferPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:     return &envHystSummerF;
        case ENV_SEASON_SPRINGFALL: return &envHystSpringFallF;
        case ENV_SEASON_WINTER:     return &envHystWinterF;
        case ENV_SEASON_EXTREME:    return &envHystExtremeF;
        default:                    return &envHystSummerF;
    }
}

static int16_t* ui_getSeasonSetpointPtr(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:     return &envSetpointSummerF;
        case ENV_SEASON_SPRINGFALL: return &envSetpointSpringFallF;
        case ENV_SEASON_WINTER:     return &envSetpointWinterF;
        case ENV_SEASON_EXTREME:    return &envSetpointExtremeF;
        default:                    return &envSetpointSummerF;
    }
}

/* ============================================================
 *  RAM‑SAFE 4‑LINE RENDERER
 * ============================================================ */

static void lcd4(const char* l1, const char* l2, const char* l3, const char* l4) {
    static char last[4][21] = {"", "", "", ""};
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

    lcd.begin(20, 4);
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
    lcdRef->setCursor(0, 0); lcdRef->print("  BOILER ASSISTANT  ");
    delay(300);
    lcdRef->setCursor(0, 1); lcdRef->print("    Initializing    ");
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
        lcdRef->setCursor(0, 2);
        lcdRef->print(bar[i]);
        delay(70);
    }

    lcdRef->setCursor(0, 3);
    lcdRef->print("System Check OK");
    delay(800);

    lcdRef->clear();
    lcdRef->setCursor(0, 0); lcdRef->print(" Preparing for Burn ");
    lcdRef->setCursor(0, 1); lcdRef->print("    Loading wifi    ");
    lcdRef->setCursor(0, 2); lcdRef->print("    and sensors     ");
    lcdRef->setCursor(0, 3); lcdRef->print("  v2.3-Environment  ");
    delay(1200);
}

/* ============================================================
 *  HOME SCREEN
 * ============================================================ */

static void ui_showHome(double exhaustF, int fanPercent) {
    char l1[21], l2[21], l3[21], l4[21];

    if (emberGuardianActive && burnState == BURN_EMBER_GUARD) {
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

    snprintf(l1, 21, "E/SET:%3dF  W/H:%03dF", exhaustSetpoint, wHighF);

    if (exhaustF < 0)
        snprintf(l2, 21, "E/CUR:---F  W/L:%03dF", wLowF);
    else
        snprintf(l2, 21, "E/CUR:%3dF  W/L:%03dF", (int)(exhaustF + 0.5), wLowF);

    if (fanPercent <= 0)
        snprintf(l3, 21, "FAN: OFF    W/C:%03dF", tankF);
    else
        snprintf(l3, 21, "FAN:%3d%%    W/C:%03dF", fanPercent, tankF);

    if (burnState == BURN_BOOST) {
        unsigned long now = millis();
        unsigned long elapsed = (now - boostStartMs) / 1000UL;
        long remain = boostTimeSeconds - elapsed;
        if (remain < 0) remain = 0;

        snprintf(l4, 21, "BOOST: %2lds remain", remain);
        lcd4(l1, l2, l3, l4);
        return;
    }

    if (emberGuardianStartMs > 0 && !emberGuardianActive) {
        unsigned long now = millis();
        unsigned long elapsed = now - emberGuardianStartMs;
        unsigned long totalMs = (unsigned long)emberGuardianTimerMinutes * 60000UL;

        long remainMs = totalMs - elapsed;
        if (remainMs < 0) remainMs = 0;

        int remainMin = (remainMs / 60000UL) + 1;

        snprintf(l4, 21, "EMBER GUARDIAN in%2dm", remainMin);
    }
    else {
        switch (burnState) {
            case BURN_IDLE:  snprintf(l4, 21, "MODE: IDLE         "); break;
            case BURN_RAMP:  snprintf(l4, 21, "MODE: RAMPING UP   "); break;
            case BURN_HOLD:  snprintf(l4, 21, "MODE: In the Zone  "); break;
            case BURN_BOOST: snprintf(l4, 21, "MODE: BOOST        "); break;
            default:         snprintf(l4, 21, "MODE: UNKNOWN      "); break;
        }
    }

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  NETWORK INFO SCREEN
 * ============================================================ */

static bool wifi_credentials_are_placeholders() {
    return (strcmp(SECRET_SSID, "YOUR_WIFI_SSID") == 0) ||
           (strcmp(SECRET_PASS, "YOUR_WIFI_PASSWORD") == 0);
}

static void ui_showNetworkInfo() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "NETWORK INFO");

    if (wifi_credentials_are_placeholders()) {
        snprintf(l2, 21, "WiFi: DISABLED");
        snprintf(l3, 21, "No credentials");
        snprintf(l4, 21, "*=BACK");
        lcd4(l1, l2, l3, l4);
        return;
    }

    IPAddress ip = WiFi.localIP();
    int rssi = WiFi.RSSI();

    if (WiFi.status() == WL_CONNECTED && ip != IPAddress(0,0,0,0)) {
        snprintf(l2, 21, "IP:%3d.%3d.%3d.%3d", ip[0], ip[1], ip[2], ip[3]);
        snprintf(l3, 21, "WiFi: Connected");
        snprintf(l4, 21, "RSSI:%4ddBm *=BACK", rssi);
    }
    else {
        snprintf(l2, 21, "IP: ---");
        snprintf(l3, 21, "WiFi: Not Conn");
        snprintf(l4, 21, "*=BACK");
    }

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  WATER SENSOR MENUS
 * ============================================================ */

static void ui_showWaterMenu() {
    lcd4(
        "SENSORS",
        "1: PROBE ROLES",
        "2: BME280",
        "*=BACK"
    );
}

static void ui_showProbeRoleScreen(uint8_t role, uint8_t physIndex) {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "Assign Probe:");
    snprintf(l2, 21, "%-16s", roleNames[role]);
    snprintf(l3, 21, "Phys: %d", physIndex);

    if (physIndex < probeCount)
        snprintf(l4, 21, "Temp: %5.1fF", waterTempF[physIndex]);
    else
        snprintf(l4, 21, "Temp: ---.-F");

    lcd4(l1, l2, l3, l4);
}

static void ui_showBME() {
    char l1[21], l2[21], l3[21], l4[21];

    if (!envSensorOK) {
        snprintf(l1, 21, "BME280 SENSOR ERR ");
        snprintf(l2, 21, "Check wiring      ");
        snprintf(l3, 21, "                  ");
        snprintf(l4, 21, "*=BACK            ");
    }
    else {
        snprintf(l1, 21, "BME280");
        snprintf(l2, 21, "Out Temp:      %3.1fF", envTempF);
        snprintf(l3, 21, "Humidity:      %2.1f%%", envHumidity);
        snprintf(l4, 21, "Pressure: %7.1fhPa", envPressure);
    }

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  MENU SCREENS
 * ============================================================ */

static void ui_showSetpoint() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "EXHAUST SETPOINT");
    snprintf(l2, 21, "Current: %3dF", exhaustSetpoint);
    snprintf(l3, 21, "New: %s", newSetpointValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showBoostTime() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "BOOST TIME (sec)");
    snprintf(l2, 21, "Current: %3d", boostTimeSeconds);
    snprintf(l3, 21, "New: %s", boostTimeEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showSystem() {
    lcd4(
        "1: DEADBAND",
        "2: CLAMP",
        "3: ENVIRONMENT",
        "4: NETWORK INFO"
    );
}

static void ui_showDeadband() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "DEADBAND (F)");
    snprintf(l2, 21, "Current: %3d", deadbandF);
    snprintf(l3, 21, "New: %s", deadbandEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showClampMenu() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "1: Min: %3d%%", clampMinPercent);
    snprintf(l2, 21, "2: Max: %3d%%", clampMaxPercent);
    snprintf(l3, 21, "3: Deadzone Fan:%s", deadzoneFanMode ? "ON " : "OFF");
    snprintf(l4, 21, "4: Ember Guardian");

    lcd4(l1, l2, l3, l4);
}

static void ui_showClampMin() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "EDIT CLAMP MIN");
    snprintf(l2, 21, "Current: %3d", clampMinPercent);
    snprintf(l3, 21, "New: %s", clampMinEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showClampMax() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "EDIT CLAMP MAX");
    snprintf(l2, 21, "Current: %3d", clampMaxPercent);
    snprintf(l3, 21, "New: %s", clampMaxEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showEmberGuardianTimer() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "EMBER GUARDIAN");
    snprintf(l2, 21, "Current: %2d min", emberGuardianTimerMinutes);
    snprintf(l3, 21, "New: %s", emberGuardianEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showFlueLow() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "FLUE LOW THRESH");
    snprintf(l2, 21, "Current: %3dF", flueLowThreshold);
    snprintf(l3, 21, "New: %s", flueLowEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

static void ui_showFlueRec() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "FLUE REC THRESH");
    snprintf(l2, 21, "Current: %3dF", flueRecoveryThreshold);
    snprintf(l3, 21, "New: %s", flueRecEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}
/* ============================================================
 *  ENVIRONMENT MENUS
 * ============================================================ */

static void ui_showEnvMenu() {
    lcd4(
        "ENVIRONMENT LOGIC",
        "1: SEASONS",
        "2: LOCKOUT / MODE",
        "              *=BACK"
    );
}

static void ui_showSeasonsMenu() {
    lcd4(
        "1: SUMMER",
        "2: SPRING/FALL",
        "3: WINTER",
        "4: EXTREME"
    );
}

static void ui_showSeasonDetailMenu() {
    lcd4(
        envSeasonLongName(uiEditSeason),
        "1: START TEMP",
        "2: BUFFER",
        "3: EXHAUST SETPOINT"
    );
}

static void ui_showSeasonEditStart() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "%s START TEMP", envSeasonShortName(uiEditSeason));
    snprintf(l2, 21, "Current: %3dF", *ui_getSeasonStartPtr(uiEditSeason));
    snprintf(l3, 21, "New: %s", envSeasonEditValue.c_str());
    snprintf(l4, 21, "A=NEG  *=CANCEL");

    lcd4(l1, l2, l3, l4);
}

static void ui_showSeasonEditBuffer() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "%s BUFFER", envSeasonShortName(uiEditSeason));
    snprintf(l2, 21, "Current: %3dF", *ui_getSeasonBufferPtr(uiEditSeason));
    snprintf(l3, 21, "New: %s", envSeasonEditValue.c_str());
    snprintf(l4, 21, "A=NEG  *=CANCEL");

    lcd4(l1, l2, l3, l4);
}

static void ui_showSeasonEditSetpoint() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "%s SETPOINT", envSeasonShortName(uiEditSeason));
    snprintf(l2, 21, "Current: %3dF", *ui_getSeasonSetpointPtr(uiEditSeason));
    snprintf(l3, 21, "New: %s", envSetpointEditValue.c_str());
    snprintf(l4, 21, "A=NEG  *=CANCEL");

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  LOCKOUT / MODE / AUTO-SEASON
 * ============================================================ */

static void ui_showEnvLockoutMenu() {
    lcd4(
        "LOCKOUT / MODE",
        "1: MODE",
        "2: AUTO-SEASON",
        "3: LOCKOUT HOURS"
    );
}

static void ui_showEnvMode() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "MODE:");

    snprintf(l2, 21, "1: OFF%s",  (envSeasonMode == 0 ? "   <" : ""));
    snprintf(l3, 21, "2: USER%s", (envSeasonMode == 1 ? "  <" : ""));
    snprintf(l4, 21, "3: AUTO%s", (envSeasonMode == 2 ? "  <" : ""));

    lcd4(l1, l2, l3, l4);
}

static void ui_showEnvAutoSeason() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "AUTO-SEASON");
    snprintf(l2, 21, "Current: %s", envAutoSeasonEnabled ? "ON" : "OFF");
    snprintf(l3, 21, "*=TOGGLE");
    snprintf(l4, 21, "#=BACK");

    lcd4(l1, l2, l3, l4);
}

static void ui_showEnvLockoutHours() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "LOCKOUT HOURS");
    snprintf(l2, 21, "Current: %2lu", (unsigned long)(envModeLockoutSec / 3600UL));
    snprintf(l3, 21, "New: %s", envLockoutEditValue.c_str());
    snprintf(l4, 21, "*=CANCEL   #=SAVE");

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  SCREEN DISPATCHER
 * ============================================================ */

void ui_showScreen(UIState st, double exhaustF, int fanPercent) {
    switch (st) {
        case UI_HOME:                 ui_showHome(exhaustF, fanPercent); break;
        case UI_SETPOINT:             ui_showSetpoint(); break;
        case UI_BOOSTTIME:            ui_showBoostTime(); break;
        case UI_SYSTEM:               ui_showSystem(); break;
        case UI_DEADBAND:             ui_showDeadband(); break;
        case UI_CLAMP_MENU:           ui_showClampMenu(); break;
        case UI_CLAMP_MIN:            ui_showClampMin(); break;
        case UI_CLAMP_MAX:            ui_showClampMax(); break;
        case UI_EMBER_GUARD_TIMER:    ui_showEmberGuardianTimer(); break;
        case UI_FLUE_LOW:             ui_showFlueLow(); break;
        case UI_FLUE_REC:             ui_showFlueRec(); break;
        case UI_WATER_MENU:           ui_showWaterMenu(); break;
        case UI_WATER_PROBE_MENU:     ui_showProbeRoleScreen(selectedRole, selectedPhys); break;
        case UI_BME_SCREEN:           ui_showBME(); break;
        case UI_NETWORK_INFO:         ui_showNetworkInfo(); break;

        case UI_ENV_MENU:             ui_showEnvMenu(); break;
        case UI_SEASONS_MENU:         ui_showSeasonsMenu(); break;
        case UI_SEASON_DETAIL_MENU:   ui_showSeasonDetailMenu(); break;
        case UI_SEASON_EDIT_START:    ui_showSeasonEditStart(); break;
        case UI_SEASON_EDIT_BUFFER:   ui_showSeasonEditBuffer(); break;
        case UI_SEASON_EDIT_SETPOINT: ui_showSeasonEditSetpoint(); break;

        case UI_ENV_LOCKOUT:          ui_showEnvLockoutMenu(); break;
        case UI_ENV_MODE:             ui_showEnvMode(); break;
        case UI_ENV_AUTOSEASON:       ui_showEnvAutoSeason(); break;
        case UI_ENV_LOCKOUT_HOURS:    ui_showEnvLockoutHours(); break;

        default:
            ui_showHome(exhaustF, fanPercent);
            break;
    }
}
/* ============================================================
 *  PUBLIC: HANDLE KEYPAD INPUT
 * ============================================================ */

void ui_handleKey(char k, double exhaustF, int fanPercent) {
    if (!k) return;

    uiNeedRedraw = true;

    /* GLOBAL EMBER GUARDIAN RESET HANDLER */
    if (emberGuardianActive && burnState == BURN_EMBER_GUARD) {
        if (k == '*') {
            emberGuardianActive  = false;
            emberGuardianStartMs = 0;
            burnengine_startBoost();
            uiState = UI_HOME;
            return;
        }
    }

    switch (uiState) {

        /* HOME */
        case UI_HOME:
            if (k == 'A') {
                newSetpointValue = "";
                uiState = UI_SETPOINT;
            }
            else if (k == 'B') {
                boostTimeEditValue = "";
                uiState = UI_BOOSTTIME;
            }
            else if (k == 'C') {
                uiState = UI_SYSTEM;
            }
            else if (k == 'D') {
                uiState = UI_WATER_MENU;
            }
            break;

        /* SETPOINT */
        case UI_SETPOINT:
            if (k >= '0' && k <= '9') {
                newSetpointValue += k;
            }
            else if (k == '#') {
                if (newSetpointValue.length() == 0) break;

                int v = newSetpointValue.toInt();
                if (v < 200) v = 200;
                if (v > 900) v = 900;

                exhaustSetpoint = v;
                eeprom_saveSetpoint(v);

                newSetpointValue = "";
                uiState = UI_HOME;
            }
            else if (k == '*') {
                newSetpointValue = "";
                uiState = UI_HOME;
            }
            break;

        /* BOOST TIME */
        case UI_BOOSTTIME:
            if (k >= '0' && k <= '9') {
                boostTimeEditValue += k;
            }
            else if (k == '#') {
                if (boostTimeEditValue.length() == 0) break;

                int v = boostTimeEditValue.toInt();
                if (v < 5) v = 5;
                if (v > 300) v = 300;

                boostTimeSeconds = v;
                eeprom_saveBoostTime(v);

                boostTimeEditValue = "";
                uiState = UI_HOME;
            }
            else if (k == '*') {
                boostTimeEditValue = "";
                uiState = UI_HOME;
            }
            break;

        /* SYSTEM MENU */
        case UI_SYSTEM:
            if (k == '1') uiState = UI_DEADBAND;
            else if (k == '2') uiState = UI_CLAMP_MENU;
            else if (k == '3') uiState = UI_ENV_MENU;
            else if (k == '4') uiState = UI_NETWORK_INFO;
            else if (k == '*') uiState = UI_HOME;
            break;

        /* NETWORK INFO */
        case UI_NETWORK_INFO:
            if (k == '*' || k == 'B') uiState = UI_SYSTEM;
            break;

        /* DEADBAND */
        case UI_DEADBAND:
            if (k >= '0' && k <= '9') {
                deadbandEditValue += k;
            }
            else if (k == '#') {
                if (deadbandEditValue.length() == 0) break;

                int v = deadbandEditValue.toInt();
                if (v < 1) v = 1;
                if (v > 100) v = 100;

                deadbandF = v;
                eeprom_saveDeadband(v);

                deadbandEditValue = "";
                uiState = UI_SYSTEM;
            }
            else if (k == '*') {
                deadbandEditValue = "";
                uiState = UI_SYSTEM;
            }
            break;

        /* CLAMP MENU */
        case UI_CLAMP_MENU:
            if (k == '1') {
                clampMinEditValue = "";
                uiState = UI_CLAMP_MIN;
            }
            else if (k == '2') {
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MAX;
            }
            else if (k == '3') {
                deadzoneFanMode = deadzoneFanMode ? 0 : 1;
                eeprom_saveDeadzone(deadzoneFanMode);
            }
            else if (k == '4') {
                emberGuardianEditValue = "";
                uiState = UI_EMBER_GUARD_TIMER;
            }
            else if (k == '*') {
                uiState = UI_SYSTEM;
            }
            break;

        /* CLAMP MIN */
        case UI_CLAMP_MIN:
            if (k >= '0' && k <= '9') {
                clampMinEditValue += k;
            }
            else if (k == '#') {
                if (clampMinEditValue.length() == 0) break;

                int v = clampMinEditValue.toInt();
                if (v < 0) v = 0;
                if (v > 100) v = 100;

                clampMinPercent = v;
                eeprom_saveClampMin(v);

                clampMinEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k == '*') {
                clampMinEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        /* CLAMP MAX */
        case UI_CLAMP_MAX:
            if (k >= '0' && k <= '9') {
                clampMaxEditValue += k;
            }
            else if (k == '#') {
                if (clampMaxEditValue.length() == 0) break;

                int v = clampMaxEditValue.toInt();
                if (v < 0) v = 0;
                if (v > 100) v = 100;

                clampMaxPercent = v;
                eeprom_saveClampMax(v);

                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k == '*') {
                clampMaxEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        /* EMBER GUARD TIMER */
        case UI_EMBER_GUARD_TIMER:
            if (k >= '0' && k <= '9') {
                emberGuardianEditValue += k;
            }
            else if (k == '#') {
                if (emberGuardianEditValue.length() == 0) break;

                int v = emberGuardianEditValue.toInt();
                if (v < 5) v = 5;
                if (v > 60) v = 60;

                emberGuardianTimerMinutes = v;
                eeprom_saveEmberGuardMinutes(v);

                emberGuardianEditValue = "";
                flueLowEditValue = "";
                uiState = UI_FLUE_LOW;
            }
            else if (k == '*') {
                emberGuardianEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        /* FLUE LOW */
        case UI_FLUE_LOW:
            if (k >= '0' && k <= '9') {
                flueLowEditValue += k;
            }
            else if (k == '#') {
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
            else if (k == '*') {
                flueLowEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        /* FLUE RECOVERY */
        case UI_FLUE_REC:
            if (k >= '0' && k <= '9') {
                flueRecEditValue += k;
            }
            else if (k == '#') {
                if (flueRecEditValue.length() == 0) break;

                int v = flueRecEditValue.toInt();
                if (v < 50) v = 50;
                if (v > 900) v = 900;

                flueRecoveryThreshold = v;

                flueRecEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            else if (k == '*') {
                flueRecEditValue = "";
                uiState = UI_CLAMP_MENU;
            }
            break;

        /* WATER MENU */
        case UI_WATER_MENU:
            if (k == '1') {
                selectedRole = 0;
                selectedPhys = 0;
                uiState = UI_WATER_PROBE_MENU;
            }
            else if (k == '2') {
                uiState = UI_BME_SCREEN;
            }
            else if (k == '*') {
                uiState = UI_HOME;
            }
            break;

        /* WATER PROBE ROLE MENU */
        case UI_WATER_PROBE_MENU:
            if (k == '2') {
                if (probeCount > 0)
                    selectedPhys = (selectedPhys + 1) % probeCount;
            }
            else if (k == '8') {
                if (probeCount > 0) {
                    if (selectedPhys == 0) selectedPhys = probeCount - 1;
                    else selectedPhys--;
                }
            }
            else if (k == '4') {
                if (selectedRole == 0) selectedRole = PROBE_ROLE_COUNT - 1;
                else selectedRole--;
            }
            else if (k == '6') {
                selectedRole = (selectedRole + 1) % PROBE_ROLE_COUNT;
            }
            else if (k == '#') {
                if (selectedRole < PROBE_ROLE_COUNT && selectedPhys < MAX_WATER_PROBES) {
                    probeRoleMap[selectedRole] = selectedPhys;
                    eeprom_saveProbeRoles();
                }
            }
            else if (k == '*') {
                uiState = UI_WATER_MENU;
            }
            break;

        /* BME SCREEN */
        case UI_BME_SCREEN:
            if (k == '*') uiState = UI_WATER_MENU;
            break;

        /* ENVIRONMENT ROOT */
        case UI_ENV_MENU:
            if (k == '1') {
                uiEditSeason = ENV_SEASON_SUMMER;
                uiState = UI_SEASONS_MENU;
            }
            else if (k == '2') {
                uiState = UI_ENV_LOCKOUT;
            }
            else if (k == '*') {
                uiState = UI_SYSTEM;
            }
            break;

        /* SEASONS MENU */
        case UI_SEASONS_MENU:
            if (k == '1') {
                uiEditSeason = ENV_SEASON_SUMMER;
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '2') {
                uiEditSeason = ENV_SEASON_SPRINGFALL;
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '3') {
                uiEditSeason = ENV_SEASON_WINTER;
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '4') {
                uiEditSeason = ENV_SEASON_EXTREME;
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '*') {
                uiState = UI_ENV_MENU;
            }
            break;

        /* SEASON DETAIL MENU */
        case UI_SEASON_DETAIL_MENU:
            if (k == '1') {
                envSeasonEditValue = "";
                uiState = UI_SEASON_EDIT_START;
            }
            else if (k == '2') {
                envSeasonEditValue = "";
                uiState = UI_SEASON_EDIT_BUFFER;
            }
            else if (k == '3') {
                envSetpointEditValue = "";
                uiState = UI_SEASON_EDIT_SETPOINT;
            }
            else if (k == '*') {
                uiState = UI_SEASONS_MENU;
            }
            break;

        /* EDIT START TEMP */
        case UI_SEASON_EDIT_START:
            if (k >= '0' && k <= '9') {
                envSeasonEditValue += k;
            }
            else if (k == 'A') {
                if (envSeasonEditValue.startsWith("-"))
                    envSeasonEditValue.remove(0, 1);
                else
                    envSeasonEditValue = "-" + envSeasonEditValue;
            }
            else if (k == '#') {
                if (envSeasonEditValue.length() == 0) break;

                int v = envSeasonEditValue.toInt();
                if (v < -40) v = -40;
                if (v > 120) v = 120;

                *ui_getSeasonStartPtr(uiEditSeason) = v;
                eeprom_saveEnvSeasonStarts();

                envSeasonEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '*') {
                envSeasonEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            break;

        /* EDIT SEASON BUFFER */
        case UI_SEASON_EDIT_BUFFER:
            if (k >= '0' && k <= '9') {
                envSeasonEditValue += k;
            }
            else if (k == 'A') {
                if (envSeasonEditValue.startsWith("-"))
                    envSeasonEditValue.remove(0, 1);
                else
                    envSeasonEditValue = "-" + envSeasonEditValue;
            }
            else if (k == '#') {
                if (envSeasonEditValue.length() == 0) break;

                int v = envSeasonEditValue.toInt();
                if (v < 1) v = 1;
                if (v > 30) v = 30;

                *ui_getSeasonBufferPtr(uiEditSeason) = v;
                eeprom_saveEnvSeasonHyst();

                envSeasonEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '*') {
                envSeasonEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            break;

        /* EDIT SEASON SETPOINT */
        case UI_SEASON_EDIT_SETPOINT:
            if (k >= '0' && k <= '9') {
                envSetpointEditValue += k;
            }
            else if (k == 'A') {
                if (envSetpointEditValue.startsWith("-"))
                    envSetpointEditValue.remove(0, 1);
                else
                    envSetpointEditValue = "-" + envSetpointEditValue;
            }
            else if (k == '#') {
                if (envSetpointEditValue.length() == 0) break;

                int v = envSetpointEditValue.toInt();
                if (v < 200) v = 200;
                if (v > 900) v = 900;

                *ui_getSeasonSetpointPtr(uiEditSeason) = v;
                eeprom_saveEnvSeasonSetpoints();

                envSetpointEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            else if (k == '*') {
                envSetpointEditValue = "";
                uiState = UI_SEASON_DETAIL_MENU;
            }
            break;

        /* ENV LOCKOUT MENU */
        case UI_ENV_LOCKOUT:
            if (k == '1') {
                uiState = UI_ENV_MODE;
            }
            else if (k == '2') {
                uiState = UI_ENV_AUTOSEASON;
            }
            else if (k == '3') {
                envLockoutEditValue = "";
                uiState = UI_ENV_LOCKOUT_HOURS;
            }
            else if (k == '*') {
                uiState = UI_ENV_MENU;
            }
            break;

        /* ENV MODE */
        case UI_ENV_MODE:
            if (k == '1') {
                envSeasonMode = ENV_MODE_OFF;
                eeprom_saveEnvSeasonMode(envSeasonMode);
            }
            else if (k == '2') {
                envSeasonMode = ENV_MODE_USER;
                eeprom_saveEnvSeasonMode(envSeasonMode);
            }
            else if (k == '3') {
                envSeasonMode = ENV_MODE_AUTO;
                eeprom_saveEnvSeasonMode(envSeasonMode);
            }
            else if (k == '*') {
                uiState = UI_ENV_LOCKOUT;
            }
            break;

        /* AUTO SEASON */
        case UI_ENV_AUTOSEASON:
            if (k == '*') {
                envAutoSeasonEnabled = !envAutoSeasonEnabled;
                eeprom_saveEnvAutoSeason(envAutoSeasonEnabled);
            }
            else if (k == '#') {
                uiState = UI_ENV_LOCKOUT;
            }
            break;

        /* LOCKOUT HOURS */
        case UI_ENV_LOCKOUT_HOURS:
            if (k >= '0' && k <= '9') {
                envLockoutEditValue += k;
            }
            else if (k == '#') {
                if (envLockoutEditValue.length() == 0) break;

                int v = envLockoutEditValue.toInt();
                if (v < 1) v = 1;
                if (v > 24) v = 24;

                envModeLockoutSec = (uint32_t)v * 3600UL;
                eeprom_saveEnvLockoutHours((uint8_t)v);

                envLockoutEditValue = "";
                uiState = UI_ENV_LOCKOUT;
            }
            else if (k == '*') {
                envLockoutEditValue = "";
                uiState = UI_ENV_LOCKOUT;
            }
            break;

        default:
            uiState = UI_HOME;
            break;
    }
}
