/*
 * Boiler Assistant - WiFiProvisioning
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <IPAddress.h>

constexpr char BOILER_WIFI_HOSTNAME[] = "boilerassistant";

// Set to 0 to omit signal-strength bridge queries for network isolation.
#define BOILER_WIFI_RSSI_ENABLED 1

void wifi_prov_init();
void wifi_prov_loop();
void wifi_prov_networkLoop();
IPAddress wifi_prov_cachedIP();
int32_t wifi_prov_cachedRSSI();
// Text of the 30 s NET serial line, also sent by /api/diag. Static buffer. The largest-free-block
// probe allocates and frees temporary blocks, so only the on-demand report asks for it.
const char* wifi_prov_netText(bool probeLargestBlock = false);
// Records the free heap so the report can show the lowest value seen.
void wifi_prov_sampleHeap();
unsigned long wifi_prov_cachedUTC();
bool wifi_prov_prepareIo();
// Correct response matching for the WiFiS3 1.6.0 hostname command.
bool wifi_prov_setHostname();
bool wifi_prov_applyNetworkConfig();
// Request WiFi-bridge recovery after an explicit service failure.
void wifi_prov_requestRecovery(const char* source);
const char* wifi_prov_saveNetwork(const String& body, bool reset);
bool wifi_prov_networkChangePending();
void wifi_prov_applySavedNetwork();
// One bridge command may wait this long for its reply. Reaching it means the
// reply was abandoned; a late reply would be misread, so the bridge is reset.
#define WIFI_STEP_LIMIT_MS 5000UL
// Socket sends: the bridge retries a stalled send (phone asleep or out of
// range) for up to ~10 s before answering. Wait for that answer instead of
// resetting the bridge. Stays below the 16 s watchdog.
#define WIFI_SEND_LIMIT_MS 12000UL
bool wifi_prov_prepareClientIo(unsigned long limitMs = WIFI_STEP_LIMIT_MS);
// A whole loop pass (several commands) is only a backstop on slow links.
#define WIFI_PASS_LIMIT_MS 15000UL
void wifi_prov_finishIo(unsigned long startedMs, const char* source,
                        unsigned long limitMs = WIFI_STEP_LIMIT_MS);
bool wifi_prov_isAPMode();
bool wifi_prov_has_credentials();

/* ============================================================
 *  Factory Reset API
 * ============================================================ */
void wifi_prov_factoryReset();

#endif
