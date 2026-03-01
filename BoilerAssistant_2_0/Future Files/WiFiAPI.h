/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v2.0)
 *  ------------------------------------------------------------
 *  File: WiFiAPI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      HTTP JSON API for dashboards and remote control.
 *      Provides:
 *        • /api/state endpoint
 *        • /api/settings endpoint
 *        • /api/set POST handler
 *        • Two‑way sync with SystemData
 *
 * ============================================================
 */

#ifndef WIFI_API_H
#define WIFI_API_H

void wifiapi_init();
void wifiapi_loop();

#endif
