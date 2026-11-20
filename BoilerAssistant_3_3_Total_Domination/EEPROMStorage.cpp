/*
 * Boiler Assistant - EEPROMStorage
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "EEPROMStorage.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include <EEPROM.h>

extern SystemData sys;
extern RuntimeCredentials runtimeCreds;

static const int EEPROM_CONFIG_END = 87;
static const int EEPROM_MAGIC_ADDR = 88;
static const int EEPROM_VERSION_ADDR = 89;
static const int EEPROM_CRC_ADDR = 90;
// Outside the CRC range so adding it doesn't invalidate existing configs.
static const int EEPROM_ENV_UNITS_ADDR = 92;
static const int EEPROM_SELF_CLEAN_ADDR = 93;
// Probe names and the tank probe are stored by probe serial number (ROM),
// so they stay with the physical probe whatever order the bus scan finds them.
// 3.3.12 kept names by slot at 400-527, which overlapped the saved Wi-Fi
// details (100-420, display name at 389) and the push topic (460-493).
// That area is no longer used. Table: magic, version, tank ROM, then slots.
// 3.4: 1024-1535 reserved (archived Heat Tracking), 1536 WaterCalibration, 2048 DataShare.
static const int EEPROM_PROBE_TABLE_ADDR = 2560;
static_assert(EEPROM_PROBE_TABLE_ADDR + 10 + 16 * (8 + PROBE_NAME_LENGTH) <= 8192,
              "probe table must fit in the 8 KB EEPROM");
static const int EEPROM_TANK_ROM_ADDR = EEPROM_PROBE_TABLE_ADDR + 2;
static const int EEPROM_PROBE_SLOTS_ADDR = EEPROM_TANK_ROM_ADDR + 8;
static const uint8_t EEPROM_PROBE_SLOTS = 16;
static const int EEPROM_PROBE_SLOT_SIZE = 8 + PROBE_NAME_LENGTH;
static const uint8_t EEPROM_PROBE_TABLE_MAGIC = 0xB7;
static const uint8_t EEPROM_PROBE_TABLE_VERSION = 1;
// Push config lives outside the CRC region (460+) so it never invalidates
// an existing saved configuration.
static const int EEPROM_PUSH_ENABLED_ADDR = 460;
static const int EEPROM_PUSH_TOPIC_ADDR   = 461;  // 33 bytes (32 chars + NUL)
static const uint8_t EEPROM_MAGIC = 0xBA;
static const uint8_t EEPROM_VERSION = 3;
// Independent block preserves the existing configuration CRC and credentials.
static const int EEPROM_SEASON_BOOST_ADDR = 512;
static const int EEPROM_SEASON_BOOST_SIZE = 12;
static_assert(EEPROM_SEASON_BOOST_ADDR >= 494 &&
              EEPROM_SEASON_BOOST_ADDR + EEPROM_SEASON_BOOST_SIZE <= 1024,
              "season boost block must stay between push settings and heat tracking");

static uint16_t seasonBoostCrc(const uint8_t* bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < 10; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

bool eeprom_saveEnvSeasonBoostTimes(const uint16_t* seconds) {
    uint8_t saved[EEPROM_SEASON_BOOST_SIZE] = {0xB5, 1};
    for (uint8_t i = 0; i < 4; ++i) {
        if (seconds[i] > 600) {
            Serial.println("EEPROM: seasonal boost out of range");
            return false;
        }
        saved[2 + i * 2] = seconds[i] & 0xFF;
        saved[3 + i * 2] = seconds[i] >> 8;
    }
    uint16_t crc = seasonBoostCrc(saved);
    saved[10] = crc & 0xFF;
    saved[11] = crc >> 8;
    EEPROM.put(EEPROM_SEASON_BOOST_ADDR, saved);
    uint8_t verified[EEPROM_SEASON_BOOST_SIZE];
    EEPROM.get(EEPROM_SEASON_BOOST_ADDR, verified);
    if (memcmp(saved, verified, sizeof(saved)) != 0) {
        Serial.println("EEPROM: seasonal boost save failed");
        return false;
    }
    return true;
}

static void eeprom_loadEnvSeasonBoostTimes() {
    uint8_t saved[EEPROM_SEASON_BOOST_SIZE];
    EEPROM.get(EEPROM_SEASON_BOOST_ADDR, saved);
    uint16_t crc = saved[10] | (uint16_t(saved[11]) << 8);
    bool valid = saved[0] == 0xB5 && saved[1] == 1 && crc == seasonBoostCrc(saved);
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t seconds = saved[2 + i * 2] | (uint16_t(saved[3 + i * 2]) << 8);
        if (seconds > 600) valid = false;
    }
    if (valid) {
        for (uint8_t i = 0; i < 4; ++i)
            sys.envBoostTimeSeconds[i] = saved[2 + i * 2] | (uint16_t(saved[3 + i * 2]) << 8);
        return;
    }
    Serial.println("EEPROM: initialize seasonal boost from General boost");
    for (uint8_t i = 0; i < 4; ++i) sys.envBoostTimeSeconds[i] = sys.boostTimeSeconds;
    if (!eeprom_saveEnvSeasonBoostTimes(sys.envBoostTimeSeconds))
        Serial.println("EEPROM: seasonal boost defaults not persisted");
}

static void eeprom_markConfigValid();

static const int EEPROM_BOOST_FAN_ADDR = 576;
static const int EEPROM_BOOST_FAN_SIZE = 9;
static uint8_t savedBoostFans[5];
static_assert(EEPROM_BOOST_FAN_ADDR >= 565 &&
              EEPROM_BOOST_FAN_ADDR + EEPROM_BOOST_FAN_SIZE <= 1024,
              "boost speeds must not overlap network settings or heat tracking");

static uint16_t boostFanCrc(const uint8_t* bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < 7; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static bool eeprom_writeBoostFans(const uint8_t* percent) {
    uint8_t saved[EEPROM_BOOST_FAN_SIZE] = {0xB8, 1};
    for (uint8_t i = 0; i < 5; ++i) {
        if (percent[i] > 100) {
            Serial.println("EEPROM: boost fan out of range");
            return false;
        }
        saved[2 + i] = percent[i];
    }
    uint16_t crc = boostFanCrc(saved);
    saved[7] = crc & 0xFF;
    saved[8] = crc >> 8;
    EEPROM.put(EEPROM_BOOST_FAN_ADDR, saved);
    uint8_t verified[EEPROM_BOOST_FAN_SIZE];
    EEPROM.get(EEPROM_BOOST_FAN_ADDR, verified);
    if (memcmp(saved, verified, sizeof(saved)) != 0) {
        Serial.println("EEPROM: boost fan save failed");
        return false;
    }
    if (percent != savedBoostFans)
        memcpy(savedBoostFans, percent, sizeof(savedBoostFans));
    return true;
}

bool eeprom_saveBoostFanPercent(int percent) {
    if (percent < 0 || percent > 100) {
        Serial.println("EEPROM: boost fan out of range");
        return false;
    }
    uint8_t candidate[5];
    memcpy(candidate, savedBoostFans, sizeof(candidate));
    candidate[0] = percent;
    return eeprom_writeBoostFans(candidate);
}

bool eeprom_saveEnvBoostFanPercent(const uint8_t* percent) {
    uint8_t candidate[5];
    candidate[0] = savedBoostFans[0];
    memcpy(candidate + 1, percent, 4);
    return eeprom_writeBoostFans(candidate);
}

static void eeprom_loadBoostFans() {
    uint8_t saved[EEPROM_BOOST_FAN_SIZE];
    EEPROM.get(EEPROM_BOOST_FAN_ADDR, saved);
    uint16_t crc = saved[7] | (uint16_t(saved[8]) << 8);
    bool valid = saved[0] == 0xB8 && saved[1] == 1 && crc == boostFanCrc(saved);
    for (uint8_t i = 0; i < 5; ++i)
        if (saved[2 + i] > 100) valid = false;
    int maxima[5] = {sys.clampMaxPercent, sys.envClampMaxSummerPercent,
                    sys.envClampMaxSpringFallPercent, sys.envClampMaxWinterPercent,
                    sys.envClampMaxExtremePercent};
    bool adjusted = false;
    for (uint8_t i = 0; i < 5; ++i) {
        int percent = valid ? saved[2 + i] : maxima[i];
        savedBoostFans[i] = constrain(percent, sys.clampMinPercent, maxima[i]);
        if (savedBoostFans[i] != percent) adjusted = true;
    }
    sys.boostFanPercent = savedBoostFans[0];
    for (uint8_t i = 0; i < 4; ++i) sys.envBoostFanPercent[i] = savedBoostFans[i + 1];
    if (!valid || adjusted) {
        Serial.println(valid ? "EEPROM: boost speeds adjusted to saved fan limits" :
                               "EEPROM: initialize boost speeds from fan maxima");
        if (!eeprom_writeBoostFans(savedBoostFans))
            Serial.println("EEPROM: boost speed defaults not persisted");
    }
}

static const int EEPROM_DAMPER_DELAY_ADDR = 592;
static const int EEPROM_DAMPER_DELAY_SIZE = 14;
static uint16_t savedDamperDelays[5] = {5, 5, 5, 5, 5};
static_assert(EEPROM_DAMPER_DELAY_ADDR >= EEPROM_BOOST_FAN_ADDR + EEPROM_BOOST_FAN_SIZE &&
              EEPROM_DAMPER_DELAY_ADDR + EEPROM_DAMPER_DELAY_SIZE <= 1024,
              "damper delays must not overlap boost speeds or heat tracking");

static uint16_t damperDelayCrc(const uint8_t* bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < 12; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static bool eeprom_writeDamperDelays(const uint16_t* seconds) {
    uint8_t saved[EEPROM_DAMPER_DELAY_SIZE] = {0xB9, 1};
    for (uint8_t i = 0; i < 5; ++i) {
        if (seconds[i] > 300) {
            Serial.println("EEPROM: damper delay out of range");
            return false;
        }
        saved[2 + i * 2] = seconds[i] & 0xFF;
        saved[3 + i * 2] = seconds[i] >> 8;
    }
    uint16_t crc = damperDelayCrc(saved);
    saved[12] = crc & 0xFF;
    saved[13] = crc >> 8;
    EEPROM.put(EEPROM_DAMPER_DELAY_ADDR, saved);
    uint8_t verified[EEPROM_DAMPER_DELAY_SIZE];
    EEPROM.get(EEPROM_DAMPER_DELAY_ADDR, verified);
    if (memcmp(saved, verified, sizeof(saved)) != 0) {
        Serial.println("EEPROM: damper delay save failed");
        return false;
    }
    if (seconds != savedDamperDelays)
        memcpy(savedDamperDelays, seconds, sizeof(savedDamperDelays));
    return true;
}

bool eeprom_saveDamperDelay(int seconds) {
    if (seconds < 0 || seconds > 300) {
        Serial.println("EEPROM: damper delay out of range");
        return false;
    }
    uint16_t candidate[5];
    memcpy(candidate, savedDamperDelays, sizeof(candidate));
    candidate[0] = seconds;
    return eeprom_writeDamperDelays(candidate);
}

bool eeprom_saveEnvDamperDelays(const uint16_t* seconds) {
    uint16_t candidate[5];
    candidate[0] = savedDamperDelays[0];
    memcpy(candidate + 1, seconds, 4 * sizeof(uint16_t));
    return eeprom_writeDamperDelays(candidate);
}

static void eeprom_loadDamperDelays() {
    uint8_t saved[EEPROM_DAMPER_DELAY_SIZE];
    EEPROM.get(EEPROM_DAMPER_DELAY_ADDR, saved);
    uint16_t crc = saved[12] | (uint16_t(saved[13]) << 8);
    bool valid = saved[0] == 0xB9 && saved[1] == 1 && crc == damperDelayCrc(saved);
    for (uint8_t i = 0; i < 5; ++i)
        if ((saved[2 + i * 2] | (uint16_t(saved[3 + i * 2]) << 8)) > 300) valid = false;
    for (uint8_t i = 0; i < 5; ++i)
        savedDamperDelays[i] = valid ?
            saved[2 + i * 2] | (uint16_t(saved[3 + i * 2]) << 8) : 5;
    sys.damperDelaySeconds = savedDamperDelays[0];
    for (uint8_t i = 0; i < 4; ++i) sys.envDamperDelaySeconds[i] = savedDamperDelays[i + 1];
    if (!valid) {
        Serial.println("EEPROM: initialize damper delays to 5 seconds");
        if (!eeprom_writeDamperDelays(savedDamperDelays))
            Serial.println("EEPROM: damper delay defaults not persisted");
    }
}

static const int EEPROM_NETWORK_ADDR = 544;
static const int EEPROM_NETWORK_SIZE = 21;
static_assert(EEPROM_NETWORK_ADDR >= EEPROM_SEASON_BOOST_ADDR + EEPROM_SEASON_BOOST_SIZE &&
              EEPROM_NETWORK_ADDR + EEPROM_NETWORK_SIZE <= 1024,
              "network settings must not overlap seasonal boost or heat tracking");

static uint16_t networkCrc(const uint8_t* bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < 19; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

bool eeprom_saveNetworkSettings(const NetworkSettings& settings) {
    const char* error = network_validate(settings);
    if (error) {
        Serial.println(error);
        return false;
    }
    uint8_t saved[EEPROM_NETWORK_SIZE] = {0xB6, 1, settings.fixed};
    if (settings.fixed) {
        memcpy(saved + 3, settings.ip, 4);
        memcpy(saved + 7, settings.gateway, 4);
        memcpy(saved + 11, settings.subnet, 4);
        memcpy(saved + 15, settings.dns, 4);
    }
    uint16_t crc = networkCrc(saved);
    saved[19] = crc & 0xFF;
    saved[20] = crc >> 8;
    EEPROM.put(EEPROM_NETWORK_ADDR, saved);
    uint8_t verified[EEPROM_NETWORK_SIZE];
    EEPROM.get(EEPROM_NETWORK_ADDR, verified);
    if (memcmp(saved, verified, sizeof(saved)) != 0) {
        Serial.println("EEPROM: network settings save failed");
        return false;
    }
    return true;
}

bool eeprom_loadNetworkSettings() {
    uint8_t saved[EEPROM_NETWORK_SIZE];
    EEPROM.get(EEPROM_NETWORK_ADDR, saved);
    bool erased = true, zeroed = true;
    for (uint8_t i = 0; i < EEPROM_NETWORK_SIZE; ++i) {
        if (saved[i] != 0xFF) erased = false;
        if (saved[i] != 0) zeroed = false;
    }
    networkSettings = NetworkSettings();
    if (erased || zeroed) {
        Serial.println("WiFi: no saved address settings; using DHCP");
        return true;
    }
    uint16_t crc = saved[19] | (uint16_t(saved[20]) << 8);
    if (saved[0] != 0xB6 || saved[1] != 1 || crc != networkCrc(saved)) {
        Serial.println("EEPROM: invalid network block; WiFi setup required");
        return false;
    }
    NetworkSettings loaded;
    loaded.fixed = saved[2];
    memcpy(loaded.ip, saved + 3, 4);
    memcpy(loaded.gateway, saved + 7, 4);
    memcpy(loaded.subnet, saved + 11, 4);
    memcpy(loaded.dns, saved + 15, 4);
    const char* error = network_validate(loaded);
    if (error) {
        Serial.println(error);
        return false;
    }
    networkSettings = loaded;
    return true;
}

/* ============================================================
 *  INTERNAL HELPERS FOR MULTIBYTE VALUES
 * ============================================================ */

static void eeprom_write16(int addr, int16_t value) {
    EEPROM.update(addr,     (uint8_t)(value & 0xFF));
    EEPROM.update(addr + 1, (uint8_t)((value >> 8) & 0xFF));
    eeprom_markConfigValid();
}

static int16_t eeprom_read16(int addr) {
    uint8_t lo = EEPROM.read(addr);
    uint8_t hi = EEPROM.read(addr + 1);
    return (int16_t)((hi << 8) | lo);
}

static uint16_t eeprom_configCrc() {
    uint16_t crc = 0xFFFF;
    for (int addr = 0; addr <= EEPROM_CONFIG_END; addr++) {
        crc ^= EEPROM.read(addr);
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

static bool eeprom_configValid() {
    uint16_t stored = EEPROM.read(EEPROM_CRC_ADDR) |
                      ((uint16_t)EEPROM.read(EEPROM_CRC_ADDR + 1) << 8);
    return EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC &&
           EEPROM.read(EEPROM_VERSION_ADDR) == EEPROM_VERSION &&
           stored == eeprom_configCrc();
}

static void eeprom_markConfigValid() {
    uint16_t crc = eeprom_configCrc();
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.update(EEPROM_VERSION_ADDR, EEPROM_VERSION);
    EEPROM.update(EEPROM_CRC_ADDR, crc & 0xFF);
    EEPROM.update(EEPROM_CRC_ADDR + 1, crc >> 8);
}

static void eeprom_saveDefaultConfig() {
    eeprom_write16(0, sys.exhaustSetpoint);
    eeprom_write16(2, sys.boostTimeSeconds);
    eeprom_write16(4, sys.deadbandF);
    eeprom_write16(6, sys.clampMinPercent);
    eeprom_write16(8, sys.clampMaxPercent);
    EEPROM.update(10, sys.deadzoneFanMode);
    eeprom_write16(12, sys.emberGuardianTimerMinutes);
    eeprom_write16(14, sys.flueLowThreshold);
    eeprom_write16(16, sys.flueRecoveryThreshold);
    EEPROM.update(18, sys.envSeasonMode);
    EEPROM.update(19, sys.envAutoSeasonEnabled ? 1 : 0);
    EEPROM.update(20, sys.envModeLockoutSec / 3600UL);
    eeprom_saveEnvSeasonStarts();
    eeprom_saveEnvSeasonHyst();
    eeprom_saveEnvSeasonSetpoints();
    eeprom_write16(46, sys.tankLowSetpointF);
    eeprom_write16(48, sys.tankHighSetpointF);
    EEPROM.update(50, sys.controlMode);
    EEPROM.put(52, 1.0f);
    EEPROM.update(56, 0);
    EEPROM.update(57, 0x80 | PREDICT_LOOKAHEAD_DEFAULT_SEC);
    eeprom_saveProbeRoles();
    eeprom_saveEnvSeasonTankValues();
    eeprom_saveEnvSeasonClampValues();
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR, sys.selfCleanEnabled ? 1 : 0);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 1, sys.selfCleanIntervalBurns);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 2, sys.selfCleanStartHour);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 3, sys.selfCleanEndHour);
    eeprom_write16(EEPROM_SELF_CLEAN_ADDR + 4, sys.selfCleanUtcOffsetMinutes);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 6, sys.selfCleanDstEnabled ? 1 : 0);
    eeprom_markConfigValid();
}

/* ============================================================
 *  INIT - LOAD ALL SETTINGS YOU SAVE
 * ============================================================ */

static void eeprom_loadRuntimeCreds() {
    runtimeCreds.hasCredentials = EEPROM.read(100) == 1;
    for (unsigned i = 1; i < sizeof(RuntimeCredentials); ++i) {
        ((uint8_t*)&runtimeCreds)[i] = EEPROM.read(100 + i);
    }
    char* fields[] = {runtimeCreds.ssid, runtimeCreds.pass, runtimeCreds.mqttServer,
                      runtimeCreds.mqttUser, runtimeCreds.mqttPass, runtimeCreds.controlPass,
                      runtimeCreds.displayName};
    const unsigned sizes[] = {sizeof(runtimeCreds.ssid), sizeof(runtimeCreds.pass),
                             sizeof(runtimeCreds.mqttServer), sizeof(runtimeCreds.mqttUser),
                             sizeof(runtimeCreds.mqttPass), sizeof(runtimeCreds.controlPass),
                             sizeof(runtimeCreds.displayName)};
    bool invalidFields = false;
    for (uint8_t i = 0; i < 7; ++i) {
        if (!memchr(fields[i], '\0', sizes[i])) {
            memset(fields[i], 0, sizes[i]);
            invalidFields = true;
        }
    }
    if (invalidFields) Serial.println("EEPROM: invalid credential fields cleared");
    if (runtimeCreds.hasCredentials &&
        (runtimeCreds.ssid[0] == '\0' || runtimeCreds.pass[0] == '\0')) {
        runtimeCreds.hasCredentials = false;
        Serial.println("EEPROM: invalid WiFi credentials; setup required");
    }
}

void eeprom_init() {
    // === COMBUSTION SETTINGS ===
    sys.exhaustSetpoint      = eeprom_read16(0);
    sys.boostTimeSeconds     = eeprom_read16(2);
    sys.deadbandF            = eeprom_read16(4);
    sys.clampMinPercent      = eeprom_read16(6);
    sys.clampMaxPercent      = eeprom_read16(8);
    sys.deadzoneFanMode      = EEPROM.read(10);

    // === EMBER GUARDIAN ===
    sys.emberGuardianTimerMinutes = eeprom_read16(12);
    sys.flueLowThreshold          = eeprom_read16(14);
    sys.flueRecoveryThreshold     = eeprom_read16(16);

    // === ENVIRONMENTAL LOGIC (ONLY FIELDS YOU ACTUALLY SAVE) ===
    sys.envSummerStartF      = eeprom_read16(22);
    sys.envSpringFallStartF  = eeprom_read16(24);
    sys.envWinterStartF      = eeprom_read16(26);
    sys.envExtremeStartF     = eeprom_read16(28);

    sys.envHystSummerF       = eeprom_read16(30);
    sys.envHystSpringFallF   = eeprom_read16(32);
    sys.envHystWinterF       = eeprom_read16(34);
    sys.envHystExtremeF      = eeprom_read16(36);

    sys.envSetpointSummerF     = eeprom_read16(38);
    sys.envSetpointSpringFallF = eeprom_read16(40);
    sys.envSetpointWinterF     = eeprom_read16(42);
    sys.envSetpointExtremeF    = eeprom_read16(44);

    sys.envSeasonMode        = EEPROM.read(18);
    sys.envAutoSeasonEnabled = EEPROM.read(19) != 0;
    sys.envModeLockoutSec    = (uint32_t)EEPROM.read(20) * 3600UL;

    sys.envTankHighSummerF     = eeprom_read16(68);
    sys.envTankHighSpringFallF = eeprom_read16(70);
    sys.envTankHighWinterF     = eeprom_read16(72);
    sys.envTankHighExtremeF    = eeprom_read16(74);
    sys.envTankLowSummerF      = eeprom_read16(76);
    sys.envTankLowSpringFallF  = eeprom_read16(78);
    sys.envTankLowWinterF      = eeprom_read16(80);
    sys.envTankLowExtremeF     = eeprom_read16(82);
    sys.envClampMaxSummerPercent     = EEPROM.read(84);
    sys.envClampMaxSpringFallPercent = EEPROM.read(85);
    sys.envClampMaxWinterPercent     = EEPROM.read(86);
    sys.envClampMaxExtremePercent    = EEPROM.read(87);

    // === BOILER CONTROL ===
    sys.tankLowSetpointF     = eeprom_read16(46);
    sys.tankHighSetpointF    = eeprom_read16(48);
    sys.controlMode          = (RunMode)EEPROM.read(50);

    sys.selfCleanEnabled = EEPROM.read(EEPROM_SELF_CLEAN_ADDR) == 1;
    sys.selfCleanIntervalBurns = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 1);
    sys.selfCleanStartHour = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 2);
    sys.selfCleanEndHour = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 3);
    int16_t storedUtcOffset = eeprom_read16(EEPROM_SELF_CLEAN_ADDR + 4);
    uint8_t dstFlagNew = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 6);
    // Format discriminator: the new layout stores minutes (16-bit) at +4/+5
    // and DST at +6. The legacy layout stored hours (8-bit) at +4 and DST at
    // +5. If +6 holds a valid DST value (0/1) AND the 16-bit minutes value is
    // in range, this is the new format - use it as-is. Only fall back to the
    // legacy interpretation when +6 has never been written by the new format.
    bool isNewFormat = (dstFlagNew == 0 || dstFlagNew == 1) &&
                       storedUtcOffset >= -720 && storedUtcOffset <= 840;
    if (isNewFormat) {
        sys.selfCleanUtcOffsetMinutes = storedUtcOffset;
        sys.selfCleanDstEnabled = dstFlagNew == 1;
    } else {
        uint8_t legacyOffset = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 4);
        uint8_t legacyDst = EEPROM.read(EEPROM_SELF_CLEAN_ADDR + 5);
        if ((legacyDst == 0 || legacyDst == 1) &&
            (int8_t)legacyOffset >= -12 && (int8_t)legacyOffset <= 14) {
            sys.selfCleanUtcOffsetMinutes = (int16_t)(int8_t)legacyOffset * 60;
            sys.selfCleanDstEnabled = legacyDst == 1;
        } else {
            sys.selfCleanUtcOffsetMinutes = -360;
            sys.selfCleanDstEnabled = false;
        }
    }

    // === PROBE ROLES ===
    // Default: Tank probe = physical probe 0
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        sys.probeRoleMap[i] = EEPROM.read(60 + i);
        if (sys.probeRoleMap[i] >= MAX_WATER_PROBES) {
            sys.probeRoleMap[i] = 0;
        }
    }
    // Probe names are loaded by serial number during the boot probe scan.
    // === RUNTIME CREDENTIALS ===
    eeprom_loadRuntimeCreds();
    bool displayNameValid = false;
    for (uint8_t i = 0; i < sizeof(runtimeCreds.displayName); i++) {
        uint8_t value = (uint8_t)runtimeCreds.displayName[i];
        if (value == '\0') {
            displayNameValid = true;
            break;
        }
        if (value < 32 || value > 126) {
            break;
        }
    }
    if (!displayNameValid) {
        runtimeCreds.displayName[0] = '\0';
    }

    /* ========================================================
     *  SAFETY CLAMPS - PREVENT INVALID EEPROM VALUES
     * ======================================================== */

    // BOOST TIME - user-configured; 0 means skip straight to RAMP.
    // Only reset to the default when the stored value is garbage.
    if (sys.boostTimeSeconds < 0 || sys.boostTimeSeconds > 600) {
        sys.boostTimeSeconds = 30;   // safe default
    }

    // Exhaust setpoint sanity
    if (sys.exhaustSetpoint < 200 || sys.exhaustSetpoint > 900) {
        sys.exhaustSetpoint = 400;
    }

    // Deadband sanity
    if (sys.deadbandF < 1 || sys.deadbandF > 100) {
        sys.deadbandF = 10;
    }

    // Fan clamp sanity
    if (sys.clampMinPercent < 0 || sys.clampMinPercent > 100) {
        sys.clampMinPercent = 10;
    }
    if (sys.clampMaxPercent < 0 || sys.clampMaxPercent > 100) {
        sys.clampMaxPercent = 90;
    }

    // Guardian thresholds sanity
    if (sys.emberGuardianTimerMinutes < 1 || sys.emberGuardianTimerMinutes > 120) {
        sys.emberGuardianTimerMinutes = 10;
    }
    if (sys.flueLowThreshold < 50 || sys.flueLowThreshold > 500) {
        sys.flueLowThreshold = 120;
    }
    if (sys.flueRecoveryThreshold < 50 || sys.flueRecoveryThreshold > 500) {
        sys.flueRecoveryThreshold = 180;
    }

    if (sys.envSeasonMode > 2) sys.envSeasonMode = 0;
    if (sys.envModeLockoutSec > 99UL * 3600UL) sys.envModeLockoutSec = 0;

    if (sys.selfCleanIntervalBurns == 0 || sys.selfCleanIntervalBurns == 255) {
        sys.selfCleanIntervalBurns = 30;
    }
    if (sys.selfCleanStartHour > 23) sys.selfCleanStartHour = 23;
    if (sys.selfCleanEndHour > 23) sys.selfCleanEndHour = 6;
    if (sys.selfCleanUtcOffsetMinutes < -720 || sys.selfCleanUtcOffsetMinutes > 840) {
        sys.selfCleanUtcOffsetMinutes = -360;
    }

    if (sys.envClampMaxSummerPercent > 100) sys.envClampMaxSummerPercent = 40;
    if (sys.envClampMaxSpringFallPercent > 100) sys.envClampMaxSpringFallPercent = 50;
    if (sys.envClampMaxWinterPercent > 100) sys.envClampMaxWinterPercent = 60;
    if (sys.envClampMaxExtremePercent > 100) sys.envClampMaxExtremePercent = 70;

    if (sys.envTankLowSummerF >= sys.envTankHighSummerF) {
        sys.envTankLowSummerF = 150;
        sys.envTankHighSummerF = 170;
    }
    if (sys.envTankLowSpringFallF >= sys.envTankHighSpringFallF) {
        sys.envTankLowSpringFallF = 155;
        sys.envTankHighSpringFallF = 175;
    }
    if (sys.envTankLowWinterF >= sys.envTankHighWinterF) {
        sys.envTankLowWinterF = 160;
        sys.envTankHighWinterF = 180;
    }
    if (sys.envTankLowExtremeF >= sys.envTankHighExtremeF) {
        sys.envTankLowExtremeF = 165;
        sys.envTankHighExtremeF = 185;
    }

    // If MAGIC + VERSION are present but the CRC does not match, a real
    // config was torn by a power cut mid-write. Do NOT factory-reset: keep
    // the values already loaded and clamped above (each field was forced to
    // a safe range), then re-commit a valid CRC. Only a field that was
    // actually mid-write is lost; everything else survives. A truly blank
    // EEPROM (no MAGIC/VERSION) still gets factory defaults.
    bool magicPresent = EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC &&
                        EEPROM.read(EEPROM_VERSION_ADDR) == EEPROM_VERSION;

    if (!eeprom_configValid()) {
        if (magicPresent) {
            // Torn write: persist the clamped values back out with a fresh CRC.
            eeprom_markConfigValid();
        } else {
            // Fresh / blank EEPROM: load factory defaults.
            systemdata_init();
            eeprom_saveDefaultConfig();
        }
    }

    eeprom_loadEnvSeasonBoostTimes();
    eeprom_loadBoostFans();
    eeprom_loadDamperDelays();
    sys.envUnitsMetric = EEPROM.read(EEPROM_ENV_UNITS_ADDR) == 1 ? 1 : 0;

    // === PUSH NOTIFICATIONS ===
    sys.pushEnabled = EEPROM.read(EEPROM_PUSH_ENABLED_ADDR) == 1;
    bool topicValid = false;
    for (uint8_t i = 0; i < 33; i++) {
        char c = (char)EEPROM.read(EEPROM_PUSH_TOPIC_ADDR + i);
        sys.pushTopic[i] = c;
        if (c == '\0') { topicValid = true; break; }
        bool okChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!okChar) { topicValid = false; break; }
    }
    if (!topicValid || sys.pushTopic[0] == '\0') {
        sys.pushTopic[0] = '\0';
        sys.pushEnabled = false;
    }
}

void eeprom_savePushNotify() {
    // One page commit, not a 1 KB flash erase/write for every topic character.
    uint8_t saved[1 + sizeof(sys.pushTopic)];
    saved[0] = sys.pushEnabled ? 1 : 0;
    memcpy(saved + 1, sys.pushTopic, sizeof(sys.pushTopic));
    EEPROM.put(EEPROM_PUSH_ENABLED_ADDR, saved);
}

/* ============================================================
 *  CORE COMBUSTION SAVES
 * ============================================================ */

void eeprom_saveSetpoint(int v) {
    eeprom_write16(0, (int16_t)v);
}

void eeprom_saveBoostTime(int v) {
    eeprom_write16(2, (int16_t)v);
}

void eeprom_saveDeadband(int v) {
    eeprom_write16(4, (int16_t)v);
}

void eeprom_saveClampMin(int v) {
    eeprom_write16(6, (int16_t)v);
}

void eeprom_saveClampMax(int v) {
    eeprom_write16(8, (int16_t)v);
}

void eeprom_saveDeadzone(int v) {
    EEPROM.update(10, (uint8_t)v);
    eeprom_markConfigValid();
}

float eeprom_loadAdaptiveSlope() {
    float value = 1.0f;
    EEPROM.get(52, value);
    if (isnan(value) || value < 0.85f || value > 1.20f) {
        return 1.0f;
    }
    return value;
}

void eeprom_saveAdaptiveSlope(float value) {
    value = constrain(value, 0.85f, 1.20f);
    EEPROM.put(52, value);
    eeprom_markConfigValid();
}

bool eeprom_loadAdaptiveLearning() {
    return EEPROM.read(56) == 1;
}

void eeprom_saveAdaptiveLearning(bool enabled) {
    EEPROM.update(56, enabled ? 1 : 0);
    eeprom_markConfigValid();
}

// Byte 57 stores 0x80 | seconds so an older board's blank or zero byte loads the default.
uint8_t eeprom_loadPredictLookahead() {
    uint8_t raw = EEPROM.read(57);
    uint8_t seconds = raw & 0x7F;
    if (!(raw & 0x80) || raw == 0xFF || seconds > PREDICT_LOOKAHEAD_MAX_SEC) {
        return PREDICT_LOOKAHEAD_DEFAULT_SEC;
    }
    return seconds;
}

void eeprom_savePredictLookahead(uint8_t seconds) {
    if (seconds > PREDICT_LOOKAHEAD_MAX_SEC) seconds = PREDICT_LOOKAHEAD_MAX_SEC;
    EEPROM.update(57, 0x80 | seconds);
    eeprom_markConfigValid();
}

/* ============================================================
 *  EMBER GUARDIAN SAVES
 * ============================================================ */

void eeprom_saveEmberGuardianMinutes(int v) {
    eeprom_write16(12, (int16_t)v);
}

void eeprom_saveFlueLow(int v) {
    eeprom_write16(14, (int16_t)v);
}

void eeprom_saveFlueRecovery(int v) {
    eeprom_write16(16, (int16_t)v);
}

/* ============================================================
 *  PROBE ROLES
 * ============================================================ */

void eeprom_saveProbeRoles() {
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        EEPROM.write(60 + i, sys.probeRoleMap[i]);
    }
    eeprom_markConfigValid();
}

static bool probeTableReady() {
    return EEPROM.read(EEPROM_PROBE_TABLE_ADDR) == EEPROM_PROBE_TABLE_MAGIC &&
           EEPROM.read(EEPROM_PROBE_TABLE_ADDR + 1) == EEPROM_PROBE_TABLE_VERSION;
}

// First save only marks the slots empty (byte 0 = 0xFF): every R4 EEPROM byte
// write is a slow flash cycle, so the table is never blanked wholesale.
// Magic is written last.
static void probeTableInit() {
    if (probeTableReady()) return;
    EEPROM.update(EEPROM_TANK_ROM_ADDR, 0xFF);
    for (uint8_t s = 0; s < EEPROM_PROBE_SLOTS; s++) {
        EEPROM.update(EEPROM_PROBE_SLOTS_ADDR + s * EEPROM_PROBE_SLOT_SIZE, 0xFF);
    }
    EEPROM.update(EEPROM_PROBE_TABLE_ADDR + 1, EEPROM_PROBE_TABLE_VERSION);
    EEPROM.update(EEPROM_PROBE_TABLE_ADDR, EEPROM_PROBE_TABLE_MAGIC);
}

static int probeSlotAddr(uint8_t slot) {
    return EEPROM_PROBE_SLOTS_ADDR + slot * EEPROM_PROBE_SLOT_SIZE;
}

static bool romAt(int addr, const uint8_t* rom) {
    for (uint8_t i = 0; i < 8; i++) {
        if (EEPROM.read(addr + i) != rom[i]) return false;
    }
    return true;
}

static int probeSlotFind(const uint8_t* rom) {
    if (!probeTableReady()) return -1;
    for (uint8_t s = 0; s < EEPROM_PROBE_SLOTS; s++) {
        if (romAt(probeSlotAddr(s), rom)) return s;
    }
    return -1;
}

bool eeprom_loadTankRom(uint8_t* rom) {
    if (!probeTableReady()) return false;
    for (uint8_t i = 0; i < 8; i++) rom[i] = EEPROM.read(EEPROM_TANK_ROM_ADDR + i);
    return rom[0] != 0xFF && rom[0] != 0x00;
}

void eeprom_saveTankRom(const uint8_t* rom) {
    probeTableInit();
    for (uint8_t i = 0; i < 8; i++) EEPROM.update(EEPROM_TANK_ROM_ADDR + i, rom[i]);
}

bool eeprom_loadProbeName(const uint8_t* rom, char* name) {
    int slot = probeSlotFind(rom);
    if (slot < 0) return false;
    int addr = probeSlotAddr(slot) + 8;
    for (uint8_t i = 0; i < PROBE_NAME_LENGTH; i++) {
        char c = (char)EEPROM.read(addr + i);
        if (c == '\0') { name[i] = '\0'; return i > 0; }
        if (c < 32 || c > 126) return false;
        name[i] = c;
    }
    name[PROBE_NAME_LENGTH - 1] = '\0';
    return true;
}

// present[] is the probes on the bus now; their names are never evicted.
void eeprom_saveProbeName(const uint8_t* rom, const char* name,
                          const uint8_t (*present)[8], uint8_t presentCount) {
    probeTableInit();
    int slot = probeSlotFind(rom);
    for (uint8_t s = 0; slot < 0 && s < EEPROM_PROBE_SLOTS; s++) {
        if (EEPROM.read(probeSlotAddr(s)) == 0xFF) slot = s;
    }
    for (uint8_t s = 0; slot < 0 && s < EEPROM_PROBE_SLOTS; s++) {
        bool inUse = false;
        for (uint8_t p = 0; p < presentCount && !inUse; p++) {
            inUse = romAt(probeSlotAddr(s), present[p]);
        }
        if (!inUse) slot = s;
    }
    if (slot < 0) slot = 0;
    int addr = probeSlotAddr(slot);
    for (uint8_t i = 0; i < 8; i++) EEPROM.update(addr + i, rom[i]);
    bool ended = false;
    for (uint8_t i = 0; i < PROBE_NAME_LENGTH; i++) {
        char c = ended || i == PROBE_NAME_LENGTH - 1 ? '\0' : name[i];
        if (c == '\0') ended = true;
        EEPROM.update(addr + 8 + i, (uint8_t)c);
    }
}

/* ============================================================
 *  ENVIRONMENTAL LOGIC SAVES
 * ============================================================ */

void eeprom_saveEnvSeasonMode(uint8_t mode) {
    EEPROM.write(18, mode);
    eeprom_markConfigValid();
}

void eeprom_saveEnvAutoSeason(bool en) {
    EEPROM.write(19, en ? 1 : 0);
    eeprom_markConfigValid();
}

void eeprom_saveEnvLockoutHours(uint8_t hours) {
    EEPROM.write(20, hours);
    eeprom_markConfigValid();
}

void eeprom_saveEnvUnits(uint8_t metric) {
    EEPROM.update(EEPROM_ENV_UNITS_ADDR, metric ? 1 : 0);
}

void eeprom_saveEnvSeasonStarts() {
    eeprom_write16(22, sys.envSummerStartF);
    eeprom_write16(24, sys.envSpringFallStartF);
    eeprom_write16(26, sys.envWinterStartF);
    eeprom_write16(28, sys.envExtremeStartF);
}

void eeprom_saveEnvSeasonHyst() {
    eeprom_write16(30, sys.envHystSummerF);
    eeprom_write16(32, sys.envHystSpringFallF);
    eeprom_write16(34, sys.envHystWinterF);
    eeprom_write16(36, sys.envHystExtremeF);
}

void eeprom_saveEnvSeasonSetpoints() {
    eeprom_write16(38, sys.envSetpointSummerF);
    eeprom_write16(40, sys.envSetpointSpringFallF);
    eeprom_write16(42, sys.envSetpointWinterF);
    eeprom_write16(44, sys.envSetpointExtremeF);
}

void eeprom_saveEnvSeasonTankValues() {
    eeprom_write16(68, sys.envTankHighSummerF);
    eeprom_write16(70, sys.envTankHighSpringFallF);
    eeprom_write16(72, sys.envTankHighWinterF);
    eeprom_write16(74, sys.envTankHighExtremeF);
    eeprom_write16(76, sys.envTankLowSummerF);
    eeprom_write16(78, sys.envTankLowSpringFallF);
    eeprom_write16(80, sys.envTankLowWinterF);
    eeprom_write16(82, sys.envTankLowExtremeF);
}

void eeprom_saveEnvSeasonClampValues() {
    EEPROM.write(84, sys.envClampMaxSummerPercent);
    EEPROM.write(85, sys.envClampMaxSpringFallPercent);
    EEPROM.write(86, sys.envClampMaxWinterPercent);
    EEPROM.write(87, sys.envClampMaxExtremePercent);
    eeprom_markConfigValid();
}

void eeprom_saveSelfClean() {
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR, sys.selfCleanEnabled ? 1 : 0);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 1, sys.selfCleanIntervalBurns);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 2, sys.selfCleanStartHour);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 3, sys.selfCleanEndHour);
    eeprom_write16(EEPROM_SELF_CLEAN_ADDR + 4, sys.selfCleanUtcOffsetMinutes);
    EEPROM.update(EEPROM_SELF_CLEAN_ADDR + 6, sys.selfCleanDstEnabled ? 1 : 0);
}

/* ============================================================
 *  BOILER CONTROL SAVES
 * ============================================================ */

void eeprom_saveTankLow(int v) {
    eeprom_write16(46, (int16_t)v);
}

void eeprom_saveTankHigh(int v) {
    eeprom_write16(48, (int16_t)v);
}

void eeprom_saveRunMode(uint8_t mode) {
    EEPROM.write(50, mode);
    eeprom_markConfigValid();
}

/* ============================================================
 *  RUNTIME CREDENTIALS
 * ============================================================ */

bool eeprom_saveRuntimeCreds(const RuntimeCredentials& credentials) {
    // update(): only changed bytes cost a (slow) flash write.
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        EEPROM.update(100 + i, ((const uint8_t*)&credentials)[i]);
    }
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        if (EEPROM.read(100 + i) != ((const uint8_t*)&credentials)[i]) {
            Serial.println("EEPROM: credentials save failed");
            return false;
        }
    }
    return true;
}

bool eeprom_saveRuntimeCreds() {
    return eeprom_saveRuntimeCreds(runtimeCreds);
}

bool eeprom_saveProvisioning(const RuntimeCredentials& credentials,
                             const NetworkSettings& settings) {
    const char* error = network_validate(settings);
    if (error) {
        Serial.println(error);
        return false;
    }
    // Leave boot in setup, not on a half-written credentials/address pair.
    EEPROM.update(100, 0);
    if (EEPROM.read(100) != 0) {
        Serial.println("EEPROM: could not invalidate provisioning");
        return false;
    }
    if (!eeprom_saveNetworkSettings(settings)) return false;
    RuntimeCredentials pending = credentials;
    pending.hasCredentials = false;
    if (!eeprom_saveRuntimeCreds(pending)) return false;
    EEPROM.update(100, credentials.hasCredentials ? 1 : 0);
    if (EEPROM.read(100) != (credentials.hasCredentials ? 1 : 0)) {
        Serial.println("EEPROM: provisioning commit failed");
        return false;
    }
    return true;
}
