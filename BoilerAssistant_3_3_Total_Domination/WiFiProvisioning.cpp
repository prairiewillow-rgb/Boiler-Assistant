/*
 * ============================================================
 *  Boiler Assistant - WiFi Provisioning Module (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiProvisioning.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic WiFi provisioning subsystem for the Boiler
 *    Assistant controller. Implements the Total Domination
 *    Architecture (TDA) for all credential handling and AP-mode
 *    onboarding.
 *
 *    Responsibilities:
 *      - STA-first connection using RuntimeCredentials
 *      - Saved-network retries with 30/60/120s backoff during outages
 *      - AP provisioning portal only when no saved credentials exist
 *      - Safe credential parsing + EEPROM persistence
 *      - Factory reset with full credential wipe
 *      - Export MQTT credentials for MQTTClient.cpp
 *
 *    Architectural Notes:
 *      - No blocking delays beyond required WiFi operations
 *      - No dynamic allocation except small String buffers
 *      - SystemData is the single source of truth for WiFi status
 *      - Saved credentials never fall back to AP after a missed join
 *      - One-second link checks; sockets restart after 5s stable link
 *      - WiFi.setTimeout(0) skips the core's association wait; the
 *        underlying modem commands are still synchronous
 *      - Runtime AT waits are 3s per command, not a total loop bound
 *      - Slow I/O is quarantined; bridge reset must be acknowledged
 *        before staged cleanup/reconnect (compatible firmware required)
 *      - UI/telemetry use cached network data; control uses cached UTC
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#include "WiFiProvisioning.h"
#include "RuntimeCredentials.h"
#include "SystemData.h"
#include "EEPROMStorage.h"   // <-- required for persistence
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "PushNotify.h"

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiServer.h>
#include <OTAUpdate.h>
#include "OTAUpdater.h"

extern SystemData sys;

/* ============================================================
 *  MQTT GLOBALS (used by MQTTClient.cpp)
 * ============================================================ */

const char* prov_mqtt_server = nullptr;
const char* prov_mqtt_user   = nullptr;
const char* prov_mqtt_pass   = nullptr;

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static WiFiServer provServer(80);
static bool apMode   = false;
static bool newCreds = false;
static unsigned long lastNetworkCheckMs = 0;
static unsigned long lastJoinAttemptMs = 0;
static unsigned long joinRetryMs = 30000UL;
static unsigned long linkStableSinceMs = 0;
static bool linkDetected = false;
static bool servicesOnline = false;
static IPAddress serviceIP;
static int32_t cachedRSSI = 0;
static unsigned long lastRSSIMs = 0;
static unsigned long cachedUTC = 0;
static unsigned long utcAnchorMs = 0;
static unsigned long lastTimeAttemptMs = 0;
static bool timeAttempted = false;
static const unsigned long MODEM_RUNTIME_TIMEOUT_MS = 3000UL;
enum BridgeRecovery { BRIDGE_NORMAL, BRIDGE_QUIET, BRIDGE_BOOT_WAIT,
                      BRIDGE_CLOSE_HTTP, BRIDGE_CLOSE_MQTT, BRIDGE_CLOSE_PUSH };
static BridgeRecovery bridgeRecovery = BRIDGE_NORMAL;
static unsigned long bridgeRecoveryMs = 0;
static unsigned long bridgeQuietIntervalMs = 15000UL;
static bool bridgeWasReset = false;

IPAddress wifi_prov_cachedIP() { return serviceIP; }
int32_t wifi_prov_cachedRSSI() { return cachedRSSI; }

unsigned long wifi_prov_cachedUTC() {
    unsigned long elapsed = millis() - utcAnchorMs;
    if (cachedUTC == 0 || elapsed >= 86400000UL) return 0;
    return cachedUTC + elapsed / 1000UL;
}

bool wifi_prov_prepareIo() {
    if (bridgeRecovery != BRIDGE_NORMAL || ota_isActive()) return false;
    // This bounds AT response waits, not just TCP connection/association waits.
    modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
    return true;
}

bool wifi_prov_prepareClientIo() {
    if (bridgeRecovery == BRIDGE_QUIET || bridgeRecovery == BRIDGE_BOOT_WAIT) return false;
    if (ota_isActive()) {
        // OTA explicitly closes runtime sockets after entering its safe state.
        modem.timeout(MODEM_TIMEOUT);
        return true;
    }
    modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
    return true;
}

void wifi_prov_finishIo(unsigned long startedMs, const char* source) {
    if (ota_isActive() || bridgeRecovery == BRIDGE_QUIET || bridgeRecovery == BRIDGE_BOOT_WAIT ||
        millis() - startedMs < MODEM_RUNTIME_TIMEOUT_MS) return;
    Serial.print("WiFi: slow/unresponsive modem operation: ");
    Serial.println(source);
    sys.wifiOK = false;
    bridgeRecovery = BRIDGE_QUIET;
    bridgeRecoveryMs = millis();
    bridgeQuietIntervalMs = 15000UL;
    bridgeWasReset = false;
    // Do not issue socket-close commands into a possibly unfinished AT reply.
    // Quiet period, acknowledged bridge reset and reboot wait precede cleanup.
}

static bool serviceBridgeRecovery() {
    if (bridgeRecovery == BRIDGE_NORMAL) return false;
    unsigned long now = millis();
    if (bridgeRecovery == BRIDGE_QUIET) {
        if (now - bridgeRecoveryMs < bridgeQuietIntervalMs) return true;
        modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
        OTAUpdate bridge;
        if (bridge.reset() != 0) {
            Serial.println("WiFi: bridge reset not acknowledged; staying offline for 60s");
            bridgeQuietIntervalMs = 60000UL;
            bridgeRecoveryMs = millis();
            return true;
        }
        Serial.println("WiFi: bridge reset acknowledged; waiting for reboot");
        bridgeWasReset = true;
        bridgeRecovery = BRIDGE_BOOT_WAIT;
        bridgeRecoveryMs = millis();
        return true;
    }
    if (bridgeRecovery == BRIDGE_BOOT_WAIT) {
        if (now - bridgeRecoveryMs < 5000UL) return true;
        bridgeRecovery = BRIDGE_CLOSE_HTTP;
        return true;
    }
    modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
    unsigned long cleanupStartMs = millis();
    if (bridgeRecovery == BRIDGE_CLOSE_HTTP) {
        wifiapi_stop();
        if (bridgeRecovery == BRIDGE_QUIET) return true;
        bridgeRecovery = BRIDGE_CLOSE_MQTT;
    } else if (bridgeRecovery == BRIDGE_CLOSE_MQTT) {
        mqtt_stop();
        if (bridgeRecovery == BRIDGE_QUIET) return true;
        bridgeRecovery = BRIDGE_CLOSE_PUSH;
    } else if (bridgeRecovery == BRIDGE_CLOSE_PUSH) {
        pushnotify_networkLost();
        if (bridgeRecovery == BRIDGE_QUIET) return true;
        servicesOnline = false;
        linkDetected = false;
        serviceIP = IPAddress();
        cachedRSSI = 0;
        joinRetryMs = 30000UL;
        lastJoinAttemptMs = bridgeWasReset ? millis() - joinRetryMs : millis();
        bridgeRecovery = BRIDGE_NORMAL;
        Serial.println("WiFi: socket cleanup complete; saved-network retries resume");
    }
    if (millis() - cleanupStartMs >= MODEM_RUNTIME_TIMEOUT_MS) {
        bridgeRecovery = BRIDGE_QUIET;
        bridgeRecoveryMs = millis();
        bridgeQuietIntervalMs = 15000UL;
        bridgeWasReset = false;
        Serial.println("WiFi: socket cleanup stalled; quarantining bridge again");
    }
    return true;
}

static void beginStationAttempt() {
    // Renesas WiFiS3 checks _timeout only after sending BEGINSTA. Zero skips
    // its 10-second status spin; association/DHCP are checked on later passes.
    WiFi.setTimeout(0);
    WiFi.begin(runtimeCreds.ssid, runtimeCreds.pass);
    lastJoinAttemptMs = millis();
    Serial.println("WiFi: station join requested; control continues offline");
}

/* Simple HTML portal */
static const char* PROV_HTML =
"<!DOCTYPE html><html><body>"
"<h2>Boiler Assistant WiFi Setup</h2>"
"<form method='POST'>"
"Your name:<br><input name='displayName' maxlength='31'><br><br>"
"WiFi SSID:<br><input name='ssid'><br>"
"WiFi Password:<br><input name='pass' type='password'><br><br>"
"MQTT Server:<br><input name='mqttServer'><br>"
"MQTT User:<br><input name='mqttUser'><br>"
"MQTT Password:<br><input name='mqttPass' type='password'><br><br>"
"Control/API Password:<br><input name='controlPass' type='password'><br><br>"
"<input type='submit' value='Save'>"
"</form></body></html>";

/* ============================================================
 *  FACTORY RESET
 * ============================================================ */

void wifi_prov_factoryReset() {
    Serial.println("WiFiProvisioning: FACTORY RESET");

    memset(&runtimeCreds, 0, sizeof(runtimeCreds));
    runtimeCreds.hasCredentials = false;

    sys.wifiOK = false;

    // Persist the cleared credentials
    eeprom_saveRuntimeCreds();

    Serial.println("WiFiProvisioning: rebooting after factory reset...");
    delay(1000);
    NVIC_SystemReset();
}

/* ============================================================
 *  AP MODE
 * ============================================================ */

static void startAP() {
    Serial.println("WiFiProvisioning: Starting AP mode...");

    WiFi.end();
    WiFi.disconnect();
    delay(200);

    WiFi.config(
        IPAddress(192,168,4,1),
        IPAddress(0,0,0,0),
        IPAddress(255,255,255,0)
    );

    int result = WiFi.beginAP("BoilerAssistant-Setup");

    Serial.print("WiFiProvisioning: AP start result = ");
    Serial.println(result);

    apMode = true;
    sys.wifiOK = false;

    provServer.begin();
}

/* ============================================================
 *  INIT: saved STA retries, AP only without credentials
 * ============================================================ */

void wifi_prov_init() {
    Serial.println("WiFiProvisioning: init (saved STA retries; AP only without credentials)");

    sys.wifiOK = false;
    WiFi.setHostname("boilerassistant");

    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0) {
        Serial.print("WiFiProvisioning: Using runtime SSID: ");
        Serial.println(runtimeCreds.ssid);

        WiFi.end();
        WiFi.disconnect();
        delay(200);

        apMode = false;
        prov_mqtt_server = runtimeCreds.mqttServer;
        prov_mqtt_user = runtimeCreds.mqttUser;
        prov_mqtt_pass = runtimeCreds.mqttPass;
        beginStationAttempt();
        return;
    }

    Serial.println("WiFiProvisioning: No runtime credentials â†’ AP mode");
    startAP();
}

/* ============================================================
 *  STATUS QUERIES
 * ============================================================ */

bool wifi_prov_has_credentials() {
    return runtimeCreds.hasCredentials;
}

bool wifi_prov_isAPMode() {
    return apMode;
}

void wifi_prov_networkLoop() {
    if (apMode || !runtimeCreds.hasCredentials || runtimeCreds.ssid[0] == '\0') return;
    if (cachedUTC != 0 && millis() - utcAnchorMs >= 86400000UL) {
        cachedUTC = 0;
        Serial.println("WiFi: cached clock expired; scheduled self-clean waits for time sync");
    }
    if (ota_isActive() || serviceBridgeRecovery()) return;
    unsigned long now = millis();
    if (now - lastNetworkCheckMs < 1000UL) return;
    lastNetworkCheckMs = now;

    unsigned long operationMs = millis();
    bool connected = WiFi.status() == WL_CONNECTED;
    wifi_prov_finishIo(operationMs, "WiFi status");
    if (bridgeRecovery != BRIDGE_NORMAL) return;
    IPAddress ip;
    if (connected) {
        operationMs = millis();
        ip = WiFi.localIP();
        wifi_prov_finishIo(operationMs, "WiFi IP");
        if (bridgeRecovery != BRIDGE_NORMAL) return;
        connected = ip != IPAddress(0, 0, 0, 0);
    }

    if (servicesOnline && (!connected || ip != serviceIP)) {
        sys.wifiOK = false;
        servicesOnline = false;
        bridgeWasReset = false;
        bridgeRecovery = BRIDGE_CLOSE_HTTP;
        linkDetected = false;
        lastJoinAttemptMs = millis();
        joinRetryMs = 30000UL;
        Serial.println("WiFi: link/IP lost; local control stays active");
        serviceIP = IPAddress();
        cachedRSSI = 0;
        return;
    }

    if (connected) {
        if (!linkDetected) {
            linkDetected = true;
            linkStableSinceMs = now;
        }
        // Avoid repeatedly opening sockets on a link that is flapping.
        if (!servicesOnline && now - linkStableSinceMs >= 5000UL) {
            sys.wifiOK = true;
            servicesOnline = true;
            serviceIP = ip;
            joinRetryMs = 30000UL;
            wifiapi_init();
            Serial.print("WiFi: stable link restored. Dashboard IP: ");
            Serial.println(ip);
            return;
        }
        if (servicesOnline) {
            if (!timeAttempted || now - lastTimeAttemptMs >= 60000UL) {
                timeAttempted = true;
                lastTimeAttemptMs = now;
                unsigned long utc = WiFi.getTime();
                if (utc != 0) {
                    cachedUTC = utc;
                    utcAnchorMs = millis();
                } else {
                    Serial.println("WiFi: time unavailable; retaining unexpired cached clock");
                }
            } else if (now - lastRSSIMs >= 15000UL) {
                lastRSSIMs = now;
                cachedRSSI = WiFi.RSSI();
            }
        }
        return;
    }

    linkDetected = false;
    sys.wifiOK = false;
    if (now - lastJoinAttemptMs < joinRetryMs) return;
    beginStationAttempt();
    // A marginal network gets breathing room: 30s, 60s, then 120s.
    joinRetryMs = min(joinRetryMs * 2UL, 120000UL);
}

/* ============================================================
 *  FORM PARSER
 * ============================================================ */

static void parseForm(const String& body) {
    auto getVal = [&](const String& key) {
        int p = body.indexOf(key + "=");
        if (p < 0) return String("");
        int s = p + key.length() + 1;
        int e = body.indexOf('&', s);
        if (e < 0) e = body.length();
        String v = body.substring(s, e);
        v.replace('+', ' ');
        return v;
    };

    String ssid       = getVal("ssid");
    String pass       = getVal("pass");
    String mqttServer = getVal("mqttServer");
    String mqttUser   = getVal("mqttUser");
    String mqttPass   = getVal("mqttPass");
    String controlPass = getVal("controlPass");
    String displayName = getVal("displayName");

    if (ssid.length() == 0 || pass.length() == 0) {
        Serial.println("WiFiProvisioning: missing SSID or password");
        return;
    }

    // Safe copies with guaranteed null-termination
    strncpy(runtimeCreds.ssid, ssid.c_str(), sizeof(runtimeCreds.ssid) - 1);
    runtimeCreds.ssid[sizeof(runtimeCreds.ssid) - 1] = '\0';

    strncpy(runtimeCreds.pass, pass.c_str(), sizeof(runtimeCreds.pass) - 1);
    runtimeCreds.pass[sizeof(runtimeCreds.pass) - 1] = '\0';

    strncpy(runtimeCreds.mqttServer, mqttServer.c_str(), sizeof(runtimeCreds.mqttServer) - 1);
    runtimeCreds.mqttServer[sizeof(runtimeCreds.mqttServer) - 1] = '\0';

    strncpy(runtimeCreds.mqttUser, mqttUser.c_str(), sizeof(runtimeCreds.mqttUser) - 1);
    runtimeCreds.mqttUser[sizeof(runtimeCreds.mqttUser) - 1] = '\0';

    strncpy(runtimeCreds.mqttPass, mqttPass.c_str(), sizeof(runtimeCreds.mqttPass) - 1);
    runtimeCreds.mqttPass[sizeof(runtimeCreds.mqttPass) - 1] = '\0';

    strncpy(runtimeCreds.controlPass, controlPass.c_str(), sizeof(runtimeCreds.controlPass) - 1);
    runtimeCreds.controlPass[sizeof(runtimeCreds.controlPass) - 1] = '\0';

    displayName.trim();
    displayName.toUpperCase();
    if (displayName.length() > 20) displayName.remove(20);
    if (displayName.length() >= sizeof(runtimeCreds.displayName)) {
        displayName.remove(sizeof(runtimeCreds.displayName) - 1);
    }
    strncpy(runtimeCreds.displayName, displayName.c_str(),
            sizeof(runtimeCreds.displayName) - 1);
    runtimeCreds.displayName[sizeof(runtimeCreds.displayName) - 1] = '\0';

    runtimeCreds.hasCredentials = true;
    newCreds = true;

    prov_mqtt_server = runtimeCreds.mqttServer;
    prov_mqtt_user   = runtimeCreds.mqttUser;
    prov_mqtt_pass   = runtimeCreds.mqttPass;

    // Persist credentials
    eeprom_saveRuntimeCreds();

    Serial.println("WiFiProvisioning: New credentials received and saved to EEPROM");
}

/* ============================================================
 *  LOOP: AP Portal
 * ============================================================ */

void wifi_prov_loop() {
    if (!apMode) return;

    WiFiClient client = provServer.available();
    if (!client) return;

    client.setTimeout(25);
    String req;
    while (client.available()) {
        req += (char)client.read();
    }

    if (req.length() == 0) return;

    Serial.println("WiFiProvisioning: RAW REQUEST BEGIN");
    Serial.println(req);
    Serial.println("WiFiProvisioning: RAW REQUEST END");

    if (req.startsWith("POST ")) {
        int bodyPos = req.indexOf("\r\n\r\n");
        if (bodyPos > 0) {
            String body = req.substring(bodyPos + 4);
            parseForm(body);
        }

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println();
        client.println("<html><body><h3>Saved. Rebooting...</h3></body></html>");
        client.stop();

        Serial.println("WiFiProvisioning: RESETTING NOW");
        delay(500);
        NVIC_SystemReset();
        return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println();
    client.print(PROV_HTML);
    client.stop();
}
