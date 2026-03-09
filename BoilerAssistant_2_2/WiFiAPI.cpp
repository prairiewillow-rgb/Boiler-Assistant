/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v2.2)
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Lightweight, non‑blocking WiFi + HTTP JSON API for UNO R4 WiFi.
 *      Responsibilities:
 *        • Safe WiFi auto‑retry (5s cooldown)
 *        • Minimal HTTP server on port 80
 *        • JSON endpoints:
 *            - GET /api/state
 *            - GET /api/settings
 *            - POST /api/set
 *        • Remote write‑back to SystemData with remoteChanged flag
 *
 *      Notes:
 *        - Designed to run alongside MQTT and LoRa without blocking.
 *        - Called once per second from main loop.
 *        - Uses ArduinoJson with static global documents to avoid
 *          stack pressure and timing jitter.
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include "WiFiAPI.h"
#include "SystemData.h"

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

extern SystemData sys;

/* ============================================================
 *  WiFi Credentials
 * ============================================================ */

static const char* WIFI_SSID = SECRET_SSID;
static const char* WIFI_PASS = SECRET_PASS;

/* ============================================================
 *  HTTP Server
 * ============================================================ */

WiFiServer server(80);

/* ============================================================
 *  Retry Timer
 * ============================================================ */

static unsigned long lastWifiAttempt = 0;

/* ============================================================
 *  JSON Documents (static, global – no stack pressure)
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

    // WiFi RSSI
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
    settingsDoc["coalbed_minutes"]  = sys.coalBedTimerMinutes;
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
    StaticJsonDocument<256> doc;  // small, local, infrequent
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
    if (doc.containsKey("coalbed_minutes")) {
        sys.coalBedTimerMinutes = doc["coalbed_minutes"];
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
 *  WiFi Init (non‑blocking)
 * ============================================================ */

void wifiapi_init() {
    Serial.println("WiFiAPI: init");

    Serial.print("Connecting to SSID: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastWifiAttempt = millis();

    server.begin();
}

/* ============================================================
 *  WiFi Loop (non‑blocking auto‑retry)
 * ============================================================ */

void wifiapi_loop() {

    // Retry WiFi every 5 seconds if disconnected
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();

        if (now - lastWifiAttempt > 5000) {
            lastWifiAttempt = now;
            Serial.println("WiFi: retrying...");
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }

        return; // skip API handling until connected
    }

    // Connected: ensure DHCP has assigned a real IP
    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        // WiFi associated but DHCP not done yet – do nothing, stay non-blocking
        return;
    }

    // Print IP once, after it's valid
    static bool printed = false;
    if (!printed) {
        printed = true;
        Serial.print("WiFi connected. IP: ");
        Serial.println(ip);
    }

    // Handle HTTP requests (fully non-blocking)
    WiFiClient client = server.available();
    if (!client) return;

    // If no data is available immediately, drop the client – never wait
    if (!client.available()) {
        client.stop();
        return;
    }

    String req = client.readStringUntil('\r');
    client.readStringUntil('\n');

    String body = "";
    if (req.startsWith("POST")) {
        // Read whatever is already available; do not block waiting
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
