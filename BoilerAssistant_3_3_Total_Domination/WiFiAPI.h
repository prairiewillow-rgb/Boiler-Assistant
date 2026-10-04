/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API (v3.3.8 "Total Domination")
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
 *      â€¢ wifiapi_init() â€” initialize WiFi hardware + HTTP server
 *      â€¢ wifiapi_loop() â€” nonâ€‘blocking retry + request handler
 *
 *    Responsibilities:
 *      - Maintain nonâ€‘blocking WiFi autoâ€‘retry logic
 *      - Serve lightweight JSON endpoints for:
 *          â€¢ Live telemetry
 *          â€¢ Settings
 *          â€¢ Network diagnostics
 *      - Integrate cleanly with MQTT without blocking
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiAPI.cpp
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.3.8 "Total Domination"
 * ============================================================
 */

#pragma once

// Initialize WiFi + HTTP JSON API (nonâ€‘blocking)
void wifiapi_init();

// Releases the listening socket so the WiFi bridge is free (used before OTA).
void wifiapi_stop();

// Run WiFi retry + HTTP server loop (nonâ€‘blocking)
void wifiapi_loop();

