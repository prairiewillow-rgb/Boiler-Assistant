/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: WiFiAPI.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the WiFi + HTTP JSON API subsystem.
 *      Responsibilities:
 *        • Initialize WiFi hardware and JSON endpoints
 *        • Maintain non‑blocking retry logic
 *        • Serve lightweight HTTP JSON responses for:
 *            - Live telemetry
 *            - Settings
 *            - Network diagnostics
 *
 *      Notes:
 *        - Implementation lives in WiFiAPI.cpp
 *        - Designed to run alongside MQTT and LoRa without blocking
 *        - Called once per second from the main loop
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#pragma once

// Initialize WiFi + HTTP JSON API (non‑blocking)
void wifiapi_init();

// Run WiFi retry + HTTP server loop (non‑blocking)
void wifiapi_loop();
