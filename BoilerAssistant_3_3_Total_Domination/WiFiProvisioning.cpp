/*
 * Boiler Assistant - WiFiProvisioning
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "WiFiProvisioning.h"
#include "DebugLog.h"
#include "RuntimeCredentials.h"
#include "SystemData.h"
#include "EEPROMStorage.h"   // <-- required for persistence
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "PushNotify.h"
#include "DataShare.h"

#include <Arduino.h>
#include <malloc.h>
#include <new>
#include <unistd.h>
#include <WiFiS3.h>
#include <WiFiServer.h>
#include <OTAUpdate.h>
#include "OTAUpdater.h"
#include "Watchdog.h"

extern SystemData sys;
extern "C" char __HeapLimit;  // top of the heap (linker)

/* ============================================================
 *  MQTT GLOBALS (used by MQTTClient.cpp)
 * ============================================================ */

const char* prov_mqtt_server = nullptr;
const char* prov_mqtt_user   = nullptr;
const char* prov_mqtt_pass   = nullptr;

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

// Every WiFiClient owns a 1,024-byte heap receive buffer, and a WiFiServer holds one. The setup
// hotspot needs its server and client only while it is open, so they are created when it starts
// and released when it closes.
struct PortalSockets {
    WiFiServer server{80};
    WiFiClient client;
};
static PortalSockets* portal = nullptr;
static bool apMode   = false;
static bool newCreds = false;
static bool stationAddressReady = false;
static unsigned long lastNetworkCheckMs = 0;
static unsigned long lastJoinAttemptMs = 0;
static unsigned long joinRetryMs = 30000UL;
static unsigned long offlineSinceMs = 0;
static bool offlineTiming = false;
static unsigned long linkStableSinceMs = 0;
static bool linkDetected = false;
static bool servicesOnline = false;
static IPAddress serviceIP;
static int32_t cachedRSSI = 0;
#if BOILER_WIFI_RSSI_ENABLED
static unsigned long lastRSSIMs = 0;
#endif
static unsigned long cachedUTC = 0;
static unsigned long utcAnchorMs = 0;
static unsigned long lastTimeAttemptMs = 0;
static bool timeAttempted = false;
static const unsigned long MODEM_RUNTIME_TIMEOUT_MS = WIFI_STEP_LIMIT_MS;
enum BridgeRecovery { BRIDGE_NORMAL, BRIDGE_QUIET, BRIDGE_BOOT_WAIT,
                      BRIDGE_CLOSE_HTTP, BRIDGE_CLOSE_MQTT, BRIDGE_CLOSE_PUSH };
static BridgeRecovery bridgeRecovery = BRIDGE_NORMAL;
static unsigned long bridgeRecoveryMs = 0;
static unsigned long bridgeQuietIntervalMs = 15000UL;
static bool bridgeResetIssued = false;
static uint16_t joinCount = 0, lostCount = 0, chipResetCount = 0;
static bool setupAfterRecovery = false;

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

bool wifi_prov_prepareClientIo(unsigned long limitMs) {
    if (bridgeRecovery == BRIDGE_QUIET || bridgeRecovery == BRIDGE_BOOT_WAIT) return false;
    if (bridgeResetIssued && bridgeRecovery != BRIDGE_NORMAL) return false;
    if (ota_isActive()) {
        // OTA explicitly closes runtime sockets after entering its safe state.
        modem.timeout(MODEM_TIMEOUT);
        return true;
    }
    modem.timeout(limitMs);
    return true;
}

static void startBridgeRecovery(const char* source) {
    Serial.print("WiFi: recovery: ");
    Serial.println(source);
    sys.wifiOK = false;
    bridgeRecovery = BRIDGE_QUIET;
    bridgeRecoveryMs = millis();
    bridgeQuietIntervalMs = 15000UL;
    bridgeResetIssued = false;
    // Do not issue socket-close commands into a possibly unfinished AT reply.
    // Reset/reboot readiness precedes local-only cleanup of old handles.
}

void wifi_prov_requestRecovery(const char* source) {
    if (apMode || ((!runtimeCreds.hasCredentials || runtimeCreds.ssid[0] == '\0') && !setupAfterRecovery) ||
        ota_isActive() || bridgeRecovery != BRIDGE_NORMAL) return;
    startBridgeRecovery(source);
}

void wifi_prov_finishIo(unsigned long startedMs, const char* source, unsigned long limitMs) {
    if (ota_isActive() || bridgeRecovery == BRIDGE_QUIET || bridgeRecovery == BRIDGE_BOOT_WAIT ||
        millis() - startedMs < limitMs) return;
    startBridgeRecovery(source);
}

bool wifi_prov_setHostname() {
    if (!wifi_prov_prepareClientIo()) return false;
    // Core 1.6.0 setHostname() omits ':' from the expected response prompt.
    std::string response;
    unsigned long startedMs = millis();
    bool success = modem.write(PROMPT(_HOSTNAME), response, "%s%s\r\n",
                               CMD_WRITE(_HOSTNAME), BOILER_WIFI_HOSTNAME);
    wifi_prov_finishIo(startedMs, "WiFi hostname");
    if (!success) {
        Serial.println("WiFi: hostname command failed");
        wifi_prov_requestRecovery("WiFi hostname failed");
        return false;
    }
    return wifi_prov_prepareClientIo();
}

static bool serviceBridgeRecovery() {
    if (bridgeRecovery == BRIDGE_NORMAL) return false;
    unsigned long now = millis();
    if (bridgeRecovery == BRIDGE_QUIET) {
        if (now - bridgeRecoveryMs < bridgeQuietIntervalMs) return true;
        modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
        OTAUpdate bridge;
        ++chipResetCount;
        bridgeResetIssued = true;
        if (bridge.reset() != 0) {
            Serial.println("WiFi: reset no ACK; checking after reboot wait");
        } else {
            Serial.println("WiFi: reset ACK; checking after reboot wait");
        }
        bridgeRecovery = BRIDGE_BOOT_WAIT;
        bridgeRecoveryMs = millis();
        return true;
    }
    if (bridgeRecovery == BRIDGE_BOOT_WAIT) {
        if (now - bridgeRecoveryMs < 5000UL) return true;
        modem.timeout(MODEM_RUNTIME_TIMEOUT_MS);
        unsigned long probeMs = millis();
        const char* firmware = WiFi.firmwareVersion();
        if (firmware[0] == '\0' || strcmp(firmware, "99.99.99") == 0 ||
            millis() - probeMs >= MODEM_RUNTIME_TIMEOUT_MS) {
            Serial.println("WiFi: bridge not ready; retry in 60s");
            bridgeRecovery = BRIDGE_QUIET;
            bridgeQuietIntervalMs = 60000UL;
            bridgeRecoveryMs = millis();
            return true;
        }
        Serial.println("WiFi: bridge responding; discard old sockets");
        stationAddressReady = false;
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
        if (bridgeResetIssued) offlineTiming = false;
        serviceIP = IPAddress();
        cachedRSSI = 0;
        joinRetryMs = 30000UL;
        lastJoinAttemptMs = bridgeResetIssued ? millis() - joinRetryMs : millis();
        bridgeResetIssued = false;
        bridgeRecovery = BRIDGE_NORMAL;
        DBG_PRINTLN("WiFi: sockets cleared");
    }
    if (millis() - cleanupStartMs >= MODEM_RUNTIME_TIMEOUT_MS) {
        bridgeRecovery = BRIDGE_QUIET;
        bridgeRecoveryMs = millis();
        bridgeQuietIntervalMs = 15000UL;
        bridgeResetIssued = false;
        DBG_PRINTLN("WiFi: socket stall");
    }
    return true;
}

static void beginStationAttempt() {
    // Renesas WiFiS3 checks _timeout only after sending BEGINSTA. Zero skips
    // its 10-second status spin; association/DHCP are checked on later passes.
    if (!wifi_prov_prepareIo()) return;
    WiFi.setTimeout(0);
    if (!wifi_prov_setHostname()) return;
    if (!wifi_prov_applyNetworkConfig()) return;
    stationAddressReady = true;
    if (!wifi_prov_prepareIo()) return;
    unsigned long operationMs = millis();
    WiFi.begin(runtimeCreds.ssid, runtimeCreds.pass);
    wifi_prov_finishIo(operationMs, "WiFi join");
    lastJoinAttemptMs = millis();
    ++joinCount;
    Serial.println("WiFi: joining");
}

bool wifi_prov_applyNetworkConfig() {
    if (!wifi_prov_prepareClientIo()) return false;
    const char* error = network_validate(networkSettings);
    if (error) {
        Serial.println(error);
        return false;
    }
    IPAddress ip, gateway, subnet, dns;
    if (networkSettings.fixed) {
        for (uint8_t i = 0; i < 4; ++i) {
            ip[i] = networkSettings.ip[i];
            gateway[i] = networkSettings.gateway[i];
            subnet[i] = networkSettings.subnet[i];
            dns[i] = networkSettings.dns[i];
        }
    }
    // WiFiS3 config() discards the ACK and also configures the setup AP.
    // SETIP uses local,gateway,mask,DNS; all zeros explicitly restart DHCP.
    std::string response;
    unsigned long startedMs = millis();
    bool success = modem.write(PROMPT(_SETIP), response, "%s%s,%s,%s,%s\r\n",
        CMD_WRITE(_SETIP), ip.toString().c_str(), gateway.toString().c_str(),
        subnet.toString().c_str(), dns.toString().c_str());
    wifi_prov_finishIo(startedMs, "WiFi addressing");
    if (!success) {
        Serial.println("WiFi: address command failed; not joining");
        wifi_prov_requestRecovery("WiFi addressing failed");
        return false;
    }
    return wifi_prov_prepareClientIo();
}

/* Simple HTML portal */
static const char* PROV_HTML =
"<!DOCTYPE html><html><body>"
"<h2>Boiler Assistant WiFi Setup</h2>"
"<form method='POST'>"
"Your name:<br><input name='displayName' maxlength='31'><br><br>"
"WiFi SSID:<br><input name='ssid' maxlength='31' required><br>"
"WiFi Password:<br><input name='pass' maxlength='63' type='password' required><br><br>"
"Address mode:<br><select name='addressMode'><option value='dhcp'>Automatic (DHCP)</option>"
"<option value='fixed'>Fixed IP</option></select><br>"
"<p>Fixed IP only: use an unused address outside your router's DHCP pool. "
"Use your network's gateway, subnet and DNS. Wrong settings can make the unit unreachable. "
"To recover: Networking &gt; Factory Reset on the keypad, then run setup again.</p>"
"IP address:<br><input name='ip' maxlength='15' placeholder='192.168.1.50'><br>"
"Gateway:<br><input name='gateway' maxlength='15' placeholder='192.168.1.1'><br>"
"Subnet:<br><input name='subnet' maxlength='15' placeholder='255.255.255.0'><br>"
"DNS:<br><input name='dns' maxlength='15' placeholder='192.168.1.1'><br><br>"
"Other blank fields keep existing settings.<br>"
"MQTT Server:<br><input name='mqttServer' maxlength='63'><br>"
"MQTT User:<br><input name='mqttUser' maxlength='31'><br>"
"MQTT Password:<br><input name='mqttPass' maxlength='63' type='password'><br><br>"
"Control/API Password:<br><input name='controlPass' maxlength='31' type='password'><br><br>"
"<input type='submit' value='Save'>"
"</form></body></html>";

/* ============================================================
 *  FACTORY RESET
 * ============================================================ */

void wifi_prov_factoryReset() {
    DBG_PRINTLN("WiFi: FACTORY RESET");

    RuntimeCredentials cleared;
    NetworkSettings automatic;
    if (!eeprom_saveProvisioning(cleared, automatic)) {
        Serial.println("WiFi: network reset save failed; not rebooting");
        return;
    }
    runtimeCreds = cleared;
    networkSettings = automatic;
    // A factory reset usually means a new owner, who must opt in again.
    datashare_clearConsent();

    sys.wifiOK = false;

    DBG_PRINTLN("WiFi: factory reset; rebooting");
    delay(1000);
    NVIC_SystemReset();
}

/* ============================================================
 *  AP MODE
 * ============================================================ */

static void startAP() {
    DBG_PRINTLN("WiFi: starting AP");

    WiFi.end();
    WiFi.disconnect();
    delay(200);

    WiFi.config(
        IPAddress(192,168,4,1),
        IPAddress(0,0,0,0),
        IPAddress(192,168,4,1),
        IPAddress(255,255,255,0)
    );

    int result = WiFi.beginAP("BoilerAssistant-Setup");

    DBG_PRINT("WiFi: AP result: ");
    DBG_PRINTLN(result);

    apMode = true;
    sys.wifiOK = false;

    if (!portal) portal = new (std::nothrow) PortalSockets;
    if (portal) portal->server.begin();
    else Serial.println("WiFi setup: not enough RAM to open the setup page");
}

/* ============================================================
 *  INIT: saved STA retries, AP if unconfigured/corrupt addressing
 * ============================================================ */

void wifi_prov_init() {
    DBG_PRINTLN("WiFi: init");

    sys.wifiOK = false;
    if (!eeprom_loadNetworkSettings()) {
        Serial.println("WiFi: saved addressing invalid; opening setup, not DHCP");
        startAP();
        return;
    }

    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0) {
        DBG_PRINT("WiFi: SSID: ");
        DBG_PRINTLN(runtimeCreds.ssid);

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

    DBG_PRINTLN("WiFi: no creds; AP");
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

// Lowest free-heap reading seen. It is sampled while requests are served and in each report,
// so a brief dip between samples can be missed.
static uint32_t lowestFreeHeap = UINT32_MAX;

static uint32_t freeHeapNow() {
    struct mallinfo heap = mallinfo();
    return static_cast<uint32_t>(heap.fordblks) +
           static_cast<uint32_t>(&__HeapLimit - static_cast<char*>(sbrk(0)));
}

void wifi_prov_sampleHeap() {
    const uint32_t freeNow = freeHeapNow();
    if (freeNow < lowestFreeHeap) lowestFreeHeap = freeNow;
}

// Largest block malloc can supply right now (up to 8 KB, to the nearest 16 bytes).
// Each trial block is freed straight away. Used only for the on-demand diagnostic report.
static uint16_t largestFreeBlock() {
    uint16_t low = 0, high = 8192;
    while (high - low > 16) {
        const uint16_t mid = low + (high - low) / 2;
        void* block = malloc(mid);
        if (block) {
            free(block);
            low = mid;
        } else {
            high = mid;
        }
    }
    return low;
}

// Uses only cached state: no bridge commands. The largest-block probe runs only when asked.
const char* wifi_prov_netText(bool probeLargestBlock) {
    static char text[256];
    // Probe first: its temporary blocks move memory between the reusable and unallocated figures.
    char largest[8] = "na";
    if (probeLargestBlock) snprintf(largest, sizeof(largest), "%u", static_cast<unsigned>(largestFreeBlock()));
    struct mallinfo heap = mallinfo();
    wifi_prov_sampleHeap();
    uint32_t slowMs;
    const char* slow = watchdog_slowStage(slowMs);
    int n = snprintf(text, sizeof(text), "up=%lu wifi=%d svc=%d link=%d br=%d rssi=%ld",
                     millis() / 1000UL, sys.wifiOK ? 1 : 0, servicesOnline ? 1 : 0,
                     linkDetected ? 1 : 0, static_cast<int>(bridgeRecovery),
                     static_cast<long>(cachedRSSI));
    if (n > 0 && n < static_cast<int>(sizeof(text)))
        n += wifiapi_statusText(text + n, sizeof(text) - n);
    if (n > 0 && n < static_cast<int>(sizeof(text)))
        snprintf(text + n, sizeof(text) - n,
                 " heap=%u+%ld blk=%s low=%lu stk=%u wd=%u slow=%s/%lums joins=%u lost=%u chip=%u",
                 static_cast<unsigned>(heap.fordblks),
                 static_cast<long>(&__HeapLimit - static_cast<char*>(sbrk(0))),
                 largest, static_cast<unsigned long>(lowestFreeHeap),
                 watchdog_stackPeak(), watchdog_resetCount(), slow,
                 static_cast<unsigned long>(slowMs), joinCount, lostCount, chipResetCount);
    return text;
}

// One serial line every 30 s so a field log shows which layer stops when the
// dashboard drops.
static void printNetStatus() {
    static unsigned long lastStatusMs = 0;
    unsigned long now = millis();
    if (now - lastStatusMs < 30000UL) return;
    lastStatusMs = now;
    Serial.print("NET: ");
    Serial.println(wifi_prov_netText());
}

void wifi_prov_networkLoop() {
    if (apMode || ((!runtimeCreds.hasCredentials || runtimeCreds.ssid[0] == '\0') && !setupAfterRecovery)) return;
    printNetStatus();
    if (cachedUTC != 0 && millis() - utcAnchorMs >= 86400000UL) {
        cachedUTC = 0;
        DBG_PRINTLN("WiFi: clock expired");
    }
    if (ota_isActive() || serviceBridgeRecovery()) return;
    if (setupAfterRecovery) {
        setupAfterRecovery = false;
        startAP();
        return;
    }
    unsigned long now = millis();
    if (now - lastNetworkCheckMs < 1000UL) return;
    lastNetworkCheckMs = now;
    // A rebooted bridge may auto-associate using DHCP before we reconfigure it.
    if (!stationAddressReady) {
        if (now - lastJoinAttemptMs >= joinRetryMs) {
            beginStationAttempt();
            joinRetryMs = min(joinRetryMs * 2UL, 120000UL);
        }
        return;
    }

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
        if (connected && networkSettings.fixed) {
            bool matches = true;
            for (uint8_t i = 0; i < 4; ++i)
                if (ip[i] != networkSettings.ip[i]) matches = false;
            if (!matches) {
                startBridgeRecovery("fixed IP not applied");
                return;
            }
        }
    }

    if (servicesOnline && (!connected || ip != serviceIP)) {
        sys.wifiOK = false;
        servicesOnline = false;
        bridgeResetIssued = false;
        bridgeRecovery = BRIDGE_CLOSE_HTTP;
        linkDetected = false;
        lastJoinAttemptMs = millis();
        joinRetryMs = 30000UL;
        offlineTiming = false;
        ++lostCount;
        Serial.println("WiFi: link lost");
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
            if (!sys.wifiOK || bridgeRecovery != BRIDGE_NORMAL) return;
            offlineTiming = false;
            Serial.print("WiFi: online ");
            Serial.println(ip);
            return;
        }
        if (servicesOnline) {
            const unsigned long timeRetryMs = cachedUTC ? 60000UL : 10000UL;
            if (!timeAttempted || now - lastTimeAttemptMs >= timeRetryMs) {
                timeAttempted = true;
                lastTimeAttemptMs = now;
                operationMs = millis();
                unsigned long utc = WiFi.getTime();
                wifi_prov_finishIo(operationMs, "WiFi time");
                if (bridgeRecovery != BRIDGE_NORMAL) return;
                if (utc != 0) {
                    cachedUTC = utc;
                    utcAnchorMs = millis();
                } else {
                    DBG_PRINTLN("WiFi: no NTP; cached");
                }
            }
#if BOILER_WIFI_RSSI_ENABLED
            else if (now - lastRSSIMs >= 15000UL) {
                lastRSSIMs = now;
                operationMs = millis();
                int32_t rssi = WiFi.RSSI();
                wifi_prov_finishIo(operationMs, "WiFi RSSI");
                if (bridgeRecovery == BRIDGE_NORMAL) cachedRSSI = rssi;
            }
#endif
        }
        return;
    }

    if (!offlineTiming) {
        offlineSinceMs = now;
        offlineTiming = true;
    }
    // Escalate failed joins even when AT commands still answer promptly.
    // Brief associations do not clear this deadline until services are online.
    if (now - offlineSinceMs >= 300000UL) {
        startBridgeRecovery("offline 5 min");
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

static RuntimeCredentials savedNetworkCredentials;
static NetworkSettings savedNetworkSettings;
static bool savedNetworkPending = false;

bool wifi_prov_networkChangePending() { return savedNetworkPending; }

void wifi_prov_applySavedNetwork() {
    if (!savedNetworkPending || ota_isActive()) return;
    runtimeCreds = savedNetworkCredentials;
    networkSettings = savedNetworkSettings;
    savedNetworkPending = false;
    setupAfterRecovery = !runtimeCreds.hasCredentials;
    stationAddressReady = false;
    wifi_prov_requestRecovery("network settings changed");
}

static int formHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool formValue(const String& body, const char* key, String& value) {
    bool found = false;
    unsigned start = 0;
    while (start < body.length()) {
        int end = body.indexOf('&', start);
        if (end < 0) end = body.length();
        int equals = body.indexOf('=', start);
        if (equals < 0 || equals >= end) return false;
        if (body.substring(start, equals) == key) {
            if (found) return false;
            found = true;
            for (int i = equals + 1; i < end; ++i) {
                char c = body[i];
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (i + 2 >= end) return false;
                    int high = formHex(body[++i]);
                    int low = formHex(body[++i]);
                    if (high < 0 || low < 0) return false;
                    c = (high << 4) | low;
                }
                if (c == '\0' || c == '\r' || c == '\n') return false;
                if (!value.concat(c)) return false;
            }
        }
        start = end + 1;
    }
    return true;
}

const char* wifi_prov_saveNetwork(const String& body, bool reset) {
    if (savedNetworkPending || bridgeRecovery != BRIDGE_NORMAL || ota_isActive())
        return "Network change or recovery already in progress.";
    savedNetworkCredentials = runtimeCreds;
    savedNetworkSettings = NetworkSettings();
    if (reset) {
        memset(savedNetworkCredentials.ssid, 0, sizeof(savedNetworkCredentials.ssid));
        memset(savedNetworkCredentials.pass, 0, sizeof(savedNetworkCredentials.pass));
        savedNetworkCredentials.hasCredentials = false;
    } else {
        const char* keys[] = {"ssid", "pass", "addressMode", "ip", "gateway", "subnet", "dns"};
        String values[7];
        for (uint8_t i = 0; i < 7; ++i)
            if (!formValue(body, keys[i], values[i])) return "Invalid or duplicate network fields.";
        if (values[0].length() == 0 || values[0].length() >= sizeof(runtimeCreds.ssid) ||
            values[1].length() >= sizeof(runtimeCreds.pass))
            return "SSID must be 1-31 bytes and Wi-Fi password at most 63 bytes.";
        if (values[1].length() == 0 && values[0] != runtimeCreds.ssid)
            return "Enter the Wi-Fi password when changing networks.";
        strcpy(savedNetworkCredentials.ssid, values[0].c_str());
        if (values[1].length()) strcpy(savedNetworkCredentials.pass, values[1].c_str());
        savedNetworkCredentials.hasCredentials = true;
        if (values[2] == "fixed") {
            savedNetworkSettings.fixed = 1;
            uint8_t* targets[] = {savedNetworkSettings.ip, savedNetworkSettings.gateway,
                                  savedNetworkSettings.subnet, savedNetworkSettings.dns};
            for (uint8_t i = 0; i < 4; ++i)
                if (!network_parseIPv4(values[i + 3].c_str(), targets[i]))
                    return "Enter all four Fixed IP fields as dotted IPv4 addresses.";
        } else if (values[2] != "dhcp") return "Select Automatic (DHCP) or Fixed IP.";
        const char* error = network_validate(savedNetworkSettings);
        if (error) return error;
    }
    if (!eeprom_saveProvisioning(savedNetworkCredentials, savedNetworkSettings))
        return "Network save failed; current connection unchanged. Retry before disconnecting.";
    savedNetworkPending = true;
    return nullptr;
}

static const char* parseForm(const String& body) {
    const char* keys[] = {"ssid", "pass", "mqttServer", "mqttUser", "mqttPass",
                         "controlPass", "displayName", "addressMode",
                         "ip", "gateway", "subnet", "dns"};
    String values[12];
    for (uint8_t i = 0; i < 12; ++i)
        if (!formValue(body, keys[i], values[i])) return "Invalid or duplicate form fields.";
    RuntimeCredentials credentials = runtimeCreds;
    char* targets[] = {credentials.ssid, credentials.pass, credentials.mqttServer,
                       credentials.mqttUser, credentials.mqttPass, credentials.controlPass,
                       credentials.displayName};
    const unsigned limits[] = {32, 64, 64, 32, 64, 32, 32};
    if (values[0].length() == 0 || values[1].length() == 0)
        return "SSID and WiFi password are required.";
    for (uint8_t i = 0; i < 7; ++i) {
        if (values[i].length() >= limits[i]) return "A credential field is too long.";
    }
    values[6].trim();
    values[6].toUpperCase();
    if (values[6].length() > 20) values[6].remove(20);
    for (uint8_t i = 0; i < 7; ++i)
        if (i < 2 || values[i].length()) strcpy(targets[i], values[i].c_str());
    NetworkSettings settings;
    if (values[7] == "fixed") {
        settings.fixed = 1;
        if (!network_parseIPv4(values[8].c_str(), settings.ip) ||
            !network_parseIPv4(values[9].c_str(), settings.gateway) ||
            !network_parseIPv4(values[10].c_str(), settings.subnet) ||
            !network_parseIPv4(values[11].c_str(), settings.dns))
            return "Enter all four Fixed IP fields as dotted IPv4 addresses.";
    } else if (values[7] != "dhcp") return "Select Automatic (DHCP) or Fixed IP.";
    const char* error = network_validate(settings);
    if (error) return error;
    credentials.hasCredentials = true;
    if (!eeprom_saveProvisioning(credentials, settings))
        return "EEPROM save failed. Stay in setup and try again; settings were not committed.";
    runtimeCreds = credentials;
    networkSettings = settings;
    newCreds = true;
    prov_mqtt_server = runtimeCreds.mqttServer;
    prov_mqtt_user = runtimeCreds.mqttUser;
    prov_mqtt_pass = runtimeCreds.mqttPass;
    return nullptr;
}

/* ============================================================
 *  LOOP: AP Portal
 * ============================================================ */

static String portalRequest;
static bool portalReading = false;
static unsigned long portalStartedMs = 0;

static void portalReply(const char* status, const char* message) {
    portal->client.print("HTTP/1.1 ");
    portal->client.println(status);
    portal->client.println("Content-Type: text/html; charset=utf-8");
    portal->client.println("Cache-Control: no-store");
    portal->client.println("Connection: close");
    portal->client.println();
    portal->client.print(message);
    portal->client.stop();
    portalRequest = "";
    portalReading = false;
}

static const char* portalLength(const String& headers, int& length) {
    bool found = false;
    unsigned start = headers.indexOf("\r\n") + 2;
    while (start < headers.length()) {
        int end = headers.indexOf("\r\n", start);
        if (end < 0) end = headers.length();
        String line = headers.substring(start, end);
        line.toLowerCase();
        if (line.startsWith("transfer-encoding:")) return "Transfer encoding is not supported.";
        if (line.startsWith("content-length:")) {
            if (found) return "Duplicate Content-Length.";
            found = true;
            String value = line.substring(15);
            value.trim();
            if (value.length() == 0 || value.length() > 4) return "Invalid Content-Length.";
            length = 0;
            for (unsigned i = 0; i < value.length(); ++i) {
                if (value[i] < '0' || value[i] > '9') return "Invalid Content-Length.";
                length = length * 10 + value[i] - '0';
            }
        }
        start = end + 2;
    }
    if (!found || length <= 0 || length > 1536) return "POST body must be 1-1536 bytes.";
    return nullptr;
}

void wifi_prov_loop() {
    if (!apMode || !portal) return;
    if (!portalReading) {
        portal->client = portal->server.available();
        if (!portal->client) return;
        portalReading = true;
        portalStartedMs = millis();
        portalRequest = "";
    }
    if (millis() - portalStartedMs >= 5000UL) {
        portalReply("408 Request Timeout", "Incomplete request. Return to setup and try again.");
        return;
    }
    int available = portal->client.available();
    if (available > 0) {
        uint8_t bytes[256];
        int count = portal->client.read(bytes, min(available, 256));
        if (count > 0 && (portalRequest.length() + count > 3072 ||
            !portalRequest.concat((const char*)bytes, count))) {
            portalReply("413 Content Too Large", "Setup request too large.");
            return;
        }
    }
    int bodyPos = portalRequest.indexOf("\r\n\r\n");
    if (bodyPos < 0) {
        if (portalRequest.length() > 1536)
            portalReply("431 Request Header Fields Too Large", "Setup headers too large.");
        return;
    }
    if (bodyPos > 1536) {
        portalReply("431 Request Header Fields Too Large", "Setup headers too large.");
        return;
    }
    if (portalRequest.startsWith("GET / ")) {
        portalReply("200 OK", PROV_HTML);
        return;
    }
    if (!portalRequest.startsWith("POST / ")) {
        portalReply("405 Method Not Allowed", "Use the setup form.");
        return;
    }
    int length = 0;
    const char* error = portalLength(portalRequest.substring(0, bodyPos), length);
    if (error) {
        portalReply("400 Bad Request", error);
        return;
    }
    int received = portalRequest.length() - bodyPos - 4;
    if (received < length) return;
    if (received != length) {
        portalReply("400 Bad Request", "Incorrect request body length.");
        return;
    }
    error = parseForm(portalRequest.substring(bodyPos + 4));
    if (error) {
        Serial.print("WiFi setup: ");
        Serial.println(error);
        portalReply("400 Bad Request", error);
        return;
    }
    portalReply("200 OK", "<h3>Saved. Reconnecting Wi-Fi...</h3>");
    delete portal;
    portal = nullptr;
    apMode = false;
    stationAddressReady = false;
    wifi_prov_requestRecovery("WiFi setup saved");
}
