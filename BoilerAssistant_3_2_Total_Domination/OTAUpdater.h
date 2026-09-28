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

// Fetches firmware-releases/version.txt from GitHub and compares it to FW_VERSION.
OtaCheckResult ota_checkForUpdate();

// Puts the boiler in its idle safe state: burn IDLE, fan off, damper closed.
void ota_enterSafeState();

// Downloads and installs the .ota image. Only returns on failure; success reboots the board.
OtaInstallResult ota_install(void (*progress)(int percent));

#endif // OTA_UPDATER_H
