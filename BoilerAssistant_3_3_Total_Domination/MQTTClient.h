/*
 * Boiler Assistant - MQTTClient
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

// Temporary isolation test: set to 1 to restore MQTT, then compile/upload.
#define BOILER_MQTT_ENABLED 0

// Initialize WiFi + MQTT subsystem
void mqtt_init();

// Closes the broker connection so the WiFi bridge is free (used before OTA).
void mqtt_stop();

// Non-blocking MQTT loop (called from main loop)
void mqtt_loop();

#endif
