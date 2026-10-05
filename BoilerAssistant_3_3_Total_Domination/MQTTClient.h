/*
 * ============================================================
 *  Boiler Assistant - MQTT Client API (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: MQTTClient.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi/MQTT subsystem under the
 *    Total Domination Architecture (TDA). This module exposes
 *    the deterministic entry points used by the main loop to
 *    maintain MQTT connectivity and dispatch inbound commands.
 *
 *    Responsibilities:
 *      - mqtt_init() -- configure MQTT authentication and transport
 *      - mqtt_loop() -- paced RX/TX and incremental discovery
 *      - Auto-reconnect logic (rate-limited, deterministic)
 *      - Home Assistant Discovery support
 *      - Periodic telemetry publishers (state, settings, water, outdoor)
 *
 *    Architectural Notes:
 *      - All implementation resides in MQTTClient.cpp
 *      - WiFiS3 calls are synchronous with requested connection/AT
 *        timeouts; quarantine gates further calls after slow transport
 *      - SystemData is the single source of truth
 *      - No burn logic, UI logic, or EEPROM logic belongs here
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

// Initialize WiFi + MQTT subsystem
void mqtt_init();

// Closes the broker connection so the WiFi bridge is free (used before OTA).
void mqtt_stop();

// Non-blocking MQTT loop (called from main loop)
void mqtt_loop();

#endif
