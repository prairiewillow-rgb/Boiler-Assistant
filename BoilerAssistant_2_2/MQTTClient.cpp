/*
 * ============================================================
 *  Boiler Assistant – MQTT Integration Module (v2.2)
 *  ------------------------------------------------------------
 *  File: MQTT_Client.cpp (FAST TELEMETRY EDITION + OUTDOOR BME280)
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect) w/Marks help
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Ultra‑light, WiFi‑gated, non‑blocking MQTT integration.
 *
 *      FAST TELEMETRY MODE:
 *        • Water temps → every 1s
 *        • Exhaust + fan + burn state → every 1s
 *        • Outdoor BME280 (temp/hum/pres) → every 1s
 *        • Full STATE refresh → every 30s
 *        • Settings → every 60s
 *
 *      Notes:
 *        - Called once per second from main loop
 *        - Uses ArduinoMqttClient (UNO R4 WiFi)
 *        - Zero blocking delays
 *
 *  v2.2 Additions:
 *      - Standardized v2.2 header format (no codename)
 *      - Clarified FAST TELEMETRY behavior
 *      - Improved documentation of Home Assistant discovery
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include <ArduinoJson.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

#include "MQTTClient.h"
#include "SystemData.h"
#include "secrets.h"

extern SystemData sys;

/* ============================================================
 *  MQTT CONFIG
 * ============================================================ */

static const char* MQTT_HOST      = SECRET_MQTT_SERVER; 
static const int   MQTT_PORT      = 1883;
static const char* MQTT_CLIENT_ID = "BoilerAssistant";

// Base topics
static const char* TOPIC_STATE    = "boiler/state";
static const char* TOPIC_SETTINGS = "boiler/settings";
static const char* TOPIC_WATER    = "boiler/water";
static const char* TOPIC_CMD      = "boiler/cmd/setpoint";
static const char* TOPIC_OUTDOOR  = "boiler/outdoor";   // Outdoor BME280

// Home Assistant discovery base
static const char* HA_DISCOVERY_PREFIX = "homeassistant";
static const char* HA_DEVICE_ID        = "boiler_assistant";

// Device metadata
static const char* HA_DEVICE_NAME  = "Boiler Assistant";
static const char* HA_DEVICE_MODEL = "UNO R4 WiFi";
static const char* HA_DEVICE_MFR   = "The Architect Collective";
static const char* HA_DEVICE_SW    = "2.2";

/* ============================================================
 *  CLIENTS
 * ============================================================ */

WiFiClient wifiClient;
MqttClient mqtt(wifiClient);

static unsigned long lastStateFastMs      = 0;
static unsigned long lastStateSlowMs      = 0;
static unsigned long lastWaterMs          = 0;
static unsigned long lastSettingsMs       = 0;
static unsigned long lastOutdoorBmeMs     = 0;
static unsigned long lastReconnectAttempt = 0;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */

static void mqtt_publishState();
static void mqtt_publishSettings();
static void mqtt_publishWater();
static void mqtt_publishOutdoor();
static void mqtt_onMessage(int messageSize);
static void mqtt_reconnect();
static void publishDiscovery();
static void publishDiscoverySensor(
    const char* objectId, const char* name, const char* stateTopic,
    const char* valueTemplate, const char* unit, const char* deviceClass,
    const char* icon = nullptr
);

/* ============================================================
 *  INIT
 * ============================================================ */

void mqtt_init() {
    mqtt.setId(MQTT_CLIENT_ID);
    mqtt.setUsernamePassword(SECRET_MQTT_USER, SECRET_MQTT_PASS);
    mqtt.setKeepAliveInterval(15);
    mqtt.onMessage(mqtt_onMessage);
}

/* ============================================================
 *  LOOP
 * ============================================================ */

void mqtt_loop() {
    if (!sys.wifiOK) return;
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt_reconnect();
    if (!mqtt.connected()) return;

    mqtt.poll();

    unsigned long now = millis();

    // Water temps — 1s
    if (now - lastWaterMs > 1000) {
        mqtt_publishWater();
        lastWaterMs = now;
    }

    // Exhaust + fan + burn state — 1s
    if (now - lastStateFastMs > 1000) {
        mqtt_publishState();
        lastStateFastMs = now;
    }

    // Full state refresh — 30s
    if (now - lastStateSlowMs > 30000) {
        mqtt_publishState();
        lastStateSlowMs = now;
    }

    // Settings — 60s
    if (now - lastSettingsMs > 60000) {
        mqtt_publishSettings();
        lastSettingsMs = now;
    }

    // Outdoor BME280 — 1s
    if (now - lastOutdoorBmeMs > 1000) {
        mqtt_publishOutdoor();
        lastOutdoorBmeMs = now;
    }
}

/* ============================================================
 *  RECONNECT
 * ============================================================ */

static void mqtt_reconnect() {
    unsigned long now = millis();
    if (!sys.wifiOK || WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
    if (now - lastReconnectAttempt < 30000) return;

    lastReconnectAttempt = now;
    if (mqtt.connect(MQTT_HOST, MQTT_PORT)) {
        mqtt.subscribe(TOPIC_CMD);
        publishDiscovery();
    }
}

/* ============================================================
 *  PUBLISHERS
 * ============================================================ */

static void mqtt_publishState() {
    StaticJsonDocument<1024> doc;

    doc["exhaust"] = sys.exhaustSmoothF;
    doc["fan"]     = sys.fanFinal;
    doc["fan_final"] = sys.fanFinal;

    doc["state"]   = sys.burnState;
    doc["rssi"]    = WiFi.RSSI();

    // Human-readable burn phase
    const char* phaseText =
        (sys.burnState == BURN_IDLE)  ? "IDLE"  :
        (sys.burnState == BURN_BOOST) ? "BOOST" :
        (sys.burnState == BURN_RAMP)  ? "RAMP"  :
        (sys.burnState == BURN_HOLD)  ? "HOLD"  :
                                        "UNKNOWN";

    doc["state_text"] = phaseText;

    // Ember Guard
    doc["ember_active"] = sys.holdTimerActive;
    int emberSeconds = sys.holdTimerActive
        ? ((sys.holdStartMs + (sys.coalBedTimerMinutes * 60000UL)) - millis()) / 1000
        : 0;

    doc["ember_seconds"] = emberSeconds;

    int emberMinutes = emberSeconds / 60;
    doc["ember_minutes"] = emberMinutes;

    char emberText[32];
    snprintf(emberText, sizeof(emberText), "%d minutes remaining", emberMinutes);
    doc["ember_remaining_text"] = emberText;

    // Phase start timestamps (ms since boot)
    doc["boost_start"] = sys.boostStartMs;
    doc["ramp_start"]  = sys.rampStartMs;
    doc["hold_start"]  = sys.holdStartMs;
    doc["ember_start"] = sys.coalbedStartMs;

    char buf[1024];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_STATE);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishSettings() {
    StaticJsonDocument<1024> doc;

    doc["setpoint"]   = sys.exhaustSetpoint;
    doc["deadband"]   = sys.deadbandF;
    doc["fan_min"]    = sys.clampMinPercent;
    doc["fan_max"]    = sys.clampMaxPercent;
    doc["boost_sec"]  = sys.boostTimeSeconds;
    doc["boost_time"] = sys.boostTimeSeconds;
    doc["coal_min"]   = sys.coalBedTimerMinutes;
    doc["flue_low"]   = sys.flueLowThreshold;
    doc["flue_rec"]   = sys.flueRecoveryThreshold;

    char buf[1024];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_SETTINGS);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishWater() {
    StaticJsonDocument<256> doc;

    JsonArray arr = doc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        arr.add(sys.waterTempF[i]);
    }
    doc["count"] = sys.waterProbeCount;

    char buf[256];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_WATER);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishOutdoor() {
    StaticJsonDocument<256> doc;

    doc["temp"] = sys.envTempF;
    doc["hum"]  = sys.envHumidity;
    doc["pres"] = sys.envPressure;

    char buf[256];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_OUTDOOR);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

/* ============================================================
 *  COMMANDS
 * ============================================================ */

static void mqtt_onMessage(int messageSize) {
    String topic = mqtt.messageTopic();
    StaticJsonDocument<128> doc;

    if (deserializeJson(doc, mqtt)) return;

    if (topic == TOPIC_CMD && doc.containsKey("setpoint")) {
        sys.exhaustSetpoint = doc["setpoint"];
        sys.remoteChanged   = true;
    }
}

/* ============================================================
 *  DISCOVERY — STATE + WATER + OUTDOOR BME280
 * ============================================================ */

static void publishDiscovery() {

    // Core state
    publishDiscoverySensor("exhaust", "Exhaust Temp", TOPIC_STATE,
                           "{{value_json.exhaust}}", "°F", "temperature", "mdi:fire");

    publishDiscoverySensor("fan", "Fan Speed", TOPIC_STATE,
                           "{{value_json.fan}}", "%", nullptr, "mdi:fan");

    publishDiscoverySensor("fan_final", "Fan Final Output", TOPIC_STATE,
                           "{{value_json.fan_final}}", "%", nullptr, "mdi:fan-speed-3");

    publishDiscoverySensor("wifi_signal", "WiFi Signal", TOPIC_STATE,
                           "{{value_json.rssi}}", "dBm", "signal_strength", "mdi:wifi");

    // Burn state
    publishDiscoverySensor("burn_state", "Burn State", TOPIC_STATE,
                           "{{value_json.state}}", "", nullptr, "mdi:fire-alert");

    publishDiscoverySensor("state_text", "Burn Phase Text", TOPIC_STATE,
                           "{{value_json.state_text}}", "", nullptr, "mdi:fire");

    // Ember Guard
    publishDiscoverySensor("ember_active", "Ember Guard Active", TOPIC_STATE,
                           "{{value_json.ember_active}}", "", "power", "mdi:shield");

    publishDiscoverySensor("ember_seconds", "Ember Guard Seconds", TOPIC_STATE,
                           "{{value_json.ember_seconds}}", "s", "duration", "mdi:timer-sand");

    publishDiscoverySensor("ember_minutes", "Ember Guard Minutes", TOPIC_STATE,
                           "{{value_json.ember_minutes}}", "min", nullptr, "mdi:timer");

    publishDiscoverySensor("ember_remaining_text", "Ember Guard Remaining", TOPIC_STATE,
                           "{{value_json.ember_remaining_text}}", "", nullptr, "mdi:timer-sand-complete");

    // Phase timestamps
    publishDiscoverySensor("boost_start", "Boost Start", TOPIC_STATE,
                           "{{value_json.boost_start}}", "ms", nullptr, "mdi:rocket-launch");

    publishDiscoverySensor("ramp_start", "Ramp Start", TOPIC_STATE,
                           "{{value_json.ramp_start}}", "ms", nullptr, "mdi:trending-up");

    publishDiscoverySensor("hold_start", "Hold Start", TOPIC_STATE,
                           "{{value_json.hold_start}}", "ms", nullptr, "mdi:hand-back-right");

    publishDiscoverySensor("ember_start", "Ember Guardian Start", TOPIC_STATE,
                           "{{value_json.ember_start}}", "ms", nullptr, "mdi:fire-circle");

    // Water probes
    publishDiscoverySensor("water_1", "Water Temp 1", TOPIC_WATER,
                           "{{value_json.water[0]}}", "°F", "temperature", "mdi:coolant-temperature");

    publishDiscoverySensor("water_2", "Water Temp 2", TOPIC_WATER,
                           "{{value_json.water[1]}}", "°F", "temperature", "mdi:coolant-temperature");

    publishDiscoverySensor("water_3", "Water Temp 3", TOPIC_WATER,
                           "{{value_json.water[2]}}", "°F", "temperature", "mdi:coolant-temperature");

    publishDiscoverySensor("water_4", "Water Temp 4", TOPIC_WATER,
                           "{{value_json.water[3]}}", "°F", "temperature", "mdi:coolant-temperature");

    // Outdoor BME280
    publishDiscoverySensor("outdoor_temp", "Outdoor Temp", TOPIC_OUTDOOR,
                           "{{value_json.temp}}", "°F", "temperature", "mdi:thermometer");

    publishDiscoverySensor("outdoor_hum", "Outdoor Humidity", TOPIC_OUTDOOR,
                           "{{value_json.hum}}", "%", "humidity", "mdi:water-percent");

    publishDiscoverySensor("outdoor_pres", "Outdoor Pressure", TOPIC_OUTDOOR,
                           "{{value_json.pres}}", "hPa", "pressure", "mdi:gauge");

    // Boost duration (settings)
    publishDiscoverySensor("boost_time", "Boost Duration", TOPIC_SETTINGS,
                           "{{value_json.boost_time}}", "s", nullptr, "mdi:rocket-launch");
}

/* ============================================================
 *  DISCOVERY SENSOR HELPER
 * ============================================================ */

static void publishDiscoverySensor(const char* objectId, const char* name, const char* stateTopic, 
                                   const char* valueTemplate, const char* unit, const char* deviceClass, const char* icon) {

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["stat_t"]  = stateTopic;

    if (valueTemplate) doc["val_tpl"] = valueTemplate;
    if (unit)         doc["unit_of_meas"] = unit;
    if (deviceClass)  doc["dev_cla"] = deviceClass;
    if (icon)         doc["ic"] = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}
