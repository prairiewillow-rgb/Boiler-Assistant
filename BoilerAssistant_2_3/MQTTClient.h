/*
 * ============================================================
 *  Boiler Assistant – MQTT Client API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: MQTT_Client.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the WiFi/MQTT subsystem.
 *      Provides:
 *          • mqtt_init() — initialize WiFi + MQTT client
 *          • mqtt_loop() — non‑blocking RX/TX handler
 *
 *      Responsibilities:
 *          - Maintain MQTT connection (auto‑reconnect)
 *          - Dispatch inbound command messages
 *          - Trigger periodic telemetry publishers
 *          - Support Home Assistant Discovery
 *
 *      Notes:
 *          - All implementation lives in MQTT_Client.cpp
 *          - No blocking calls allowed in the loop
 *          - Fully compatible with SystemData v2.3‑Environmental
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

// Initialize WiFi + MQTT subsystem
void mqtt_init();

// Non‑blocking MQTT loop (called from main loop)
void mqtt_loop();

#endif
