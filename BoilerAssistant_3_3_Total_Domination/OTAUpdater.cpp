/*
 * ============================================================
 *  Boiler Assistant â€“ OTA Updater (v3.3.6 "Total Domination")
 *  ------------------------------------------------------------
 *  File: OTAUpdater.cpp
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Checks GitHub for a newer firmware version and installs it
 *    through the UNO R4 WiFi's ESP32-S3 bridge (OTAUpdate library).
 *    The boiler is forced into its idle safe state before download.
 *    Requires WiFi bridge firmware 0.5.0 or newer.
 * ============================================================
 */

#include "OTAUpdater.h"
#include "FanDimmer.h"
#include "OTARootCA.h"
#include "Version.h"
#include "SystemState.h"
#include "SystemData.h"
#include "Pinout.h"
#include "RuntimeCredentials.h"
#include "WiFiAPI.h"
#include "MQTTClient.h"

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include <OTAUpdate.h>
#include <WDT.h>

extern SystemData sys;
extern RuntimeCredentials runtimeCreds;

static bool otaActive = false;

bool ota_isActive() {
    return otaActive;
}

void ota_clearActive() {
    otaActive = false;
}

static const char OTA_HOST[]         = "raw.githubusercontent.com";
static const char OTA_VERSION_PATH[] = "/prairiewillow-rgb/Boiler-Assistant/main/firmware-releases/version.txt";
// The modem truncates commands at MAX_BUFF_SIZE (128), so the URL and local path are kept short.
static const char OTA_FILE_URL[]     = "https://raw.githubusercontent.com/prairiewillow-rgb/Boiler-Assistant/main/fw.ota";
static const char OTA_LOCAL_PATH[]   = "/u.bin";

static const unsigned long OTA_HTTP_TIMEOUT_MS     = 10000UL;
static const unsigned long OTA_DOWNLOAD_TIMEOUT_MS = 300000UL;

static char latestVersion[16] = "";
static char checkError[21] = "";
static char installError[21] = "";

static bool parseVersion(const char* text, int& major, int& minor, int& patch) {
    return sscanf(text, "%d.%d.%d", &major, &minor, &patch) == 3;
}

static int compareVersions(const char* a, const char* b) {
    int a1, a2, a3, b1, b2, b3;
    if (!parseVersion(a, a1, a2, a3) || !parseVersion(b, b1, b2, b3)) return 0;
    if (a1 != b1) return a1 < b1 ? -1 : 1;
    if (a2 != b2) return a2 < b2 ? -1 : 1;
    if (a3 != b3) return a3 < b3 ? -1 : 1;
    return 0;
}

static bool wifiBridgeSupportsOta() {
    String fv = WiFi.firmwareVersion();
    return compareVersions(fv.c_str(), "0.5.0") >= 0;
}

const char* ota_currentVersion() {
    return FW_VERSION;
}

const char* ota_latestVersion() {
    return latestVersion;
}

const char* ota_checkError() {
    return checkError;
}

const char* ota_installError() {
    return installError;
}

OtaCheckResult ota_checkForUpdate() {
    latestVersion[0] = '\0';
    checkError[0] = '\0';

    if (WiFi.status() != WL_CONNECTED) return OTA_CHECK_NO_WIFI;

    WiFiSSLClient client;
    WDT.refresh();
    if (!client.connect(OTA_HOST, 443)) {
        strncpy(checkError, "SSL CONNECT FAILED", sizeof(checkError) - 1);
        return OTA_CHECK_FAILED;
    }

    // HTTP/1.0 so the server replies without chunked encoding.
    client.print("GET ");
    client.print(OTA_VERSION_PATH);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(OTA_HOST);
    client.print("\r\nUser-Agent: BoilerAssistant\r\nConnection: close\r\n\r\n");

    char statusLine[32];
    uint8_t statusLen = 0;
    bool statusDone = false;
    uint8_t lineBreaks = 0;
    bool inBody = false;
    uint8_t bodyLen = 0;
    bool gotData = false;

    // connected() can report false before buffered data is read, so only stop once data has arrived.
    unsigned long start = millis();
    while (millis() - start < OTA_HTTP_TIMEOUT_MS) {
        WDT.refresh();
        if (!client.available()) {
            if (gotData && !client.connected()) break;
            delay(5);
            continue;
        }

        int value = client.read();
        if (value < 0) continue;
        char c = (char)value;
        gotData = true;

        if (!inBody) {
            if (!statusDone) {
                if (c == '\n') statusDone = true;
                else if (c != '\r' && statusLen < sizeof(statusLine) - 1) statusLine[statusLen++] = c;
            }
            lineBreaks = (c == '\r' || c == '\n') ? lineBreaks + 1 : 0;
            if (lineBreaks >= 4) inBody = true;
        } else if (bodyLen < sizeof(latestVersion) - 1 &&
                   c != '\r' && c != '\n' && c != ' ') {
            latestVersion[bodyLen++] = c;
        }
    }
    client.stop();

    statusLine[statusLen] = '\0';
    latestVersion[bodyLen] = '\0';

    int major, minor, patch;
    if (!gotData) {
        strncpy(checkError, "NO RESPONSE", sizeof(checkError) - 1);
    } else if (strstr(statusLine, " 200") == nullptr) {
        snprintf(checkError, sizeof(checkError), "HTTP:%s", statusLine + (statusLen > 9 ? 9 : 0));
    } else if (!parseVersion(latestVersion, major, minor, patch)) {
        snprintf(checkError, sizeof(checkError), "BAD VER:%s", latestVersion);
    }

    Serial.print("OTA check status: ");
    Serial.println(statusLine);
    Serial.print("OTA check body: ");
    Serial.println(latestVersion);

    if (checkError[0] != '\0') {
        latestVersion[0] = '\0';
        return OTA_CHECK_FAILED;
    }

    return compareVersions(latestVersion, FW_VERSION) > 0
               ? OTA_CHECK_NEWER
               : OTA_CHECK_UP_TO_DATE;
}

void ota_enterSafeState() {
    otaActive                    = true;
    sys.burnState                = BURN_IDLE;
    sys.boostActive              = false;
    sys.rampTimerActive          = false;
    sys.holdTimerActive          = false;
    sys.emberGuardianTimerActive = false;
    sys.fanFinal                 = 0;
    sys.fanDemand                = 0;

    fan_dimmer_setPercent(0);
    digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
}

// A failed OTA leaves the WiFi bridge busy, so clear it and rejoin the network.
static void ota_recover(OTAUpdate& ota) {
    ota.reset();
    delay(500);
    WDT.refresh();
    if (WiFi.status() != WL_CONNECTED && runtimeCreds.ssid[0] != '\0') {
        WiFi.disconnect();
        delay(500);
        WDT.refresh();
        WiFi.begin(runtimeCreds.ssid, runtimeCreds.pass);
        WDT.refresh();
    }
}

// A mid-download TLS read error (-26 and friends) is transient on the R4 bridge,
// so retry the whole begin/cert/download sequence a few times before giving up.
// Each failed attempt reboots the bridge via ota_recover(); wait for it to come
// back and for WiFi to reconnect before starting the next attempt.
static const uint8_t OTA_MAX_ATTEMPTS = 3;

// ota_recover() reboots the bridge (ota.reset() -> ESP.restart()), so give the
// bridge time to bounce its AT modem and rejoin WiFi before the next attempt.
static bool ota_waitBridgeReady(unsigned long timeoutMs) {
    delay(3000);  // let the rebooted bridge finish restarting
    WDT.refresh();
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        WDT.refresh();
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

OtaInstallResult ota_install(void (*progress)(int percent)) {
    installError[0] = '\0';

    Serial.print("OTA wifi bridge fw: ");
    Serial.println(WiFi.firmwareVersion());

    if (!wifiBridgeSupportsOta()) return OTA_INSTALL_WIFI_FW_OLD;

    ota_enterSafeState();

    // The bridge handles one job at a time; free its sockets before flashing.
    mqtt_stop();
    wifiapi_stop();
    delay(500);
    WDT.refresh();

    OTAUpdate ota;
    int size = -1;

    for (uint8_t attempt = 1; attempt <= OTA_MAX_ATTEMPTS; attempt++) {
        Serial.print("OTA attempt ");
        Serial.print(attempt);
        Serial.print(" of ");
        Serial.println(OTA_MAX_ATTEMPTS);

        WDT.refresh();
        int ret = ota.begin(OTA_LOCAL_PATH);
        Serial.print("OTA begin: ");
        Serial.println(ret);
        if (ret != OTAUpdate::OTA_ERROR_NONE) {
            snprintf(installError, sizeof(installError), "BEGIN ERR %d", ret);
            ota_recover(ota);
            if (attempt == OTA_MAX_ATTEMPTS) return OTA_INSTALL_BEGIN_FAILED;
            ota_waitBridgeReady(15000);
            continue;
        }

        WDT.refresh();
        ret = ota.setCACert(OTA_ROOT_CA);
        Serial.print("OTA setCACert: ");
        Serial.println(ret);
        if (ret != OTAUpdate::OTA_ERROR_NONE) {
            snprintf(installError, sizeof(installError), "CERT ERR %d", ret);
            ota_recover(ota);
            if (attempt == OTA_MAX_ATTEMPTS) return OTA_INSTALL_BEGIN_FAILED;
            ota_waitBridgeReady(15000);
            continue;
        }

        WDT.refresh();
        size = ota.startDownload(OTA_FILE_URL, OTA_LOCAL_PATH);
        Serial.print("OTA startDownload: ");
        Serial.println(size);
        if (size <= 0) {
            snprintf(installError, sizeof(installError), "START ERR %d", size);
            ota_recover(ota);
            if (attempt == OTA_MAX_ATTEMPTS) return OTA_INSTALL_DOWNLOAD_FAILED;
            ota_waitBridgeReady(15000);
            continue;
        }

        unsigned long start = millis();
        int received = 0;
        bool downloadFailed = false;
        while (received < size) {
            WDT.refresh();
            received = ota.downloadProgress();
            if (received < 0 || millis() - start > OTA_DOWNLOAD_TIMEOUT_MS) {
                Serial.print("OTA download error: ");
                Serial.println(received);
                snprintf(installError, sizeof(installError), "DL ERR %d", received);
                downloadFailed = true;
                break;
            }
            if (progress) progress((int)((long)received * 100L / size));
            delay(200);
        }

        if (downloadFailed) {
            ota_recover(ota);
            if (attempt == OTA_MAX_ATTEMPTS) return OTA_INSTALL_DOWNLOAD_FAILED;
            ota_waitBridgeReady(15000);
            continue;
        }

        // Download completed cleanly.
        break;
    }

    if (size <= 0) return OTA_INSTALL_DOWNLOAD_FAILED;

    WDT.refresh();
    int ret = ota.verify();
    Serial.print("OTA verify: ");
    Serial.println(ret);
    if (ret != OTAUpdate::OTA_ERROR_NONE) {
        snprintf(installError, sizeof(installError), "VERIFY ERR %d", ret);
        ota_recover(ota);
        return OTA_INSTALL_VERIFY_FAILED;
    }

    WDT.refresh();
    ret = ota.update(OTA_LOCAL_PATH);
    Serial.print("OTA update: ");
    Serial.println(ret);
    snprintf(installError, sizeof(installError), "FLASH ERR %d", ret);
    ota_recover(ota);
    return OTA_INSTALL_UPDATE_FAILED;
}
