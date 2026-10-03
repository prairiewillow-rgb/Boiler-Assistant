/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v3.3.7 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic, nonâ€‘blocking WiFi + HTTP JSON API subsystem
 *    for the UNO R4 WiFi. Implements the Total Domination
 *    Architecture (TDA) for all networkâ€‘side operator access.
 *
 *    Responsibilities:
 *      â€¢ Safe WiFi autoâ€‘retry (5s cooldown)
 *      â€¢ Minimal HTTP server on port 80
 *      â€¢ JSON endpoints:
 *          - GET  /api/state
 *          - GET  /api/settings
 *          - POST /api/set
 *      â€¢ Remote writeâ€‘back to SystemData with remoteChanged flag
 *
 *    Architectural Notes:
 *      - No blocking delays
 *      - No dynamic allocation beyond ArduinoJson buffers
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.3.7 "Total Domination"
 * ============================================================
 */

#include "WiFiAPI.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include "WiFiProvisioning.h"
#include "EEPROMStorage.h"
#include "ConfigValidation.h"
#include "DashboardHTML.h"
#include "BurnEngine.h"
#include "PushNotify.h"

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

extern SystemData sys;

/* ============================================================
 *  WiFi Credentials (from provisioning)
 * ============================================================ */

static const char* getWifiSSID() {
    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0)
        return runtimeCreds.ssid;
    return "";
}

static const char* getWifiPASS() {
    if (runtimeCreds.hasCredentials && runtimeCreds.pass[0] != 0)
        return runtimeCreds.pass;
    return "";
}

/* ============================================================
 *  HTTP Server
 * ============================================================ */

WiFiServer server(80);

/* ============================================================
 *  Retry Timer
 * ============================================================ */


/* ============================================================
 *  JSON Documents
 * ============================================================ */

static StaticJsonDocument<4096> stateDoc;
static StaticJsonDocument<512> settingsDoc;

/* ============================================================
 *  Helpers
 * ============================================================ */

static void sendJson(WiFiClient& client, const String& json) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print(json);
}

static void sendNotFound(WiFiClient& client) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
}

static void sendUnauthorized(WiFiClient& client) {
    client.println("HTTP/1.1 401 Unauthorized");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"authentication required\"}");
}

static bool hasApiToken(const String& headers) {
    if (runtimeCreds.controlPass[0] == '\0') return false;

    String expected = "X-Boiler-Token: ";
    expected += runtimeCreds.controlPass;
    return headers.indexOf(expected) >= 0;
}

static int requestContentLength(const String& headers) {
    int marker = headers.indexOf("Content-Length:");
    if (marker < 0) return 0;

    marker += 15;
    while (marker < headers.length() && headers[marker] == ' ') marker++;

    int end = headers.indexOf('\n', marker);
    if (end < 0) end = headers.length();
    return headers.substring(marker, end).toInt();
}

/* ============================================================
 *  JSON Builders
 * ============================================================ */

static String buildStateJson() {
    stateDoc.clear();

    const char* burnText =
        (sys.burnState == BURN_IDLE) ? "IDLE" :
        (sys.burnState == BURN_BOOST) ? "BOOST" :
        (sys.burnState == BURN_RAMP) ? "RAMP" :
        (sys.burnState == BURN_HOLD) ? "HOLD" :
        (sys.burnState == BURN_EMBER_GUARD) ? "EMBER_GUARD" : "UNKNOWN";
    const char* safetyText =
        (sys.safetyState == SAFETY_OK) ? "OK" :
        (sys.safetyState == SAFETY_HIGHTEMP) ? "HIGH_TEMP" : "SENSOR_FAULT";
    bool tankSensorOK = false;
    float tankTemp = NAN;
    if (sys.waterProbeCount > 0) {
        uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
        if (tankProbe < sys.waterProbeCount) {
            tankTemp = sys.waterTempF[tankProbe];
            tankSensorOK = !isnan(tankTemp) &&
                           millis() - sys.waterTempLastGoodMs[tankProbe] <= 3000UL;
        }
    }

    stateDoc["exhaust_raw"]   = sys.exhaustRawF;
    stateDoc["exhaust_smooth"] = sys.exhaustSmoothF;
    stateDoc["fan"]            = sys.fanFinal;
    stateDoc["fan_demand"]     = sys.fanDemand;
    stateDoc["fan_final"]      = sys.fanFinal;
    stateDoc["fan_pwm_percent"] = sys.fanFinal;
    stateDoc["adaptive_slope"] = burnengine_getAdaptiveSlope();
    stateDoc["burn_state"]     = sys.burnState;
    stateDoc["burn_state_text"] = burnText;
    stateDoc["safety_state"] = sys.safetyState;
    stateDoc["safety_text"] = safetyText;
    stateDoc["exhaust_fallback"] = sys.exhaustFallbackActive;
    const char* alertText = "";
    if (sys.exhaustFallbackActive) {
        alertText = "EXHAUST PROBE NEEDS CLEANING - FAN AT MAX";
    } else if (sys.emberGuardianLatched || sys.burnState == BURN_EMBER_GUARD) {
        alertText = "EMBER GUARDIAN ACTIVE - RESET REQUIRED";
    } else if (sys.safetyState == SAFETY_HIGHTEMP) {
        alertText = "HIGH TEMPERATURE LOCKOUT";
    } else if (sys.safetyState == SAFETY_SENSOR_FAULT) {
        alertText = (sys.sensorFaultMask & SENSOR_FAULT_TANK)
                        ? "TANK PROBE FAULT - SYSTEM STOPPED"
                        : "SENSOR FAULT - SYSTEM STOPPED";
    }
    stateDoc["alert"] = alertText;
    stateDoc["tank_temp"] = tankTemp;
    stateDoc["tank_sensor_ok"] = tankSensorOK;
    stateDoc["exhaust_sensor_ok"] = sys.exhaustSensorOK;
    stateDoc["wifi_ok"] = sys.wifiOK;
    stateDoc["display_name"] = runtimeCreds.displayName;
    stateDoc["tank_low"] = sys.tankLowSetpointF;
    stateDoc["tank_high"] = sys.tankHighSetpointF;
    stateDoc["self_clean_enabled"] = sys.selfCleanEnabled;
    stateDoc["self_clean_due"] = sys.selfCleanDue;
    stateDoc["self_clean_active"] = sys.selfCleanActive;
    stateDoc["self_clean_manual"] = sys.selfCleanManualRequested;
    stateDoc["self_clean_burn_count"] = sys.selfCleanBurnCount;
    stateDoc["self_clean_interval"] = sys.selfCleanIntervalBurns;
    stateDoc["push_enabled"]  = sys.pushEnabled;
    stateDoc["push_configured"] = sys.pushEnabled && sys.pushTopic[0] != '\0';

    stateDoc["rssi"]           = WiFi.RSSI();

    JsonObject env = stateDoc.createNestedObject("env");
    env["temp_f"]   = sys.envTempF;
    env["humidity"] = sys.envHumidity;
    env["pressure"] = sys.envPressure;
    env["units"]    = sys.envUnitsMetric ? "metric" : "imperial";

    JsonArray water = stateDoc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        JsonObject probe = water.createNestedObject();
        probe["index"] = i;
        probe["name"] = sys.waterProbeNames[i];
        probe["temp_f"] = sys.waterTempF[i];
        probe["sensor_ok"] = millis() - sys.waterTempLastGoodMs[i] <= 3000UL;
        probe["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
    }

    String out;
    serializeJson(stateDoc, out);
    return out;
}

static String buildHistoryJson() {
    String out;
    out.reserve(12000);
    out += "{\"history_now_min\":";
    out += millis() / 60000UL;
    out += ",\"burn_history\":[";
    for (uint8_t i = 0; i < sys.burnHistoryCount; i++) {
        if (i > 0) out += ',';
        out += "{\"duration_sec\":";
        out += sys.burnHistoryDurationSec[i];
        out += ",\"interval_sec\":";
        out += sys.burnHistoryIntervalSec[i];
        out += ",\"start_elapsed_min\":";
        out += sys.burnHistoryStartElapsedMin[i];
        out += ",\"water_temp_f\":";
        out += sys.burnHistoryWaterTempF[i];
        out += '}';
    }

    out += "],\"water_history\":[";
    for (uint16_t i = 0; i < sys.waterHistoryCount; i++) {
        uint16_t index = sys.waterHistoryCount < WATER_HISTORY_COUNT
                            ? i
                            : (sys.waterHistoryNext + i) % WATER_HISTORY_COUNT;
        if (i > 0) out += ',';
        out += sys.waterHistoryF[index];
    }

    out += "],\"water_history_minutes\":[";
    for (uint16_t i = 0; i < sys.waterHistoryCount; i++) {
        uint16_t index = sys.waterHistoryCount < WATER_HISTORY_COUNT
                            ? i
                            : (sys.waterHistoryNext + i) % WATER_HISTORY_COUNT;
        if (i > 0) out += ',';
        out += sys.waterHistoryElapsedMin[index];
    }
    out += "]}";
    return out;
}

static String buildSettingsJson() {
    settingsDoc.clear();

    settingsDoc["exhaust_setpoint"] = sys.exhaustSetpoint;
    settingsDoc["deadband"]         = sys.deadbandF;
    settingsDoc["boost_time"]       = sys.boostTimeSeconds;
    settingsDoc["clamp_min"]        = sys.clampMinPercent;
    settingsDoc["clamp_max"]        = sys.clampMaxPercent;
    settingsDoc["deadzone_fan"]     = sys.deadzoneFanMode;
    settingsDoc["ember_minutes"]    = sys.emberGuardianTimerMinutes;
    settingsDoc["flue_low"]         = sys.flueLowThreshold;
    settingsDoc["flue_recovery"]    = sys.flueRecoveryThreshold;
    settingsDoc["tank_low"]          = sys.tankLowSetpointF;
    settingsDoc["tank_high"]         = sys.tankHighSetpointF;
    settingsDoc["env_units"]         = sys.envUnitsMetric;
    settingsDoc["self_clean_enabled"] = sys.selfCleanEnabled;
    settingsDoc["self_clean_interval"] = sys.selfCleanIntervalBurns;
    settingsDoc["self_clean_start_hour"] = sys.selfCleanStartHour;
    settingsDoc["self_clean_end_hour"] = sys.selfCleanEndHour;
    settingsDoc["self_clean_utc_offset"] = sys.selfCleanUtcOffsetMinutes;
    settingsDoc["self_clean_dst"] = sys.selfCleanDstEnabled;
    settingsDoc["push_enabled"]  = sys.pushEnabled;
    settingsDoc["push_topic"]    = sys.pushTopic;
    String out;
    serializeJson(settingsDoc, out);
    return out;
}

/* ============================================================
 *  POST /api/set
 * ============================================================ */

static void handleApiSet(WiFiClient& client, const String& body) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendJson(client, "{\"error\":\"invalid JSON\"}");
        return;
    }

    int exhaustSetpoint = sys.exhaustSetpoint;
    int deadband = sys.deadbandF;
    int boostTime = sys.boostTimeSeconds;
    int clampMin = sys.clampMinPercent;
    int clampMax = sys.clampMaxPercent;
    int deadzoneFan = sys.deadzoneFanMode;
    int guardianMinutes = sys.emberGuardianTimerMinutes;
    int flueLow = sys.flueLowThreshold;
    int flueRecovery = sys.flueRecoveryThreshold;
    int tankLow = sys.tankLowSetpointF;
    int tankHigh = sys.tankHighSetpointF;
    int envUnits = sys.envUnitsMetric;
    bool selfCleanEnabled = sys.selfCleanEnabled;
    int selfCleanInterval = sys.selfCleanIntervalBurns;
    int selfCleanStartHour = sys.selfCleanStartHour;
    int selfCleanEndHour = sys.selfCleanEndHour;
    int selfCleanUtcOffset = sys.selfCleanUtcOffsetMinutes;
    bool selfCleanDst = sys.selfCleanDstEnabled;
    bool selfCleanRunNow = false;

    if (doc.containsKey("exhaust_setpoint")) exhaustSetpoint = doc["exhaust_setpoint"];
    if (doc.containsKey("deadband")) deadband = doc["deadband"];
    if (doc.containsKey("boost_time")) boostTime = doc["boost_time"];
    if (doc.containsKey("clamp_min")) clampMin = doc["clamp_min"];
    if (doc.containsKey("clamp_max")) clampMax = doc["clamp_max"];
    if (doc.containsKey("deadzone_fan")) deadzoneFan = doc["deadzone_fan"];
    if (doc.containsKey("ember_minutes")) guardianMinutes = doc["ember_minutes"];
    if (doc.containsKey("flue_low")) flueLow = doc["flue_low"];
    if (doc.containsKey("flue_recovery")) flueRecovery = doc["flue_recovery"];
    if (doc.containsKey("tank_low")) tankLow = doc["tank_low"];
    if (doc.containsKey("tank_high")) tankHigh = doc["tank_high"];
    if (doc.containsKey("env_units")) envUnits = doc["env_units"];
    if (doc.containsKey("self_clean_enabled")) selfCleanEnabled = doc["self_clean_enabled"];
    if (doc.containsKey("self_clean_interval")) selfCleanInterval = doc["self_clean_interval"];
    if (doc.containsKey("self_clean_start_hour")) selfCleanStartHour = doc["self_clean_start_hour"];
    if (doc.containsKey("self_clean_end_hour")) selfCleanEndHour = doc["self_clean_end_hour"];
    if (doc.containsKey("self_clean_utc_offset")) selfCleanUtcOffset = doc["self_clean_utc_offset"];
    if (doc.containsKey("self_clean_dst")) selfCleanDst = doc["self_clean_dst"];
    if (doc.containsKey("self_clean_run_now")) selfCleanRunNow = doc["self_clean_run_now"];
    if (!validExhaustSetpoint(exhaustSetpoint) ||
        !validDeadband(deadband) ||
        !validBoostTime(boostTime) ||
        !validFanClamp(clampMin) ||
        !validFanClamp(clampMax) ||
        clampMin > clampMax ||
        (deadzoneFan != 0 && deadzoneFan != 1) ||
        !validGuardianMinutes(guardianMinutes) ||
        !validFlueThreshold(flueLow) ||
        !validFlueThreshold(flueRecovery) ||
        flueRecovery < flueLow ||
        !validTankSetpoint(tankLow) ||
        !validTankSetpoint(tankHigh) ||
        tankLow >= tankHigh ||
        tankHigh >= 190 ||
        (envUnits != 0 && envUnits != 1) ||
        !validSelfCleanInterval(selfCleanInterval) ||
        !validSelfCleanHour(selfCleanStartHour) ||
        !validSelfCleanHour(selfCleanEndHour) ||
        !validSelfCleanUtcOffset(selfCleanUtcOffset)) {
        sendJson(client, "{\"error\":\"configuration value out of range\"}");
        return;
    }

    bool changed = false;

    if (doc.containsKey("exhaust_setpoint")) {
        sys.exhaustSetpoint = exhaustSetpoint;
        eeprom_saveSetpoint(exhaustSetpoint);
        changed = true;
    }
    if (doc.containsKey("deadband")) {
        sys.deadbandF = deadband;
        eeprom_saveDeadband(deadband);
        changed = true;
    }
    if (doc.containsKey("boost_time")) {
        sys.boostTimeSeconds = boostTime;
        eeprom_saveBoostTime(boostTime);
        changed = true;
    }
    if (doc.containsKey("clamp_min")) {
        sys.clampMinPercent = clampMin;
        eeprom_saveClampMin(clampMin);
        changed = true;
    }
    if (doc.containsKey("clamp_max")) {
        sys.clampMaxPercent = clampMax;
        eeprom_saveClampMax(clampMax);
        changed = true;
    }
    if (doc.containsKey("deadzone_fan")) {
        sys.deadzoneFanMode = deadzoneFan;
        eeprom_saveDeadzone(deadzoneFan);
        changed = true;
    }
    if (doc.containsKey("ember_minutes")) {
        sys.emberGuardianTimerMinutes = guardianMinutes;
        eeprom_saveEmberGuardianMinutes(guardianMinutes);
        changed = true;
    }
    if (doc.containsKey("flue_low")) {
        sys.flueLowThreshold = flueLow;
        eeprom_saveFlueLow(flueLow);
        changed = true;
    }
    if (doc.containsKey("flue_recovery")) {
        sys.flueRecoveryThreshold = flueRecovery;
        eeprom_saveFlueRecovery(flueRecovery);
        changed = true;
    }
    if (doc.containsKey("tank_low")) {
        sys.tankLowSetpointF = tankLow;
        eeprom_saveTankLow(tankLow);
        changed = true;
    }
    if (doc.containsKey("tank_high")) {
        sys.tankHighSetpointF = tankHigh;
        eeprom_saveTankHigh(tankHigh);
        changed = true;
    }
    if (doc.containsKey("env_units")) {
        sys.envUnitsMetric = (uint8_t)envUnits;
        eeprom_saveEnvUnits(sys.envUnitsMetric);
        sys.uiNeedsRefresh = true;
        changed = true;
    }
    if (doc.containsKey("self_clean_enabled") ||
        doc.containsKey("self_clean_interval") ||
        doc.containsKey("self_clean_start_hour") ||
        doc.containsKey("self_clean_end_hour") ||
        doc.containsKey("self_clean_utc_offset") ||
        doc.containsKey("self_clean_dst")) {
        sys.selfCleanEnabled = selfCleanEnabled;
        sys.selfCleanIntervalBurns = (uint16_t)selfCleanInterval;
        sys.selfCleanStartHour = (uint8_t)selfCleanStartHour;
        sys.selfCleanEndHour = (uint8_t)selfCleanEndHour;
        sys.selfCleanUtcOffsetMinutes = (int16_t)selfCleanUtcOffset;
        sys.selfCleanDstEnabled = selfCleanDst;
        if (!sys.selfCleanEnabled) {
            sys.selfCleanDue = false;
            sys.selfCleanActive = false;
            sys.selfCleanManualRequested = false;
            sys.selfCleanBurnCount = 0;
        }
        eeprom_saveSelfClean();
        changed = true;
    }
    if (selfCleanRunNow && sys.selfCleanEnabled) {
        sys.selfCleanManualRequested = true;
        changed = true;
    }

    // === PUSH NOTIFICATIONS ===
    bool pushChanged = false;
    if (doc.containsKey("push_enabled")) {
        sys.pushEnabled = (doc["push_enabled"] | 0) != 0;
        pushChanged = true;
    }
    if (doc.containsKey("push_topic")) {
        const char* t = doc["push_topic"] | "";
        char clean[33];
        uint8_t n = 0;
        for (const char* p = t; *p && n < 32; p++) {
            char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_') {
                clean[n++] = c;
            }
        }
        clean[n] = '\0';
        strncpy(sys.pushTopic, clean, sizeof(sys.pushTopic) - 1);
        sys.pushTopic[sizeof(sys.pushTopic) - 1] = '\0';
        pushChanged = true;
    }
    if (pushChanged) {
        if (sys.pushTopic[0] == '\0') sys.pushEnabled = false;
        eeprom_savePushNotify();
        changed = true;
    }

    if (changed) {
        sys.remoteChanged = true;
    }

    sendJson(client, "{\"ok\":true}");
}

static void handleApiProbe(WiFiClient& client, const String& body) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, body)) {
        StaticJsonDocument<1024> doc;
        return;
    }

    int index = doc["index"] | -1;
    const char* name = doc["name"] | "";
    if (index < 0 || index >= MAX_WATER_PROBES || name[0] == '\0') {
        sendJson(client, "{\"error\":\"invalid probe name\"}");
        return;
    }

    strncpy(sys.waterProbeNames[index], name, PROBE_NAME_LENGTH - 1);
    sys.waterProbeNames[index][PROBE_NAME_LENGTH - 1] = '\0';
    eeprom_saveProbeName((uint8_t)index);
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

static void handleApiReset(WiFiClient& client) {
    if (sys.safetyState == SAFETY_HIGHTEMP) {
        sendJson(client, "{\"error\":\"high-temperature lockout requires local reset\"}");
        return;
    }
    burnengine_resetAlarms();
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

/* ============================================================
 *  WiFi Init (provisioning-aware)
 * ============================================================ */

void wifiapi_init() {
    if (wifi_prov_isAPMode()) {
        Serial.println("WiFiAPI: skipped (AP mode active)");
        return;
    }

    Serial.println("WiFiAPI: init");

    const char* ssid = getWifiSSID();
    const char* pass = getWifiPASS();

    if (ssid[0] == 0) {
        Serial.println("WiFiAPI: no credentials â†’ skipping");
        sys.wifiOK = false;
        return;
    }

    // WiFiProvisioning owns connection setup. Starting WiFi again here can
    // block the control loop even though provisioning already connected.
    server.begin();
}

void wifiapi_stop() {
    server.end();
}

/* ============================================================
 *  WiFi Loop
 * ============================================================ */

void wifiapi_loop() {
    if (wifi_prov_isAPMode()) {
        sys.wifiOK = false;
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        sys.wifiOK = false;
        return;
    }

    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        sys.wifiOK = false;
        return;
    }

    sys.wifiOK = true;

    static bool printed = false;
    if (!printed) {
        printed = true;
        Serial.print("WiFiAPI: WiFi connected. IP: ");
        Serial.println(ip);
    }

    WiFiClient client = server.available();
    if (!client) return;

    if (!client.available()) {
        client.stop();
        return;
    }

    client.setTimeout(5);
    String req = client.readStringUntil('\r');
    client.readStringUntil('\n');

    String headers;
    while (client.available()) {
        String headerLine = client.readStringUntil('\n');
        headers += headerLine;
        if (headerLine == "\r" || headerLine.length() == 0) break;
    }

    String body = "";
    if (req.startsWith("POST")) {
        int contentLength = requestContentLength(headers);
        unsigned long bodyStart = millis();

        while ((contentLength == 0 || body.length() < contentLength) &&
               millis() - bodyStart < 25UL) {
            while (client.available() &&
                   (contentLength == 0 || body.length() < contentLength)) {
                body += (char)client.read();
            }
        }
    }

    if (req.startsWith("GET / HTTP") || req.startsWith("GET /dashboard")) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Cache-Control: no-store");
        client.println("Connection: close");
        client.println();
        client.write((const uint8_t*)DASHBOARD_HTML, strlen_P(DASHBOARD_HTML));
    }
    else if (req.startsWith("GET /api/state")) {
        sendJson(client, buildStateJson());
    }
    else if (req.startsWith("GET /api/history")) {
        sendJson(client, buildHistoryJson());
    }
    else if (req.startsWith("GET /api/settings")) {
        sendJson(client, buildSettingsJson());
    }
    else if (req.startsWith("POST /api/set")) {
        if (hasApiToken(headers)) {
            handleApiSet(client, body);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/probe")) {
        if (hasApiToken(headers)) {
            handleApiProbe(client, body);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/reset")) {
        if (hasApiToken(headers)) {
            handleApiReset(client);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/push_test")) {
        if (hasApiToken(headers)) {
            bool ok = pushnotify_sendTest();
            sendJson(client, ok ? "{\"ok\":true}" : "{\"error\":\"push failed - check topic and WiFi\"}");
        } else {
            sendUnauthorized(client);
        }
    }
    else {
        sendNotFound(client);
    }

    client.stop();
}
