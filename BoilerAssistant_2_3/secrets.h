/*
 * ============================================================
 *  Boiler Assistant – Secrets Template (v2.3)
 *  ------------------------------------------------------------
 *  File: secrets.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      This file stores all private credentials required by the
 *      Boiler Assistant firmware. You must replace each placeholder
 *      with their real credentials before compiling.
 *
 *      Required fields:
 *        • WiFi SSID + password
 *        • MQTT broker address + credentials
 *        • OTA password (optional but recommended)
 *
 *      Notes:
 *        - NEVER commit real credentials to version control.
 *        - This file is intentionally excluded via .gitignore.
 *        - Keep this file local to your device or build system.
 *
 *  Version:
 *      Boiler Assistant v2.3
 * ============================================================
 */

// ------------------------------------------------------------
// WiFi Credentials
// ------------------------------------------------------------
// Replace with your WiFi network name and password.
#define SECRET_SSID "Skynet"
#define SECRET_PASS "Kz67Gb42a"


// ------------------------------------------------------------
// MQTT Broker Credentials
// ------------------------------------------------------------
// Replace with your Home Assistant / Mosquitto broker details.
#define SECRET_MQTT_SERVER "192.168.1.182" // Home Assistant IP
#define SECRET_MQTT_USER   "WoodBoiler"    // HA MQTT Username
#define SECRET_MQTT_PASS   "Kz67Gb42a"     // HA MQTT Password


// ------------------------------------------------------------
// OTA Password (ArduinoOTA) future
// ------------------------------------------------------------
// Set a strong password for over‑the‑air firmware updates.
#define SECRET_OTA_PASS    "OTA_PASSWORD"
