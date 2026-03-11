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
// ------------------------------------------------------------
// WiFi Credentials (placeholders)
// ------------------------------------------------------------
#define SECRET_SSID        "YOUR_WIFI_SSID"
#define SECRET_PASS        "YOUR_WIFI_PASSWORD"

// ------------------------------------------------------------
// MQTT Broker Credentials (placeholders)
// ------------------------------------------------------------
#define SECRET_MQTT_SERVER "MQTT_SERVER_IP"
#define SECRET_MQTT_USER   "MQTT_USERNAME"
#define SECRET_MQTT_PASS   "MQTT_PASSWORD"

// ------------------------------------------------------------
// OTA Password (placeholder)
// ------------------------------------------------------------
#define SECRET_OTA_PASS    "YOUR_OTA_PASSWORD"

