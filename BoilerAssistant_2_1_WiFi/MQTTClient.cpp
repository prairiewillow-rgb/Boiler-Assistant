/*
 * ============================================================
 *  Boiler Assistant – MQTT Integration Module (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: MQTT_Client.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Ultra‑light, WiFi‑gated, non‑blocking MQTT integration.
 *      Responsibilities:
 *        • Maintain MQTT connection (30s reconnect cooldown)
 *        • Publish state + water every 60s
 *        • Publish settings every 120s
 *        • Handle remote setpoint commands
 *        • Provide full Home Assistant MQTT Discovery
 *
 *      Notes:
 *        - Called once per second from main loop
 *        - Uses ArduinoMqttClient (UNO R4 WiFi)
 *        - Zero blocking delays
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#include <ArduinoJson.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

#include "MQTTClient.h"
#include "SystemData.h"

extern SystemData sys;

/* ============================================================
 *  MQTT CONFIG
 * ============================================================ */

static const char* MQTT_HOST      = "192.168.1.182";
static const int   MQTT_PORT      = 1883;
static const char* MQTT_CLIENT_ID = "BoilerAssistant";

// Base topics
static const char* TOPIC_STATE    = "boiler/state";
static const char* TOPIC_SETTINGS = "boiler/settings";
static const char* TOPIC_WATER    = "boiler/water";
static const char* TOPIC_CMD      = "boiler/cmd/setpoint";

// Home Assistant discovery base
static const char* HA_DISCOVERY_PREFIX = "homeassistant";
static const char* HA_DEVICE_ID        = "boiler_assistant";

// Device metadata
static const char* HA_DEVICE_NAME  = "Boiler Assistant";
static const char* HA_DEVICE_MODEL = "UNO R4 WiFi";
static const char* HA_DEVICE_MFR   = "The Architect Collective";
static const char* HA_DEVICE_SW    = "2.1";

/* ============================================================
 *  CLIENTS
 * ============================================================ */

WiFiClient wifiClient;
MqttClient mqtt(wifiClient);

// Publish timers
static unsigned long lastStateMs    = 0;
static unsigned long lastSettingsMs = 0;
static unsigned long lastWaterMs    = 0;

// Reconnect cooldown
static unsigned long lastReconnectAttempt = 0;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */

static void mqtt_publishState();
static void mqtt_publishSettings();
static void mqtt_publishWater();
static void mqtt_onMessage(int messageSize);
static void mqtt_reconnect();

static void publishDiscovery();
static void publishDiscoverySensor(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* unit,
    const char* deviceClass,
    const char* icon = nullptr
);

/* ============================================================
 *  INIT
 * ============================================================ */

void mqtt_init() {
    mqtt.setId(MQTT_CLIENT_ID);

    // Insert your real MQTT password here
    mqtt.setUsernamePassword("homeassistant", "PASSWORD");

    // Correct keep-alive fix (no manual publish needed)
    mqtt.setKeepAliveInterval(15);

    mqtt.onMessage(mqtt_onMessage);
}

/* ============================================================
 *  NON-BLOCKING RECONNECT (WiFi-gated, 30s cooldown)
 * ============================================================ */

static void mqtt_reconnect() {
    unsigned long now = millis();

    if (!sys.wifiOK) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;

    if (now - lastReconnectAttempt < 30000) return;
    lastReconnectAttempt = now;

    mqtt.connect(MQTT_HOST, MQTT_PORT);

    if (mqtt.connected()) {
        mqtt.subscribe(TOPIC_CMD);
        publishDiscovery();
    }
}

/* ============================================================
 *  LOOP (called once per second from .ino)
 * ============================================================ */

void mqtt_loop() {
    if (!sys.wifiOK) return;
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt_reconnect();
    if (!mqtt.connected()) return;

    mqtt.poll();  // handles internal PINGREQ automatically

    unsigned long now = millis();

    if (now - lastStateMs > 60000) {
        mqtt_publishState();
        lastStateMs = now;
    }

    if (now - lastWaterMs > 60000) {
        mqtt_publishWater();
        lastWaterMs = now;
    }

    if (now - lastSettingsMs > 120000) {
        mqtt_publishSettings();
        lastSettingsMs = now;
    }
}

/* ============================================================
 *  PUBLISHERS – STATE / SETTINGS / WATER
 * ============================================================ */

static void mqtt_publishState() {
    StaticJsonDocument<192> doc;
    doc["exhaust"] = sys.exhaustSmoothF;
    doc["fan"]     = sys.fanFinal;
    doc["state"]   = sys.burnState;

    char buf[192];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_STATE);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishSettings() {
    StaticJsonDocument<192> doc;

    doc["setpoint"]  = sys.exhaustSetpoint;
    doc["deadband"]  = sys.deadbandF;
    doc["fan_min"]   = sys.clampMinPercent;
    doc["fan_max"]   = sys.clampMaxPercent;
    doc["boost_sec"] = sys.boostTimeSeconds;
    doc["coal_min"]  = sys.coalBedTimerMinutes;
    doc["flue_low"]  = sys.flueLowThreshold;
    doc["flue_rec"]  = sys.flueRecoveryThreshold;

    char buf[192];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_SETTINGS);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishWater() {
    StaticJsonDocument<192> doc;
    JsonArray arr = doc.createNestedArray("water");

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        arr.add(sys.waterTempF[i]);
    }
    doc["count"] = sys.waterProbeCount;

    char buf[192];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_WATER);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

/* ============================================================
 *  COMMAND HANDLER – remote setpoint only
 * ============================================================ */

static void mqtt_onMessage(int messageSize) {
    String topic = mqtt.messageTopic();

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, mqtt)) return;

    if (topic == TOPIC_CMD) {
        if (doc.containsKey("setpoint")) {
            sys.exhaustSetpoint = doc["setpoint"];
            sys.remoteChanged   = true;
        }
    }
}

/* ============================================================
 *  HOME ASSISTANT DISCOVERY – HELPERS
 * ============================================================ */

static void publishDiscoverySensor(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* unit,
    const char* deviceClass,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["stat_t"]  = stateTopic;

    if (valueTemplate && valueTemplate[0] != '\0') doc["val_tpl"] = valueTemplate;
    if (unit && unit[0] != '\0') doc["unit_of_meas"] = unit;
    if (deviceClass && deviceClass[0] != '\0') doc["dev_cla"] = deviceClass;
    if (icon && icon[0] != '\0') doc["ic"] = icon;

    doc["avty_t"]       = "boiler/availability";
    doc["pl_avail"]     = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;
    dev["mdl"]  = HA_DEVICE_MODEL;
    dev["mf"]   = HA_DEVICE_MFR;
    dev["sw"]   = HA_DEVICE_SW;

    char buf[512];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(topic);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void publishAvailabilityOnline() {
    StaticJsonDocument<64> doc;
    doc["status"] = "online";

    char buf[64];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage("boiler/availability");
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

/* ============================================================
 *  HOME ASSISTANT DISCOVERY – FULL PACKAGE
 * ============================================================ */

static void publishDiscovery() {
    if (!mqtt.connected()) return;

    publishAvailabilityOnline();

    publishDiscoverySensor("exhaust_temp", "Boiler Exhaust Temperature",
        TOPIC_STATE, "{{ value_json.exhaust }}", "°F", "temperature");

    publishDiscoverySensor("fan_percent", "Boiler Fan Output",
        TOPIC_STATE, "{{ value_json.fan }}", "%", "power_factor");

    publishDiscoverySensor("burn_state", "Boiler Burn State",
        TOPIC_STATE, "{{ value_json.state }}", "", "", "mdi:fire");

    publishDiscoverySensor("setpoint", "Boiler Exhaust Setpoint",
        TOPIC_SETTINGS, "{{ value_json.setpoint }}", "°F", "temperature");

    publishDiscoverySensor("deadband", "Boiler Deadband",
        TOPIC_SETTINGS, "{{ value_json.deadband }}", "°F", "", "mdi:ray-vertex");

    publishDiscoverySensor("fan_min", "Boiler Fan Min",
        TOPIC_SETTINGS, "{{ value_json.fan_min }}", "%", "", "mdi:fan");

    publishDiscoverySensor("fan_max", "Boiler Fan Max",
        TOPIC_SETTINGS, "{{ value_json.fan_max }}", "%", "", "mdi:fan");

    publishDiscoverySensor("boost_seconds", "Boiler Boost Time",
        TOPIC_SETTINGS, "{{ value_json.boost_sec }}", "s", "", "mdi:rocket-launch");

    publishDiscoverySensor("coalbed_minutes", "Boiler Coalbed Timer",
        TOPIC_SETTINGS, "{{ value_json.coal_min }}", "min", "", "mdi:timer-sand");

    publishDiscoverySensor("flue_low", "Boiler Flue Low Threshold",
        TOPIC_SETTINGS, "{{ value_json.flue_low }}", "°F", "temperature");

    publishDiscoverySensor("flue_recovery", "Boiler Flue Recovery Threshold",
        TOPIC_SETTINGS, "{{ value_json.flue_rec }}", "°F", "temperature");

    publishDiscoverySensor("env_temp", "Boiler Ambient Temperature",
        TOPIC_STATE, "", "", "");

    publishDiscoverySensor("water_probe_count", "Boiler Water Probe Count",
        TOPIC_WATER, "{{ value_json.count }}", "", "", "mdi:counter");

    publishDiscoverySensor("water_temp_0", "Boiler Water Temp 0",
        TOPIC_WATER, "{{ value_json.water[0] }}", "°F", "temperature");

    publishDiscoverySensor("water_temp_1", "Boiler Water Temp 1",
        TOPIC_WATER, "{{ value_json.water[1] }}", "°F", "temperature");

    publishDiscoverySensor("water_temp_2", "Boiler Water Temp 2",
        TOPIC_WATER, "{{ value_json.water[2] }}", "°F", "temperature");

    publishDiscoverySensor("water_temp_3", "Boiler Water Temp 3",
        TOPIC_WATER, "{{ value_json.water[3] }}", "°F", "temperature");

    publishDiscoverySensor("wifi_status", "Boiler WiFi Status",
        "boiler/availability", "{{ value_json.status }}", "", "", "mdi:wifi");
}

