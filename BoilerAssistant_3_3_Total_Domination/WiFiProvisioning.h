/*
 * ============================================================
 *  Boiler Assistant – WiFi Provisioning API (v3.3.8 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiProvisioning.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi provisioning subsystem.
 *    This module exposes deterministic entry points for:
 *
 *      â€¢ wifi_prov_init()  â€” STAâ€‘first initialization with AP fallback
 *      â€¢ wifi_prov_loop()  â€” APâ€‘mode HTML portal handler
 *      â€¢ wifi_prov_isAPMode() â€” query active provisioning mode
 *      â€¢ wifi_prov_has_credentials() â€” query stored credentials
 *      â€¢ wifi_prov_factoryReset() â€” full credential wipe + reboot
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiProvisioning.cpp
 *      - Provisioning is authoritative when STA fails or no creds exist
 *      - SystemData is the single source of truth for WiFi status
 *
 *  Version:
 *      Boiler Assistant v3.3.8 "Total Domination"
 * ============================================================
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>

void wifi_prov_init();
void wifi_prov_loop();
bool wifi_prov_isAPMode();
bool wifi_prov_has_credentials();

/* ============================================================
 *  Factory Reset API
 * ============================================================ */
void wifi_prov_factoryReset();

#endif
