/*
 * Boiler Assistant - read-only MQTT telemetry.
 * Author: The Architect Collective
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#include "MQTTClient.h"

#if !BOILER_MQTT_ENABLED

#include <Arduino.h>
void mqtt_init() { Serial.println("MQTT: disabled for isolation test"); }
void mqtt_stop() {}
void mqtt_loop() {}

#else

#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include "JsonConfig.h"
#include "DebugLog.h"
#include "SystemState.h"
#include "SystemData.h"
#include "WiFiProvisioning.h"
#include "RuntimeWiFiClient.h"

extern const char* prov_mqtt_server;
extern const char* prov_mqtt_user;
extern const char* prov_mqtt_pass;

RuntimeWiFiClient<WiFiClient> wifiClient;
MqttClient mqtt(wifiClient);

static const char* TOPIC_STATE = "boiler/state";
static const char* TOPIC_WATER = "boiler/water";
static const char* TOPIC_OUTDOOR = "boiler/outdoor";
static const char* TOPIC_ALERT = "boiler/alert";
static const unsigned long MQTT_READING_INTERVAL_MS = 60000UL;
static unsigned long lastStateFastMs = 0, lastWaterMs = 0, lastOutdoorBmeMs = 0;
static unsigned long lastReconnectAttempt = 0, lastMqttIoMs = 0;
static bool mqttAuthConfigured = false, discoveryActive = false;
static bool lastHighTemp = false, lastTankFault = false;
static bool lastExhaustFallback = false, lastGuardian = false;
static uint8_t discoveryIndex = 0;

// Remove retained discovery for retired entities when MQTT is re-enabled.
static const char* const retiredDiscovery[] = {
    "sensor/fan_final", "sensor/wifi_signal", "sensor/burn_state",
    "sensor/ember_guardian_active", "sensor/ember_guardian_seconds",
    "sensor/ember_guardian_minutes", "sensor/ember_guardian_remaining_text",
    "switch/ember_guardian_override", "sensor/outdoor_hum", "sensor/outdoor_pres",
    "number/setpoint", "number/boost", "number/deadband",
    "number/clamp_min", "number/clamp_max", "switch/deadzone",
    "number/ember", "number/flue_low", "number/flue_rec", "number/lockout",
    "switch/auto_season", "number/season_mode", "number/summer_setpoint",
    "number/spf_setpoint", "number/winter_setpoint", "number/extreme_setpoint",
    "number/tank_low", "number/tank_high", "number/control_mode"
};
static const uint8_t retiredCount = sizeof(retiredDiscovery) / sizeof(retiredDiscovery[0]);

static bool publishJson(const char* topic, JsonDocument& doc, bool retained = false) {
    if (!mqtt.beginMessage(topic, retained)) {
        Serial.println("MQTT: publish start failed");
        return false;
    }
    size_t written = serializeJson(doc, mqtt);
    bool sent = mqtt.endMessage() != 0;
    if (!sent || written != measureJson(doc) || doc.overflowed()) {
        Serial.println("MQTT: publish failed");
        return false;
    }
    return true;
}

static void publishDiscovery() {
    char topic[128], object[20], name[32], value[64];
    if (discoveryIndex < retiredCount) {
        const char* entry = retiredDiscovery[discoveryIndex];
        const char* slash = strchr(entry, '/');
        snprintf(topic, sizeof(topic), "homeassistant/%.*s/boiler_assistant/%s/config",
                 static_cast<int>(slash - entry), entry, slash + 1);
        if (!mqtt.beginMessage(topic, true) || !mqtt.endMessage()) {
            Serial.println("MQTT: discovery removal failed");
            return;
        }
        ++discoveryIndex;
        return;
    }
    uint8_t index = discoveryIndex - retiredCount;
    const char* stateTopic = TOPIC_STATE;
    const char* unit = "\xC2\xB0" "F";
    const char* deviceClass = "temperature";
    if (index >= 5) {
        uint8_t probe = index - 5;
        if (probe >= MAX_WATER_PROBES) {
            discoveryActive = false;
            return;
        }
        snprintf(object, sizeof(object), "water_%u", probe + 1);
        snprintf(name, sizeof(name), "Water Temp %u", probe + 1);
        snprintf(value, sizeof(value), "{{value_json.water[%u].temp_f}}", probe);
        stateTopic = TOPIC_WATER;
    } else {
        static const char* const objects[] = {"exhaust", "tank_temp", "fan", "state_text", "outdoor_temp"};
        static const char* const names[] = {"Exhaust Temp", "Tank Temp", "Fan Output", "Burn Phase", "Outdoor Temp"};
        static const char* const values[] = {"exhaust", "tank_temp", "fan", "state_text", "temp"};
        snprintf(object, sizeof(object), "%s", objects[index]);
        snprintf(name, sizeof(name), "%s", names[index]);
        snprintf(value, sizeof(value), "{{value_json.%s}}", values[index]);
        if (index == 2 || index == 3) {
            unit = index == 2 ? "%" : nullptr;
            deviceClass = nullptr;
        }
        if (index == 4) stateTopic = TOPIC_OUTDOOR;
    }
    snprintf(topic, sizeof(topic), "homeassistant/sensor/boiler_assistant/%s/config", object);
    StaticJsonDocument<512> doc;
    doc["name"] = name;
    doc["uniq_id"] = String("boiler_assistant_") + object;
    doc["stat_t"] = stateTopic;
    doc["val_tpl"] = value;
    if (unit) doc["unit_of_meas"] = unit;
    if (deviceClass) doc["dev_cla"] = deviceClass;
    JsonObject device = doc.createNestedObject("dev");
    device["ids"] = "boiler_assistant";
    device["name"] = "Boiler Assistant";
    device["mf"] = "Karl";
    device["mdl"] = "UNO R4 WiFi";
    device["sw"] = "3.4";
    if (publishJson(topic, doc, true)) ++discoveryIndex;
}

static bool mqtt_publishState() {
    StaticJsonDocument<512> doc;
    float tankTemp = NAN;
    uint8_t probe = sys.probeRoleMap[PROBE_TANK];
    bool tankOK = probe < sys.waterProbeCount && isfinite(sys.waterTempF[probe]) &&
                  millis() - sys.waterTempLastGoodMs[probe] <= 3000UL;
    if (tankOK) tankTemp = sys.waterTempF[probe];
    doc["exhaust"] = sys.exhaustSmoothF;
    doc["tank_temp"] = tankTemp;
    doc["tank_sensor_ok"] = tankOK;
    doc["fan"] = sys.fanFinal;
    const char* phase = "IDLE";
    switch (sys.burnState) {
        case BURN_BOOST: phase = "BOOST"; break;
        case BURN_RAMP: phase = "RAMP"; break;
        case BURN_HOLD: phase = "HOLD"; break;
        case BURN_EMBER_GUARD: phase = "EMBER GUARDIAN"; break;
        default: break;
    }
    doc["state_text"] = phase;
    doc["safety_state"] = sys.safetyState;
    doc["exhaust_fallback"] = sys.exhaustFallbackActive;
    return publishJson(TOPIC_STATE, doc);
}

static bool mqtt_publishWater() {
    // Eight probe objects exceed the controller's 1 KB main stack.
    static StaticJsonDocument<1536> doc;
    doc.clear();
    JsonArray water = doc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount && i < MAX_WATER_PROBES; ++i) {
        JsonObject probe = water.createNestedObject();
        bool valid = isfinite(sys.waterTempF[i]) &&
                     millis() - sys.waterTempLastGoodMs[i] <= 3000UL;
        probe["index"] = i;
        probe["name"] = sys.waterProbeNames[i];
        probe["temp_f"] = valid ? sys.waterTempF[i] : NAN;
        probe["sensor_ok"] = valid;
        probe["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
    }
    doc["count"] = water.size();
    return publishJson(TOPIC_WATER, doc);
}

static bool mqtt_publishOutdoor() {
    StaticJsonDocument<128> doc;
    doc["temp"] = sys.envSensorOK ? sys.envTempF : NAN;
    return publishJson(TOPIC_OUTDOOR, doc);
}

static bool mqtt_publishAlert(const char* code, const char* message, bool active) {
    StaticJsonDocument<256> doc;
    doc["active"] = active;
    doc["code"] = code;
    doc["message"] = message;
    doc["timestamp_ms"] = millis();
    return publishJson(TOPIC_ALERT, doc);
}

static void mqtt_publishAlertTransitions() {
    bool highTemp = sys.safetyState == SAFETY_HIGHTEMP;
    bool tankFault = sys.safetyState == SAFETY_SENSOR_FAULT &&
                     (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0;
    bool exhaustFallback = sys.exhaustFallbackActive;
    bool guardian = sys.emberGuardianLatched || sys.burnState == BURN_EMBER_GUARD;
    if (highTemp != lastHighTemp &&
        mqtt_publishAlert("HIGH_TEMP", highTemp ? "HIGH TEMPERATURE LOCKOUT" : "HIGH TEMPERATURE CLEARED", highTemp))
        lastHighTemp = highTemp;
    if (tankFault != lastTankFault &&
        mqtt_publishAlert("TANK_PROBE_FAULT", tankFault ? "TANK PROBE FAULT - SYSTEM STOPPED" : "TANK PROBE FAULT CLEARED", tankFault))
        lastTankFault = tankFault;
    if (exhaustFallback != lastExhaustFallback &&
        mqtt_publishAlert("EXHAUST_PROBE_FAULT", exhaustFallback ? "EXHAUST PROBE NEEDS CLEANING OR REPLACEMENT - FAN AT MAX CLAMP" : "EXHAUST PROBE FAULT CLEARED", exhaustFallback))
        lastExhaustFallback = exhaustFallback;
    if (guardian != lastGuardian &&
        mqtt_publishAlert("EMBER_GUARDIAN", guardian ? "EMBER GUARDIAN ACTIVE" : "EMBER GUARDIAN CLEARED", guardian))
        lastGuardian = guardian;
}

static void mqtt_reconnect() {
    unsigned long now = millis();
    if (!sys.wifiOK || mqtt.connected() || now - lastReconnectAttempt < 30000UL) return;
    lastReconnectAttempt = now;
    if (mqtt.connect(prov_mqtt_server, 1883)) {
        discoveryIndex = 0;
        discoveryActive = true;
        lastHighTemp = lastTankFault = lastExhaustFallback = lastGuardian = false;
    } else {
        DBG_PRINT("MQTT: fail ");
        DBG_PRINTLN(mqtt.connectError());
    }
}

void mqtt_init() {
    mqttAuthConfigured = prov_mqtt_server && prov_mqtt_server[0] &&
                         prov_mqtt_user && prov_mqtt_user[0] &&
                         prov_mqtt_pass && prov_mqtt_pass[0];
    if (!mqttAuthConfigured) {
        DBG_PRINTLN("MQTT: no auth; off");
        return;
    }
    mqtt.setId("BoilerAssistant");
    mqtt.setUsernamePassword(prov_mqtt_user, prov_mqtt_pass);
    mqtt.setKeepAliveInterval(15000UL);
    mqtt.setConnectionTimeout(1000UL);
    wifiClient.setConnectionTimeout(1000);
}

void mqtt_stop() {
    discoveryActive = false;
    lastReconnectAttempt = millis();
    mqtt.stop();
    wifiClient.stop();
}

void mqtt_loop() {
    if (wifi_prov_isAPMode() || !mqttAuthConfigured || !sys.wifiOK) return;
    unsigned long now = millis();
    if (now - lastMqttIoMs < 100UL) return;
    lastMqttIoMs = now;
    mqtt_reconnect();
    if (!mqtt.connected()) return;
    mqtt.poll();
    mqtt_publishAlertTransitions();
    if (discoveryActive) {
        publishDiscovery();
        return;
    }
    static uint8_t nextPublisher = 0;
    for (uint8_t checked = 0; checked < 3; ++checked) {
        uint8_t publisher = nextPublisher;
        nextPublisher = (nextPublisher + 1) % 3;
        switch (publisher) {
            case 0:
                if (now - lastWaterMs < MQTT_READING_INTERVAL_MS) break;
                mqtt_publishWater();
                lastWaterMs = now;
                return;
            case 1:
                if (now - lastStateFastMs < MQTT_READING_INTERVAL_MS) break;
                mqtt_publishState();
                lastStateFastMs = now;
                return;
            case 2:
                if (now - lastOutdoorBmeMs < MQTT_READING_INTERVAL_MS) break;
                mqtt_publishOutdoor();
                lastOutdoorBmeMs = now;
                return;
        }
    }
}

#endif // BOILER_MQTT_ENABLED
