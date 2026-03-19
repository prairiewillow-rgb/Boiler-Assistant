/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v3.0 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic, non‑blocking WiFi + HTTP JSON API subsystem
 *    for the UNO R4 WiFi. Implements the Total Domination
 *    Architecture (TDA) for all network‑side operator access.
 *
 *    Responsibilities:
 *      • Safe WiFi auto‑retry (5s cooldown)
 *      • Minimal HTTP server on port 80
 *      • JSON endpoints:
 *          - GET  /api/state
 *          - GET  /api/settings
 *          - POST /api/set
 *      • Remote write‑back to SystemData with remoteChanged flag
 *
 *    Architectural Notes:
 *      - No blocking delays
 *      - No dynamic allocation beyond ArduinoJson buffers
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.0 "Total Domination"
 * ============================================================
 */

#include "WiFiAPI.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include "WiFiProvisioning.h"

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

static unsigned long lastWifiAttempt = 0;

/* ============================================================
 *  JSON Documents
 * ============================================================ */

static StaticJsonDocument<512> stateDoc;
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

/* ============================================================
 *  JSON Builders
 * ============================================================ */

static String buildStateJson() {
    stateDoc.clear();

    stateDoc["exhaust_smooth"] = sys.exhaustSmoothF;
    stateDoc["fan"]            = sys.fanFinal;
    stateDoc["burn_state"]     = sys.burnState;

    stateDoc["rssi"]           = WiFi.RSSI();

    JsonObject env = stateDoc.createNestedObject("env");
    env["temp_f"]   = sys.envTempF;
    env["humidity"] = sys.envHumidity;
    env["pressure"] = sys.envPressure;

    JsonArray water = stateDoc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        water.add(sys.waterTempF[i]);
    }

    String out;
    serializeJson(stateDoc, out);
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

    String out;
    serializeJson(settingsDoc, out);
    return out;
}

/* ============================================================
 *  POST /api/set
 * ============================================================ */

static void handleApiSet(WiFiClient& client, const String& body) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendJson(client, "{\"error\":\"invalid JSON\"}");
        return;
    }

    bool changed = false;

    if (doc.containsKey("exhaust_setpoint")) {
        sys.exhaustSetpoint = doc["exhaust_setpoint"];
        changed = true;
    }
    if (doc.containsKey("deadband")) {
        sys.deadbandF = doc["deadband"];
        changed = true;
    }
    if (doc.containsKey("boost_time")) {
        sys.boostTimeSeconds = doc["boost_time"];
        changed = true;
    }
    if (doc.containsKey("clamp_min")) {
        sys.clampMinPercent = doc["clamp_min"];
        changed = true;
    }
    if (doc.containsKey("clamp_max")) {
        sys.clampMaxPercent = doc["clamp_max"];
        changed = true;
    }
    if (doc.containsKey("deadzone_fan")) {
        sys.deadzoneFanMode = doc["deadzone_fan"];
        changed = true;
    }
    if (doc.containsKey("ember_minutes")) {
        sys.emberGuardianTimerMinutes = doc["ember_minutes"];
        changed = true;
    }
    if (doc.containsKey("flue_low")) {
        sys.flueLowThreshold = doc["flue_low"];
        changed = true;
    }
    if (doc.containsKey("flue_recovery")) {
        sys.flueRecoveryThreshold = doc["flue_recovery"];
        changed = true;
    }

    if (changed) {
        sys.remoteChanged = true;
    }

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
        Serial.println("WiFiAPI: no credentials → skipping");
        sys.wifiOK = false;
        return;
    }

    Serial.print("WiFiAPI: connecting to SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    lastWifiAttempt = millis();

    server.begin();
}

/* ============================================================
 *  WiFi Loop (non‑blocking auto‑retry)
 * ============================================================ */

void wifiapi_loop() {

    if (wifi_prov_isAPMode()) {
        sys.wifiOK = false;
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        sys.wifiOK = false;

        unsigned long now = millis();

        if (now - lastWifiAttempt > 5000) {
            lastWifiAttempt = now;

            const char* ssid = getWifiSSID();
            const char* pass = getWifiPASS();

            if (ssid[0] == 0) return;

            Serial.print("WiFiAPI: retrying SSID ");
            Serial.println(ssid);

            WiFi.begin(ssid, pass);
        }

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

    String req = client.readStringUntil('\r');
    client.readStringUntil('\n');

    String body = "";
    if (req.startsWith("POST")) {
        while (client.available()) {
            body += client.readString();
        }
    }

    if (req.startsWith("GET /api/state")) {
        sendJson(client, buildStateJson());
    }
    else if (req.startsWith("GET /api/settings")) {
        sendJson(client, buildSettingsJson());
    }
    else if (req.startsWith("POST /api/set")) {
        handleApiSet(client, body);
    }
    else {
        sendNotFound(client);
    }

    client.stop();
}
