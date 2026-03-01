/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v2.0)
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      HTTP JSON API for dashboards and remote control.
 *      Compatible with Arduino UNO R4 WiFi (WiFiS3 stack).
 *
 *      Endpoints:
 *        • GET  /api/state
 *        • GET  /api/settings
 *        • POST /api/set   (JSON body)
 *
 *      Uses manual HTTP parsing because UNO R4 WiFi does not
 *      support WebServer.h (ESP-only).
 *
 * ============================================================
 */

#include "WiFiAPI.h"
#include "SystemData.h"

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

extern SystemData sys;

// ============================================================
//  WiFi Credentials (replace with your own)
// ============================================================
static const char* WIFI_SSID = "YOUR_SSID";
static const char* WIFI_PASS = "YOUR_PASSWORD";

// ============================================================
//  HTTP Server
// ============================================================
WiFiServer server(80);

// ============================================================
//  Helpers
// ============================================================
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

// ============================================================
//  JSON Builders
// ============================================================
static String buildStateJson() {
    StaticJsonDocument<256> doc;

    doc["exhaust_smooth"] = sys.exhaustSmoothF;
    doc["fan"] = sys.fanFinal;
    doc["burn_state"] = sys.burnState;

    JsonObject env = doc.createNestedObject("env");
    env["temp_f"] = sys.envTempF;
    env["humidity"] = sys.envHumidity;
    env["pressure"] = sys.envPressure;

    String out;
    serializeJson(doc, out);
    return out;
}

static String buildSettingsJson() {
    StaticJsonDocument<256> doc;

    doc["exhaust_setpoint"] = sys.exhaustSetpoint;
    doc["deadband"] = sys.deadbandF;
    doc["boost_time"] = sys.boostTimeSeconds;
    doc["clamp_min"] = sys.clampMinPercent;
    doc["clamp_max"] = sys.clampMaxPercent;
    doc["deadzone_fan"] = sys.deadzoneFanMode;
    doc["coalbed_minutes"] = sys.coalBedTimerMinutes;
    doc["flue_low"] = sys.flueLowThreshold;
    doc["flue_recovery"] = sys.flueRecoveryThreshold;

    String out;
    serializeJson(doc, out);
    return out;
}

// ============================================================
//  POST /api/set
// ============================================================
static void handleApiSet(WiFiClient& client, const String& body) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendJson(client, "{\"error\":\"invalid JSON\"}");
        return;
    }

    if (doc.containsKey("exhaust_setpoint")) {
        sys.exhaustSetpoint = doc["exhaust_setpoint"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("deadband")) {
        sys.deadbandF = doc["deadband"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("boost_time")) {
        sys.boostTimeSeconds = doc["boost_time"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("clamp_min")) {
        sys.clampMinPercent = doc["clamp_min"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("clamp_max")) {
        sys.clampMaxPercent = doc["clamp_max"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("deadzone_fan")) {
        sys.deadzoneFanMode = doc["deadzone_fan"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("coalbed_minutes")) {
        sys.coalBedTimerMinutes = doc["coalbed_minutes"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("flue_low")) {
        sys.flueLowThreshold = doc["flue_low"];
        sys.remoteChanged = true;
    }
    if (doc.containsKey("flue_recovery")) {
        sys.flueRecoveryThreshold = doc["flue_recovery"];
        sys.remoteChanged = true;
    }

    sendJson(client, "{\"ok\":true}");
}

// ============================================================
//  WiFi Init
// ============================================================
void wifiapi_init() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
    }

    server.begin();
}

// ============================================================
//  WiFi Loop
// ============================================================
void wifiapi_loop() {
    WiFiClient client = server.available();
    if (!client) return;

    // Wait for request
    while (!client.available()) {
        delay(1);
    }

    String req = client.readStringUntil('\r');
    client.readStringUntil('\n'); // consume newline

    // Read body if POST
    String body = "";
    if (req.startsWith("POST")) {
        while (client.available()) {
            body += client.readString();
        }
    }

    // Route
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

    delay(1);
    client.stop();
}
