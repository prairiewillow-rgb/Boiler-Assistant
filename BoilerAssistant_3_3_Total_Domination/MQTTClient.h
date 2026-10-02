/*
 * ============================================================
 *  Boiler Assistant â€“ MQTT Client API (v3.3.4 "Total Domination")
 *  ------------------------------------------------------------
 *  File: MQTT_Client.h
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
 *      â€¢ mqtt_init() â€” initialize WiFi + MQTT client
 *      â€¢ mqtt_loop() â€” fully nonâ€‘blocking RX/TX handler
 *      â€¢ Autoâ€‘reconnect logic (rateâ€‘limited, deterministic)
 *      â€¢ Home Assistant Discovery support
 *      â€¢ Periodic telemetry publishers (state, settings, water, outdoor)
 *
 *    Architectural Notes:
 *      - All implementation resides in MQTTClient.cpp
 *      - No blocking calls allowed in mqtt_loop()
 *      - SystemData is the single source of truth
 *      - No burn logic, UI logic, or EEPROM logic belongs here
 *
 *  Version:
 *      Boiler Assistant v3.3.4 "Total Domination"
 * ============================================================
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

// Initialize WiFi + MQTT subsystem
void mqtt_init();

// Closes the broker connection so the WiFi bridge is free (used before OTA).
void mqtt_stop();

// Nonâ€‘blocking MQTT loop (called from main loop)
void mqtt_loop();

#endif

