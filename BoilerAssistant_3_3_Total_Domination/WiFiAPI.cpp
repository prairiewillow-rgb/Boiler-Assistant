/*
 * ============================================================
 *  Boiler Assistant - WiFi JSON API Module (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Incrementally serviced WiFi + HTTP JSON API subsystem
 *    for the UNO R4 WiFi. Implements the Total Domination
 *    Architecture (TDA) for all network-side operator access.
 *
 *    Responsibilities:
 *      - Safe WiFi auto-retry (5s cooldown)
 *      - Minimal HTTP server on port 80
 *      - JSON endpoints:
 *          - GET  /api/state
 *          - GET  /api/settings
 *          - POST /api/set
 *      - Remote write-back to SystemData with remoteChanged flag
 *
 *    Architectural Notes:
 *      - Up to 256 request bytes or 512 response bytes per loop pass
 *      - 1 KiB header/body limits; 2s receive and 12s send deadlines
 *      - WiFiS3 modem operations themselves are still synchronous
 *      - String storage is released between requests
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
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
#include "RuntimeWiFiClient.h"

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <utility>

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

enum HttpPhase { HTTP_IDLE, HTTP_RECEIVING, HTTP_SENDING };
static HttpPhase httpPhase = HTTP_IDLE;
static RuntimeWiFiClient<WiFiClient> httpClient;
static String httpHeaders;
static String httpBody;
static char httpResponseHeaders[256];
static size_t httpResponseHeaderLength = 0;
static String httpResponseBody;
static const char* httpResponseData = nullptr;
static size_t httpResponseLength = 0;
static size_t httpResponseOffset = 0;
static size_t httpHeaderOffset = 0;
static bool httpHeadersComplete = false;
static int httpContentLength = 0;
static unsigned long httpStartedMs = 0;
static unsigned long httpLastWriteMs = 0;
static unsigned long httpLastReadMs = 0;
static const size_t HTTP_HEADER_LIMIT = 1024;
static const size_t HTTP_BODY_LIMIT = 1024;
static const size_t HTTP_READ_CHUNK = 256;
static const size_t HTTP_WRITE_CHUNK = 512;
static const unsigned long HTTP_RECEIVE_TIMEOUT_MS = 2000UL;
static const unsigned long HTTP_SEND_TIMEOUT_MS = 12000UL;

static void closeHttp() {
    httpClient.stop();
    httpHeaders = String();
    httpBody = String();
    httpResponseHeaderLength = 0;
    httpResponseBody = String();
    httpResponseData = nullptr;
    httpPhase = HTTP_IDLE;
}

static void queueResponse(const char* status, const char* contentType,
                          const char* data, size_t length) {
    int headerLength = snprintf(httpResponseHeaders, sizeof(httpResponseHeaders),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
        "Connection: close\r\nContent-Length: %u\r\n\r\n",
        status, contentType, static_cast<unsigned int>(length));
    if (headerLength < 0 || static_cast<size_t>(headerLength) >= sizeof(httpResponseHeaders)) {
        Serial.println("WiFiAPI: response headers exceeded buffer");
        closeHttp();
        return;
    }
    httpResponseHeaderLength = headerLength;
    httpResponseData = data;
    httpResponseLength = length;
    httpResponseOffset = httpHeaderOffset = 0;
    httpStartedMs = millis();
    httpLastWriteMs = millis() - 10UL;
    httpPhase = HTTP_SENDING;
}

static void queueJson(const char* status, String json) {
    if (!json) {
        Serial.println("WiFiAPI: response allocation failed");
        static const char error[] = "{\"error\":\"insufficient memory\"}";
        queueResponse("503 Service Unavailable", "application/json", error, sizeof(error) - 1);
        return;
    }
    httpResponseBody = std::move(json);
    queueResponse(status, "application/json",
                  httpResponseBody.c_str(), httpResponseBody.length());
}

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

static void sendJson(WiFiClient&, String json) {
    queueJson("200 OK", std::move(json));
}

static void sendNotFound(WiFiClient&) {
    queueJson("404 Not Found", "{\"error\":\"not found\"}");
}

static void sendUnauthorized(WiFiClient&) {
    queueJson("401 Unauthorized", "{\"error\":\"authentication required\"}");
}

static bool hasApiToken(const String& headers) {
    if (runtimeCreds.controlPass[0] == '\0') return false;

    String expected = "X-Boiler-Token: ";
    expected += runtimeCreds.controlPass;
    return headers.indexOf(expected) >= 0;
}

static int requestContentLength(const String& headers) {
    int length = 0;
    bool found = false;
    int start = headers.indexOf("\r\n") + 2;
    while (start >= 2 && start < (int)headers.length()) {
        int end = headers.indexOf("\r\n", start);
        if (end < 0 || end == start) break;
        String line = headers.substring(start, end);
        line.toLowerCase();
        if (line.startsWith("transfer-encoding:")) return -1;
        if (line.startsWith("content-length:")) {
            if (found) return -1;
            found = true;
            String value = line.substring(15);
            value.trim();
            if (value.length() == 0) return -1;
            for (unsigned int i = 0; i < value.length(); ++i) {
                if (value[i] < '0' || value[i] > '9') return -1;
                length = length * 10 + value[i] - '0';
                if (length > (int)HTTP_BODY_LIMIT) return -1;
            }
        }
        start = end + 2;
    }
    return length;
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
    stateDoc["push_status"] = pushnotify_status();
    stateDoc["loop_last_ms"] = sys.loopLastMs;
    stateDoc["loop_max_ms"] = sys.loopMaxMs;
    stateDoc["network_last_ms"] = sys.networkLastMs;
    stateDoc["network_max_ms"] = sys.networkMaxMs;

    if (sys.wifiOK && wifi_prov_cachedRSSI() != 0) {
        stateDoc["rssi"] = wifi_prov_cachedRSSI();
    } else {
        stateDoc["rssi"] = nullptr;
    }

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
    if (!out.reserve(96 + sys.burnHistoryCount * 120 +
                     sys.waterHistoryCount * 12)) {
        Serial.println("WiFiAPI: insufficient memory for history response");
        return "{\"error\":\"insufficient memory for history\"}";
    }
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
    if (!sys.wifiOK || !wifi_prov_prepareIo()) return;

    // WiFiProvisioning owns connection setup. Starting WiFi again here can
    // block the control loop even though provisioning already connected.
    unsigned long startedMs = millis();
    server.begin();
    wifi_prov_finishIo(startedMs, "HTTP server start");
}

void wifiapi_stop() {
    if (httpPhase != HTTP_IDLE) closeHttp();
    if (wifi_prov_prepareClientIo()) {
        unsigned long startedMs = millis();
        server.end();
        wifi_prov_finishIo(startedMs, "HTTP server close");
    }
}

/* ============================================================
 *  WiFi Loop
 * ============================================================ */

static void dispatchHttp() {
    String req = httpHeaders.substring(0, httpHeaders.indexOf("\r\n"));
    WiFiClient& client = httpClient;
    const String& headers = httpHeaders;
    const String& body = httpBody;
    if (req.startsWith("GET / HTTP") || req.startsWith("GET /dashboard")) {
        queueResponse("200 OK", "text/html; charset=utf-8",
                      DASHBOARD_HTML, strlen_P(DASHBOARD_HTML));
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
            sendJson(client, ok ? "{\"ok\":true,\"queued\":true}"
                                : "{\"error\":\"push not queued - check topic, WiFi and queue status\"}");
        } else {
            sendUnauthorized(client);
        }
    }
    else {
        sendNotFound(client);
    }

    httpHeaders = String();
    httpBody = String();
}

void wifiapi_loop() {
    if (wifi_prov_isAPMode()) {
        sys.wifiOK = false;
        if (httpPhase != HTTP_IDLE) closeHttp();
        return;
    }

    unsigned long now = millis();
    if (!sys.wifiOK) {
        if (httpPhase != HTTP_IDLE) closeHttp();
        return;
    }

    if (httpPhase == HTTP_IDLE) {
        static unsigned long lastAcceptMs = 0;
        if (now - lastAcceptMs < 20UL) return;
        lastAcceptMs = now;
        unsigned long startedMs = millis();
        httpClient = server.available();
        wifi_prov_finishIo(startedMs, "HTTP accept");
        if (!wifi_prov_prepareIo()) {
            httpClient.stop();
            return;
        }
        if (!httpClient) return;
        now = millis();
        httpHeadersComplete = false;
        httpContentLength = 0;
        httpStartedMs = now;
        httpLastReadMs = now - 20UL;
        httpPhase = HTTP_RECEIVING;
    }

    if (httpPhase == HTTP_SENDING) {
        if (now - httpStartedMs >= HTTP_SEND_TIMEOUT_MS) {
            Serial.println("WiFiAPI: response timed out; closing client");
            closeHttp();
            return;
        }
        if (now - httpLastWriteMs < 10UL) return;
        httpLastWriteMs = now;
        bool sendingHeaders = httpHeaderOffset < httpResponseHeaderLength;
        const char* data = sendingHeaders ? httpResponseHeaders : httpResponseData;
        size_t& offset = sendingHeaders ? httpHeaderOffset : httpResponseOffset;
        size_t length = sendingHeaders ? httpResponseHeaderLength : httpResponseLength;
        size_t count = min(HTTP_WRITE_CHUNK, length - offset);
        if (count > 0) {
            size_t written = httpClient.write(
                reinterpret_cast<const uint8_t*>(data + offset), count);
            if (written == 0) {
                Serial.println("WiFiAPI: response write failed; closing client");
                closeHttp();
                return;
            }
            offset += written;
        }
        if (httpHeaderOffset == httpResponseHeaderLength &&
            httpResponseOffset == httpResponseLength) closeHttp();
        return;
    }

    if (now - httpStartedMs >= HTTP_RECEIVE_TIMEOUT_MS) {
        Serial.println("WiFiAPI: incomplete request timed out");
        queueJson("408 Request Timeout", "{\"error\":\"request timed out\"}");
        return;
    }

    if (now - httpLastReadMs < 20UL) return;
    httpLastReadMs = now;
    int available = httpClient.available();
    if (available <= 0) return;
    uint8_t buffer[HTTP_READ_CHUNK];
    size_t count = min(HTTP_READ_CHUNK, static_cast<size_t>(available));
    int received = httpClient.read(buffer, count);
    for (int i = 0; i < received; ++i) {
        if (!httpHeadersComplete) {
            if (httpHeaders.length() >= HTTP_HEADER_LIMIT ||
                !httpHeaders.concat(static_cast<char>(buffer[i]))) {
                Serial.println("WiFiAPI: headers too large or allocation failed");
                queueJson("431 Request Header Fields Too Large",
                          "{\"error\":\"headers too large\"}");
                return;
            }
            if (httpHeaders.endsWith("\r\n\r\n")) {
                httpHeadersComplete = true;
                httpContentLength = requestContentLength(httpHeaders);
                if (httpContentLength < 0) {
                    Serial.println("WiFiAPI: unsupported or oversized request body");
                    queueJson("400 Bad Request", "{\"error\":\"invalid content length or encoding\"}");
                    return;
                }
            }
        } else if (httpBody.length() < static_cast<unsigned int>(httpContentLength)) {
            if (!httpBody.concat(static_cast<char>(buffer[i]))) {
                Serial.println("WiFiAPI: request body allocation failed");
                queueJson("503 Service Unavailable", "{\"error\":\"insufficient memory\"}");
                return;
            }
        }
    }
    if (httpHeadersComplete &&
        httpBody.length() == static_cast<unsigned int>(httpContentLength)) dispatchHttp();
}
