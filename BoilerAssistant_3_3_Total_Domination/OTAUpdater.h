/*
 * ============================================================
 *  Boiler Assistant - OTA Updater API (v3.3.9 "Total Domination")
 *  File: OTAUpdater.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Version checks, error results and firmware installation
 *    with progress reporting. Installation enters the idle safe
 *    state and intentionally blocks using OTA library timeouts.
 *    Requires bridge firmware 0.5.0 or newer. Success reboots;
 *    failures return to the operator interface.
 *
 *  Version: Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

enum OtaCheckResult {
    OTA_CHECK_NEWER,
    OTA_CHECK_UP_TO_DATE,
    OTA_CHECK_NO_WIFI,
    OTA_CHECK_FAILED
};

enum OtaInstallResult {
    OTA_INSTALL_WIFI_FW_OLD,
    OTA_INSTALL_BEGIN_FAILED,
    OTA_INSTALL_DOWNLOAD_FAILED,
    OTA_INSTALL_VERIFY_FAILED,
    OTA_INSTALL_UPDATE_FAILED
};

const char* ota_currentVersion();
const char* ota_latestVersion();

// Short reason for the last OTA_CHECK_FAILED (fits one LCD line).
const char* ota_checkError();

// Short reason for the last failed install, including the raw error code.
const char* ota_installError();

// Fetches firmware-releases/version.txt from GitHub and compares it to FW_VERSION.
OtaCheckResult ota_checkForUpdate();

// Puts the boiler in its idle safe state: burn IDLE, fan off, damper closed.
void ota_enterSafeState();

// Downloads and installs the .ota image. Only returns on failure; success reboots the board.
OtaInstallResult ota_install(void (*progress)(int percent));

// True from the moment an install starts until the operator acknowledges a failure.
bool ota_isActive();
void ota_clearActive();

#endif // OTA_UPDATER_H
