/*
 * ============================================================
 *  Boiler Assistant – MQTT Integration Module (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: MQTT_Client.cpp
 *  Maintainer: Karl (Embedded Systems Architect) w/Marks help
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
 *      v2.3‑Environmental:
 *        • Full seasonal logic exposure (mode, auto, lockout, starts, buffers, setpoints)
 *        • Command topics for all major settings
 *        • Home Assistant discovery for sensors + controls
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#include <ArduinoJson.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

#include "MQTTClient.h"
#include "SystemData.h"
#include "EEPROMStorage.h"
#include "secrets.h"

extern SystemData sys;
extern uint8_t probeRoleMap[PROBE_ROLE_COUNT];

/* ============================================================
 *  MQTT CONFIG
 * ============================================================ */

static const char* MQTT_HOST      = SECRET_MQTT_SERVER;
static const int   MQTT_PORT      = 1883;
static const char* MQTT_CLIENT_ID = "BoilerAssistant";

// Base topics
static const char* TOPIC_STATE      = "boiler/state";
static const char* TOPIC_SETTINGS   = "boiler/settings";
static const char* TOPIC_WATER      = "boiler/water";
static const char* TOPIC_OUTDOOR    = "boiler/outdoor";

// Command base (we subscribe with wildcard)
static const char* TOPIC_CMD_BASE   = "boiler/cmd/";

// Home Assistant discovery base
static const char* HA_DISCOVERY_PREFIX = "homeassistant";
static const char* HA_DEVICE_ID        = "boiler_assistant";

// Device metadata
static const char* HA_DEVICE_NAME  = "Boiler Assistant";
static const char* HA_DEVICE_MODEL = "UNO R4 WiFi";
static const char* HA_DEVICE_MFR   = "Karl";
static const char* HA_DEVICE_SW    = "2.3";

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
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* unit,
    const char* deviceClass,
    const char* icon = nullptr
);

static void publishDiscoveryNumber(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* unit,
    float minVal,
    float maxVal,
    float step,
    const char* deviceClass = nullptr,
    const char* icon = nullptr
);

static void publishDiscoverySwitch(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* icon = nullptr
);

static void handleCommandTopic(const String& topic, StaticJsonDocument<256>& doc);

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
        mqtt.subscribe("boiler/cmd/#");
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

    const char* phaseText =
        (sys.burnState == BURN_IDLE)  ? "IDLE"  :
        (sys.burnState == BURN_BOOST) ? "BOOST" :
        (sys.burnState == BURN_RAMP)  ? "RAMP"  :
        (sys.burnState == BURN_HOLD)  ? "HOLD"  :
                                        "UNKNOWN";

    doc["state_text"] = phaseText;

    // Ember Guardian
    doc["ember_active"] = sys.holdTimerActive;

    int emberSeconds = sys.holdTimerActive
        ? ((sys.holdStartMs + (sys.emberGuardianTimerMinutes * 60000UL)) - millis()) / 1000
        : 0;

    doc["ember_seconds"] = emberSeconds;
    doc["ember_minutes"] = emberSeconds / 60;

    char emberText[32];
    snprintf(emberText, sizeof(emberText), "%d minutes remaining", emberSeconds / 60);
    doc["ember_remaining_text"] = emberText;

    // Phase timestamps
    doc["boost_start"] = sys.boostStartMs;
    doc["ramp_start"]  = sys.rampStartMs;
    doc["hold_start"]  = sys.holdStartMs;
    doc["ember_start"] = sys.emberGuardianStartMs;

    char buf[1024];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_STATE);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishSettings() {
    StaticJsonDocument<1024> doc;

    // Core v2.2 settings
    doc["setpoint"]   = sys.exhaustSetpoint;
    doc["deadband"]   = sys.deadbandF;
    doc["fan_min"]    = sys.clampMinPercent;
    doc["fan_max"]    = sys.clampMaxPercent;
    doc["boost_sec"]  = sys.boostTimeSeconds;
    doc["ember_min"]  = sys.emberGuardianTimerMinutes;
    doc["flue_low"]   = sys.flueLowThreshold;
    doc["flue_rec"]   = sys.flueRecoveryThreshold;
    doc["deadzone"]   = sys.deadzoneFanMode;

    // v2.3 environmental logic
    doc["season_mode"] = sys.envSeasonMode;
    doc["auto_season"] = sys.envAutoSeasonEnabled;
    doc["lockout_hr"]  = (uint16_t)(sys.envModeLockoutSec / 3600UL);

    doc["summer_start"]  = sys.envSummerStartF;
    doc["spf_start"]     = sys.envSpringFallStartF;
    doc["winter_start"]  = sys.envWinterStartF;
    doc["extreme_start"] = sys.envExtremeStartF;

    doc["summer_buffer"]  = sys.envHystSummerF;
    doc["spf_buffer"]     = sys.envHystSpringFallF;
    doc["winter_buffer"]  = sys.envHystWinterF;
    doc["extreme_buffer"] = sys.envHystExtremeF;

    doc["summer_setpoint"]  = sys.envSetpointSummerF;
    doc["spf_setpoint"]     = sys.envSetpointSpringFallF;
    doc["winter_setpoint"]  = sys.envSetpointWinterF;
    doc["extreme_setpoint"] = sys.envSetpointExtremeF;

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
 *  HOME ASSISTANT DISCOVERY
 * ============================================================ */

static void publishDiscovery() {

    /* -------------------------
     * Core sensors
     * ------------------------- */
    publishDiscoverySensor("exhaust", "Exhaust Temp", TOPIC_STATE,
                           "{{value_json.exhaust}}", "°F", "temperature", "mdi:fire");

    publishDiscoverySensor("fan", "Fan Speed", TOPIC_STATE,
                           "{{value_json.fan}}", "%", nullptr, "mdi:fan");

    publishDiscoverySensor("fan_final", "Fan Final Output", TOPIC_STATE,
                           "{{value_json.fan_final}}", "%", nullptr, "mdi:fan-speed-3");

    publishDiscoverySensor("wifi_signal", "WiFi Signal", TOPIC_STATE,
                           "{{value_json.rssi}}", "dBm", "signal_strength", "mdi:wifi");

    publishDiscoverySensor("burn_state", "Burn State", TOPIC_STATE,
                           "{{value_json.state}}", "", nullptr, "mdi:fire-alert");

    publishDiscoverySensor("state_text", "Burn Phase Text", TOPIC_STATE,
                           "{{value_json.state_text}}", "", nullptr, "mdi:fire");

    /* -------------------------
     * Ember Guardian
     * ------------------------- */
    publishDiscoverySensor("ember_active", "Ember Guard Active", TOPIC_STATE,
                           "{{value_json.ember_active}}", "", "power", "mdi:shield");

    publishDiscoverySensor("ember_seconds", "Ember Guard Seconds", TOPIC_STATE,
                           "{{value_json.ember_seconds}}", "s", "duration", "mdi:timer-sand");

    publishDiscoverySensor("ember_minutes", "Ember Guard Minutes", TOPIC_STATE,
                           "{{value_json.ember_minutes}}", "min", nullptr, "mdi:timer");

    publishDiscoverySensor("ember_remaining_text", "Ember Guard Remaining", TOPIC_STATE,
                           "{{value_json.ember_remaining_text}}", "", nullptr, "mdi:timer-sand-complete");

    /* -------------------------
     * Water probes
     * ------------------------- */
    for (int i = 0; i < 4; i++) {
        char obj[16], name[32], tpl[64];
        snprintf(obj, sizeof(obj), "water_%d", i+1);
        snprintf(name, sizeof(name), "Water Temp %d", i+1);
        snprintf(tpl, sizeof(tpl), "{{value_json.water[%d]}}", i);

        publishDiscoverySensor(obj, name, TOPIC_WATER,
                               tpl, "°F", "temperature", "mdi:coolant-temperature");
    }

    /* -------------------------
     * Outdoor BME280
     * ------------------------- */
    publishDiscoverySensor("outdoor_temp", "Outdoor Temp", TOPIC_OUTDOOR,
                           "{{value_json.temp}}", "°F", "temperature", "mdi:thermometer");

    publishDiscoverySensor("outdoor_hum", "Outdoor Humidity", TOPIC_OUTDOOR,
                           "{{value_json.hum}}", "%", "humidity", "mdi:water-percent");

    publishDiscoverySensor("outdoor_pres", "Outdoor Pressure", TOPIC_OUTDOOR,
                           "{{value_json.pres}}", "hPa", "pressure", "mdi:gauge");

    /* ============================================================
     *  CONTROL ENTITIES (NUMBERS + SWITCHES)
     * ============================================================ */

    /* -------------------------
     * Core controls
     * ------------------------- */
    publishDiscoveryNumber("setpoint", "Exhaust Setpoint",
                           "boiler/cmd/setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1, "temperature", "mdi:fire");

    publishDiscoveryNumber("boost", "Boost Time",
                           "boiler/cmd/boost", TOPIC_SETTINGS,
                           "s", 5, 300, 5, nullptr, "mdi:rocket-launch");

    publishDiscoveryNumber("deadband", "Deadband",
                           "boiler/cmd/deadband", TOPIC_SETTINGS,
                           "°F", 1, 100, 1, nullptr, "mdi:arrow-expand-vertical");

    publishDiscoveryNumber("clamp_min", "Fan Clamp Min",
                           "boiler/cmd/clamp_min", TOPIC_SETTINGS,
                           "%", 0, 100, 1, nullptr, "mdi:fan");

    publishDiscoveryNumber("clamp_max", "Fan Clamp Max",
                           "boiler/cmd/clamp_max", TOPIC_SETTINGS,
                           "%", 0, 100, 1, nullptr, "mdi:fan");

    publishDiscoverySwitch("deadzone", "Deadzone Fan Mode",
                           "boiler/cmd/deadzone", TOPIC_SETTINGS,
                           "mdi:toggle-switch");

    publishDiscoveryNumber("ember", "Ember Guardian Minutes",
                           "boiler/cmd/ember", TOPIC_SETTINGS,
                           "min", 5, 60, 1, nullptr, "mdi:shield");

    publishDiscoveryNumber("flue_low", "Flue Low Threshold",
                           "boiler/cmd/flue_low", TOPIC_SETTINGS,
                           "°F", 50, 900, 5, nullptr, "mdi:thermometer-alert");

    publishDiscoveryNumber("flue_rec", "Flue Recovery Threshold",
                           "boiler/cmd/flue_rec", TOPIC_SETTINGS,
                           "°F", 50, 900, 5, nullptr, "mdi:thermometer-chevron-up");

    /* -------------------------
     * Seasonal logic controls
     * ------------------------- */

    publishDiscoveryNumber("lockout", "Season Lockout Hours",
                           "boiler/cmd/lockout", TOPIC_SETTINGS,
                           "h", 1, 24, 1, nullptr, "mdi:timer-lock");

    publishDiscoverySwitch("auto_season", "Auto Season",
                           "boiler/cmd/auto_season", TOPIC_SETTINGS,
                           "mdi:calendar-sync");

    publishDiscoveryNumber("season_mode", "Season Mode",
                           "boiler/cmd/season_mode", TOPIC_SETTINGS,
                           "", 0, 2, 1, nullptr, "mdi:calendar");

    /* -------------------------
     * Seasonal thresholds
     * ------------------------- */

    publishDiscoveryNumber("summer_start", "Summer Start Temp",
                           "boiler/cmd/summer_start", TOPIC_SETTINGS,
                           "°F", -40, 120, 1);

    publishDiscoveryNumber("spf_start", "Spring/Fall Start Temp",
                           "boiler/cmd/spf_start", TOPIC_SETTINGS,
                           "°F", -40, 120, 1);

    publishDiscoveryNumber("winter_start", "Winter Start Temp",
                           "boiler/cmd/winter_start", TOPIC_SETTINGS,
                           "°F", -40, 120, 1);

    publishDiscoveryNumber("extreme_start", "Extreme Start Temp",
                           "boiler/cmd/extreme_start", TOPIC_SETTINGS,
                           "°F", -60, 120, 1);

    /* -------------------------
     * Seasonal buffers
     * ------------------------- */

    publishDiscoveryNumber("summer_buffer", "Summer Buffer",
                           "boiler/cmd/summer_buffer", TOPIC_SETTINGS,
                           "°F", 1, 30, 1);

    publishDiscoveryNumber("spf_buffer", "Spring/Fall Buffer",
                           "boiler/cmd/spf_buffer", TOPIC_SETTINGS,
                           "°F", 1, 30, 1);

     publishDiscoveryNumber("winter_buffer", "Winter Buffer",
                           "boiler/cmd/winter_buffer", TOPIC_SETTINGS,
                           "°F", 1, 30, 1);

    publishDiscoveryNumber("extreme_buffer", "Extreme Buffer",
                           "boiler/cmd/extreme_buffer", TOPIC_SETTINGS,
                           "°F", 1, 30, 1);

    /* -------------------------
     * Seasonal setpoints
     * ------------------------- */

    publishDiscoveryNumber("summer_setpoint", "Summer Setpoint",
                           "boiler/cmd/summer_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("spf_setpoint", "Spring/Fall Setpoint",
                           "boiler/cmd/spf_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("winter_setpoint", "Winter Setpoint",
                           "boiler/cmd/winter_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("extreme_setpoint", "Extreme Setpoint",
                           "boiler/cmd/extreme_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);
}

/* ============================================================
 *  DISCOVERY HELPERS
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

static void publishDiscoveryNumber(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* unit,
    float minVal,
    float maxVal,
    float step,
    const char* deviceClass,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/number/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["cmd_t"]   = cmdTopic;
    doc["stat_t"]  = stateTopic;

    doc["min"]     = minVal;
    doc["max"]     = maxVal;
    doc["step"]    = step;

    if (unit)        doc["unit_of_meas"] = unit;
    if (deviceClass) doc["dev_cla"] = deviceClass;
    if (icon)        doc["ic"] = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}

static void publishDiscoverySwitch(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["cmd_t"]   = cmdTopic;
    doc["stat_t"]  = stateTopic;

    if (icon) doc["ic"] = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}

/* ============================================================
 *  COMMAND HANDLER
 * ============================================================ */

static void mqtt_onMessage(int messageSize) {
    String topic = mqtt.messageTopic();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, mqtt)) {
        return; // bad JSON
    }

    handleCommandTopic(topic, doc);
}

/* ============================================================
 *  TOPIC DISPATCHER
 * ============================================================ */

static void handleCommandTopic(const String& topic, StaticJsonDocument<256>& doc) {

    if (!doc.containsKey("value")) return;
    auto val = doc["value"];

    /* ============================================================
     *  CORE SETTINGS
     * ============================================================ */

    if (topic.endsWith("/setpoint")) {
        int v = val.as<int>();
        eeprom_saveSetpoint(v);
        sys.exhaustSetpoint = v;
        return;
    }

    if (topic.endsWith("/boost")) {
        int v = val.as<int>();
        eeprom_saveBoostTime(v);
        sys.boostTimeSeconds = v;
        return;
    }

    if (topic.endsWith("/deadband")) {
        int v = val.as<int>();
        eeprom_saveDeadband(v);
        sys.deadbandF = v;
        return;
    }

    if (topic.endsWith("/clamp_min")) {
        int v = val.as<int>();
        eeprom_saveClampMin(v);
        sys.clampMinPercent = v;
        return;
    }

    if (topic.endsWith("/clamp_max")) {
        int v = val.as<int>();
        eeprom_saveClampMax(v);
        sys.clampMaxPercent = v;
        return;
    }

    if (topic.endsWith("/deadzone")) {
        bool v = val.as<bool>();
        eeprom_saveDeadzone(v ? 1 : 0);
        sys.deadzoneFanMode = v ? 1 : 0;
        return;
    }

    if (topic.endsWith("/ember")) {
        int v = val.as<int>();
        eeprom_saveEmberGuardMinutes(v);
        sys.emberGuardianTimerMinutes = v;
        return;
    }

    if (topic.endsWith("/flue_low")) {
        int v = val.as<int>();
        eeprom_saveFlueLow(v);
        sys.flueLowThreshold = v;
        return;
    }

    if (topic.endsWith("/flue_rec")) {
        int v = val.as<int>();
        eeprom_saveFlueRecovery(v);
        sys.flueRecoveryThreshold = v;
        return;
    }

    /* ============================================================
     *  SEASONAL LOGIC (v2.3‑Environmental)
     * ============================================================ */

    if (topic.endsWith("/season_mode")) {
        int mode = val.as<int>();
        eeprom_saveEnvSeasonMode(mode);
        sys.envSeasonMode = mode;
        return;
    }

    if (topic.endsWith("/auto_season")) {
        bool en = val.as<bool>();
        eeprom_saveEnvAutoSeason(en);
        sys.envAutoSeasonEnabled = en;
        return;
    }

    if (topic.endsWith("/lockout")) {
        int hr = val.as<int>();
        eeprom_saveEnvLockoutHours(hr);
        sys.envModeLockoutSec = (uint32_t)hr * 3600UL;
        return;
    }

    /* -------------------------
     * Seasonal start temps
     * ------------------------- */

    if (topic.endsWith("/summer_start")) {
        int v = val.as<int>();
        sys.envSummerStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/spf_start")) {
        int v = val.as<int>();
        sys.envSpringFallStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/winter_start")) {
        int v = val.as<int>();
        sys.envWinterStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/extreme_start")) {
        int v = val.as<int>();
        sys.envExtremeStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    /* -------------------------
     * Seasonal buffers
     * ------------------------- */

    if (topic.endsWith("/summer_buffer")) {
        int v = val.as<int>();
        sys.envHystSummerF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/spf_buffer")) {
        int v = val.as<int>();
        sys.envHystSpringFallF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/winter_buffer")) {
        int v = val.as<int>();
        sys.envHystWinterF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/extreme_buffer")) {
        int v = val.as<int>();
        sys.envHystExtremeF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    /* -------------------------
     * Seasonal setpoints
     * ------------------------- */

    if (topic.endsWith("/summer_setpoint")) {
        int v = val.as<int>();
        sys.envSetpointSummerF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/spf_setpoint")) {
        int v = val.as<int>();
        sys.envSetpointSpringFallF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/winter_setpoint")) {
        int v = val.as<int>();
        sys.envSetpointWinterF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/extreme_setpoint")) {
        int v = val.as<int>();
        sys.envSetpointExtremeF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    /* ============================================================
     *  OPTIONAL: PROBE ROLE ASSIGNMENT
     * ============================================================ */

    if (topic.endsWith("/probe_role")) {
        if (!doc.containsKey("role") || !doc.containsKey("phys")) return;

        int role = doc["role"].as<int>();
        int phys = doc["phys"].as<int>();

        if (role >= 0 && role < PROBE_ROLE_COUNT &&
            phys >= 0 && phys < MAX_WATER_PROBES) {

            probeRoleMap[role] = phys;
            eeprom_saveProbeRoles();
        }
        return;
    }

    /* ============================================================
     *  OPTIONAL: FACTORY RESET
     * ============================================================ */

    if (topic.endsWith("/factory_reset")) {
        if (doc.containsKey("confirm") && doc["confirm"] == true) {
            eeprom_init();  // reload defaults
        }
        return;
    }
}
