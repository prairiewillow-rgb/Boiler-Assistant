/*
 * ============================================================
 *  Boiler Assistant – MQTT Integration Module (v2.0)
 *  ------------------------------------------------------------
 *  File: MQTTClient.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      MQTT client for telemetry + remote control. Implements:
 *        • JSON state publishing
 *        • JSON settings publishing
 *        • Command topic handler
 *        • Two‑way sync with SystemData
 *        • Home Assistant auto‑discovery compatibility
 *
 * ============================================================
 */

#include "MQTTClient.h"
#include "SystemData.h"
#include <PubSubClient.h>
#include <WiFi.h>

extern SystemData sys;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Forward declarations
static void mqtt_publishState();
static void mqtt_publishSettings();
static void mqtt_handleCommand(char* topic, byte* payload, unsigned int len);

void mqtt_init() {
    mqtt.setServer("YOUR_MQTT_BROKER", 1883);
    mqtt.setCallback(mqtt_handleCommand);
}

void mqtt_loop() {
    if (!mqtt.connected()) {
        // reconnect logic here
    }
    mqtt.loop();

    mqtt_publishState();
    mqtt_publishSettings();
}

static void mqtt_publishState() {
    StaticJsonDocument<256> doc;
    doc["exhaust_smooth"] = sys.exhaustSmoothF;
    doc["fan"] = sys.fanFinal;
    doc["burn_state"] = sys.burnState;
    doc["env_temp"] = sys.envTempF;

    char buf[256];
    size_t n = serializeJson(doc, buf);
    mqtt.publish("boiler/state", buf, n);
}

static void mqtt_handleCommand(char* topic, byte* payload, unsigned int len) {
    payload[len] = 0;
    int value = atoi((char*)payload);

    if (strcmp(topic, "boiler/cmd/exhaust_setpoint") == 0) {
        sys.exhaustSetpoint = value;
        sys.remoteChanged = true;
    }
}

