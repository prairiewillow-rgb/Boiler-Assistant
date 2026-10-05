/*
 * ============================================================
 *  Boiler Assistant - WiFi Provisioning API (v3.3.9 "Total Domination")
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
 *      - wifi_prov_init() -- saved STA initialization, AP without credentials
 *      - wifi_prov_networkLoop() -- link monitoring and paced reconnects
 *      - wifi_prov_loop()  - AP-mode HTML portal handler
 *      - wifi_prov_isAPMode() - query active provisioning mode
 *      - wifi_prov_has_credentials() - query stored credentials
 *      - wifi_prov_factoryReset() - full credential wipe + reboot
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiProvisioning.cpp
 *      - AP setup is used only when no saved credentials exist
 *      - Saved-network outages stay offline and retry without resetting control
 *      - wifi_prov_networkLoop owns sys.wifiOK and socket restart on link/IP loss
 *      - Retry spacing: 30/60/120s; service restart requires 5s stable link
 *      - Runtime AT response waits are 3s, not a total loop deadline
 *      - Slow operations quarantine I/O; acknowledged bridge reset,
 *        5s reboot wait and staged cleanup precede reconnect
 *      - Reset requires compatible bridge firmware (0.5.0+ baseline);
 *        failed acknowledgement stays offline and retries after 60s
 *      - Cached IP/RSSI avoid UI/control modem calls; UTC expires in 24h
 *      - Interactive OTA intentionally retains the library's own timeouts
 *      - SystemData is the single source of truth for WiFi status
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <IPAddress.h>

void wifi_prov_init();
void wifi_prov_loop();
void wifi_prov_networkLoop();
IPAddress wifi_prov_cachedIP();
int32_t wifi_prov_cachedRSSI();
unsigned long wifi_prov_cachedUTC();
bool wifi_prov_prepareIo();
bool wifi_prov_prepareClientIo();
void wifi_prov_finishIo(unsigned long startedMs, const char* source);
bool wifi_prov_isAPMode();
bool wifi_prov_has_credentials();

/* ============================================================
 *  Factory Reset API
 * ============================================================ */
void wifi_prov_factoryReset();

#endif
