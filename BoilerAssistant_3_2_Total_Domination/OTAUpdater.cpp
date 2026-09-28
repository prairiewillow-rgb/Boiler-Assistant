/*
 * ============================================================
 *  Boiler Assistant – OTA Updater (v3.2 "Total Domination")
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
#include "Version.h"
#include "SystemState.h"
#include "SystemData.h"
#include "Pinout.h"

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include <OTAUpdate.h>
#include <WDT.h>

extern SystemData sys;

static const char OTA_HOST[]         = "raw.githubusercontent.com";
static const char OTA_VERSION_PATH[] = "/prairiewillow-rgb/Boiler-Assistant/main/firmware-releases/version.txt";
static const char OTA_FILE_URL[]     = "https://raw.githubusercontent.com/prairiewillow-rgb/Boiler-Assistant/main/firmware-releases/BoilerAssistant.ota";
static const char OTA_LOCAL_PATH[]   = "/update.bin";

static const unsigned long OTA_HTTP_TIMEOUT_MS     = 10000UL;
static const unsigned long OTA_DOWNLOAD_TIMEOUT_MS = 300000UL;

static char latestVersion[16] = "";

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

OtaCheckResult ota_checkForUpdate() {
    latestVersion[0] = '\0';

    if (WiFi.status() != WL_CONNECTED) return OTA_CHECK_NO_WIFI;

    WiFiSSLClient client;
    WDT.refresh();
    if (!client.connect(OTA_HOST, 443)) return OTA_CHECK_FAILED;

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

    unsigned long start = millis();
    while ((client.connected() || client.available()) &&
           millis() - start < OTA_HTTP_TIMEOUT_MS) {
        WDT.refresh();
        if (!client.available()) {
            delay(5);
            continue;
        }

        char c = client.read();
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
    if (strstr(statusLine, " 200") == nullptr ||
        !parseVersion(latestVersion, major, minor, patch)) {
        latestVersion[0] = '\0';
        return OTA_CHECK_FAILED;
    }

    return compareVersions(latestVersion, FW_VERSION) > 0
               ? OTA_CHECK_NEWER
               : OTA_CHECK_UP_TO_DATE;
}

void ota_enterSafeState() {
    sys.burnState                = BURN_IDLE;
    sys.boostActive              = false;
    sys.rampTimerActive          = false;
    sys.holdTimerActive          = false;
    sys.emberGuardianTimerActive = false;
    sys.fanFinal                 = 0;

    analogWrite(PIN_FAN_PWM, 0);
    digitalWrite(PIN_DAMPER, HIGH);   // CLOSED
}

OtaInstallResult ota_install(void (*progress)(int percent)) {
    if (!wifiBridgeSupportsOta()) return OTA_INSTALL_WIFI_FW_OLD;

    ota_enterSafeState();

    OTAUpdate ota;
    WDT.refresh();
    if (ota.begin(OTA_LOCAL_PATH) != OTAUpdate::OTA_ERROR_NONE) {
        return OTA_INSTALL_BEGIN_FAILED;
    }

    WDT.refresh();
    int size = ota.startDownload(OTA_FILE_URL, OTA_LOCAL_PATH);
    if (size <= 0) return OTA_INSTALL_DOWNLOAD_FAILED;

    unsigned long start = millis();
    int received = 0;
    while (received < size) {
        WDT.refresh();
        received = ota.downloadProgress();
        if (received < 0 || millis() - start > OTA_DOWNLOAD_TIMEOUT_MS) {
            return OTA_INSTALL_DOWNLOAD_FAILED;
        }
        if (progress) progress((int)((long)received * 100L / size));
        delay(200);
    }

    WDT.refresh();
    if (ota.verify() != OTAUpdate::OTA_ERROR_NONE) return OTA_INSTALL_VERIFY_FAILED;

    WDT.refresh();
    ota.update(OTA_LOCAL_PATH);
    return OTA_INSTALL_UPDATE_FAILED;
}
