/*
 * ============================================================
 *  Boiler Assistant - WiFi JSON API (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi + HTTP JSON API subsystem.
 *    This module exposes deterministic entry points for:
 *
 *      - wifiapi_init() -- start HTTP server after stable STA connection
 *      - wifiapi_loop() -- incremental request/response handler
 *
 *    Responsibilities:
 *      - WiFiProvisioning owns association, link monitoring and retry
 *      - Serve lightweight JSON endpoints for:
 *          - Live telemetry
 *          - Settings
 *          - Network diagnostics
 *      - Bounded HTTP chunks share loop time with MQTT; underlying
 *        modem operations remain synchronous and recovery-gated
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiAPI.cpp
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#pragma once

// Initialize WiFi + HTTP JSON API (non-blocking)
void wifiapi_init();

// Releases the listening socket so the WiFi bridge is free (used before OTA).
void wifiapi_stop();

// Run WiFi retry + HTTP server loop (non-blocking)
void wifiapi_loop();
