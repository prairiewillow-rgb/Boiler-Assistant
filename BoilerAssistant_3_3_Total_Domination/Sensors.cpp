/*
 * Boiler Assistant - Sensors
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "Sensors.h"
#include "DebugLog.h"
#include "SystemData.h"
#include "SystemState.h"
#include "EEPROMStorage.h"
#include "Pinout.h"
#include "I2CBus.h"
#include "BoundedBME280.h"
#include "WaterCalibration.h"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MAX31855.h>

/* ============================================================
 *  GLOBALS
 * ============================================================ */

extern SystemData sys;

// DS18B20 bus + driver
static OneWire oneWire(PIN_DS18B20_DATA);
static DallasTemperature waterSensors(&oneWire);
static DeviceAddress probeAddr[MAX_WATER_PROBES];
static DeviceAddress tankProbeAddress;
static bool tankProbeAddressValid = false;
static uint8_t tankIndexAtLastScan = MAX_WATER_PROBES;

static const unsigned long WATER_SCAN_BUDGET_MS = 400UL;
static const uint8_t WATER_BOOT_SCAN_PASSES = 3;
static const unsigned long WATER_READ_PERIOD_MS = 500UL;
static const unsigned long WATER_CONVERT_MS = 120UL;
static const unsigned long WATER_STALE_MS = 10000UL;
static bool waterConverting = false;
static unsigned long waterRequestMs = 0;
static uint8_t waterReadNext = 0;
static unsigned long probeFoundMs[MAX_WATER_PROBES];
static const unsigned long PROBE_MISSING_MS = 60000UL;

// BME280
static BoundedBME280 bme;
static unsigned long bmeLastAttemptMs = 0;
static uint32_t bmeBusGeneration = 0;
static const unsigned long BME_RETRY_MS = 30000UL;

static void invalidateBME() {
    sys.envSensorOK = false;
    sys.envTempF = sys.envHumidity = sys.envPressure = NAN;
    bme.cancel();
}

static Adafruit_MAX31855 max31855(
    PIN_MAX31855_SCK,
    PIN_TC1_CS,
    PIN_MAX31855_MISO
);

static unsigned long lastExhaustReadMs = 0;
static double lastExhaustF = NAN;

/* ============================================================
 *  EXHAUST SENSOR (MAX31855)
 * ============================================================ */

double exhaust_readF_cached() {
    unsigned long now = millis();
    if (now - lastExhaustReadMs < 250) {
        return lastExhaustF;
    }

    lastExhaustReadMs = now;

    double c = max31855.readCelsius();
    // A missing/unpowered MAX31855 or broken DO wire reads all zeros, which
    // the library returns as exactly 0 C (32 F) with no fault bit. A real
    // reading also has a non-zero board (cold-junction) temperature.
    if (c == 0.0 && max31855.readInternal() == 0.0) c = NAN;

    if (isnan(c)) {
        // Hold the last good value through brief bus/thermocouple glitches.
        sys.exhaustSensorOK = !isnan(lastExhaustF) &&
                              now - sys.exhaustLastGoodMs <= 1000UL;
        return lastExhaustF;
    }

    sys.exhaustSensorOK = true;
    sys.exhaustLastGoodMs = now;

    lastExhaustF = c * 9.0 / 5.0 + 32.0;
    return lastExhaustF;
}

/* ============================================================
 *  WATER PROBE SCAN
 * ============================================================ */

// OneWire search returns devices in ROM bit order (bit 0 first, 0 before 1).
// Sorting merged boot passes the same way keeps 3.3.12 probe numbering, so
// a 3.3.12 tank choice (saved by number) lands on the same probe once.
// Names and the tank probe are then kept by serial number (EEPROM table).
static bool probeSearchOrderLess(const uint8_t* a, const uint8_t* b) {
    for (uint8_t bit = 0; bit < 64; bit++) {
        uint8_t ba = (a[bit >> 3] >> (bit & 7)) & 1;
        uint8_t bb = (b[bit >> 3] >> (bit & 7)) & 1;
        if (ba != bb) return ba < bb;
    }
    return false;
}

static int findAddress(DeviceAddress* list, uint8_t count, const uint8_t* addr) {
    for (uint8_t i = 0; i < count; i++) {
        if (memcmp(list[i], addr, 8) == 0) return i;
    }
    return -1;
}

// One budgeted bus search. Adds new CRC-valid addresses to list[] and keeps
// anything found before the budget ran out. Boot keeps the list in search
// order so merged passes number the probes like a single complete search.
static void searchWaterBus(DeviceAddress* list, uint8_t& count, bool sorted) {
    oneWire.reset_search();
    DeviceAddress addr;
    unsigned long startMs = millis();
    uint8_t attempts = 0;

    while (true) {
        if (attempts >= MAX_WATER_PROBES * 2 ||
            millis() - startMs >= WATER_SCAN_BUDGET_MS) {
            Serial.println("Probes: scan over budget");
            break;
        }
        if (!oneWire.search(addr)) break;
        attempts++;
        if (OneWire::crc8(addr, 7) != addr[7]) {
            Serial.println("Probes: bad CRC");
            continue;
        }
        if (findAddress(list, count, addr) >= 0 || count >= MAX_WATER_PROBES) continue;
        uint8_t pos = count++;
        while (sorted && pos > 0 && probeSearchOrderLess(addr, list[pos - 1])) {
            memcpy(list[pos], list[pos - 1], 8);
            pos--;
        }
        memcpy(list[pos], addr, 8);
    }
    oneWire.reset_search();
}
static void scanWaterProbesInternal(bool boot) {
    uint8_t oldCount = sys.waterProbeCount;

    if (boot) {
        tankProbeAddressValid = eeprom_loadTankRom(tankProbeAddress);
    }

    // Follow the tank role by address. A new role index set from the keypad,
    // MQTT or the dashboard re-captures the address instead of being undone.
    uint8_t tankRole = sys.probeRoleMap[PROBE_TANK];
    if (tankRole < oldCount &&
        (!tankProbeAddressValid || tankRole != tankIndexAtLastScan)) {
        memcpy(tankProbeAddress, probeAddr[tankRole], 8);
        tankProbeAddressValid = true;
        eeprom_saveTankRom(tankProbeAddress);
    }

    // Boot merges several passes, since one search on a long or noisy bus
    // can miss a probe. Runtime: never reorder or drop probes, so names and
    // roles stay on the same physical probe; a probe that stops answering
    // reads as missing (NaN) until it returns, and a new one is appended.
    uint8_t count = boot ? 0 : oldCount;
    for (uint8_t pass = 0; pass < (boot ? WATER_BOOT_SCAN_PASSES : 1); pass++) {
        searchWaterBus(probeAddr, count, boot);
    }
    for (uint8_t i = boot ? 0 : oldCount; i < count; i++) {
        waterSensors.setResolution(probeAddr[i], 9, true);
        sys.waterTempF[i] = NAN;
        sys.waterTempLastGoodMs[i] = 0;
        probeFoundMs[i] = millis();
        // Saved name for this serial number, else "Probe" + last 4 ID digits.
        if (!eeprom_loadProbeName(probeAddr[i], sys.waterProbeNames[i])) {
            snprintf(sys.waterProbeNames[i], PROBE_NAME_LENGTH, "Probe %02X%02X",
                     probeAddr[i][6], probeAddr[i][7]);
        }
    }
    sys.waterProbeCount = count;
    // Upgrade from 3.3.12 (tank saved only by number): adopt that probe once.
    if (!tankProbeAddressValid && tankRole < count) {
        memcpy(tankProbeAddress, probeAddr[tankRole], 8);
        tankProbeAddressValid = true;
        eeprom_saveTankRom(tankProbeAddress);
    }
    if (tankProbeAddressValid) {
        int tank = findAddress(probeAddr, sys.waterProbeCount, tankProbeAddress);
        sys.probeRoleMap[PROBE_TANK] = tank >= 0 ? (uint8_t)tank : MAX_WATER_PROBES;
    }
    tankIndexAtLastScan = sys.probeRoleMap[PROBE_TANK];

    if (boot || sys.waterProbeCount != oldCount) {
        Serial.print("Probes: ");
        Serial.print(sys.waterProbeCount);
        Serial.println(" found");
    }
}

void scanWaterProbes() {
    scanWaterProbesInternal(true);
}

void sensors_rescanWaterProbes() {
    scanWaterProbesInternal(false);
}

bool sensors_setTankProbe(uint8_t index) {
    if (index >= sys.waterProbeCount) return false;
    sys.probeRoleMap[PROBE_TANK] = index;
    memcpy(tankProbeAddress, probeAddr[index], 8);
    tankProbeAddressValid = true;
    tankIndexAtLastScan = index;
    eeprom_saveTankRom(tankProbeAddress);
    eeprom_saveProbeRoles();
    return true;
}

bool sensors_setProbeName(uint8_t index, const char* name) {
    if (index >= sys.waterProbeCount || name[0] == '\0') return false;
    strncpy(sys.waterProbeNames[index], name, PROBE_NAME_LENGTH - 1);
    sys.waterProbeNames[index][PROBE_NAME_LENGTH - 1] = '\0';
    eeprom_saveProbeName(probeAddr[index], sys.waterProbeNames[index],
                         probeAddr, sys.waterProbeCount);
    return true;
}

bool sensors_waterProbeId(uint8_t index, char* output) {
    if (index >= sys.waterProbeCount) return false;
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < 8; ++i) {
        output[i * 2] = hex[probeAddr[index][i] >> 4];
        output[i * 2 + 1] = hex[probeAddr[index][i] & 15];
    }
    output[16] = '\0';
    return true;
}

const char* sensors_missingProbe() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        unsigned long last = sys.waterTempLastGoodMs[i] ? sys.waterTempLastGoodMs[i]
                                                        : probeFoundMs[i];
        if (now - last > PROBE_MISSING_MS) return sys.waterProbeNames[i];
    }
    if (tankProbeAddressValid && sys.probeRoleMap[PROBE_TANK] >= sys.waterProbeCount &&
        now > PROBE_MISSING_MS) return "Tank probe";
    return nullptr;
}

/* ============================================================
 *  WATER PROBE READ
 * ============================================================ */

// Called every loop pass. Starts a conversion every 500 ms, waits for it
// (9-bit = 94 ms), then reads one probe per pass so the bus is never held
// for all probes at once while Wi-Fi is being serviced.
void sensors_readWaterProbes() {
    unsigned long now = millis();

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        // Never-read probes are already NaN, so lastGood = 0 needs no special case.
        if (now - sys.waterTempLastGoodMs[i] > WATER_STALE_MS) sys.waterTempF[i] = NAN;
    }

    if (sys.waterProbeCount == 0) {
        waterConverting = false;
        return;
    }

    if (!waterConverting) {
        if (now - waterRequestMs < WATER_READ_PERIOD_MS) return;
        waterSensors.requestTemperatures();
        waterRequestMs = now;
        waterReadNext = 0;
        waterConverting = true;
        return;
    }

    if (now - waterRequestMs < WATER_CONVERT_MS) return;

    if (waterReadNext < sys.waterProbeCount) {
        uint8_t i = waterReadNext++;
        float c = waterSensors.getTempC(probeAddr[i]);

        if (c > -55 && c < 125) {
            float newF = c * 9.0f / 5.0f + 32.0f + watercal_offset(probeAddr[i]);

            if (isnan(sys.waterTempF[i])) {
                sys.waterTempF[i] = newF;
            } else {
                sys.waterTempF[i] = sys.waterTempF[i] * 0.8f + newF * 0.2f;
            }

            sys.waterTempLastGoodMs[i] = now;
        }
    }
    if (waterReadNext >= sys.waterProbeCount) waterConverting = false;
}
/* ============================================================
 *  BME280 READ
 * ============================================================ */

void sensors_readBME280() {
    if (!i2cbus_ready()) {
        if (sys.envSensorOK) DBG_PRINTLN("BME280: no I2C; outdoor readings invalid");
        invalidateBME();
        return;
    }

    if (!bme.ready()) return;
    if (!bme.measurementAvailable()) {
        DBG_PRINTLN("BME280: check failed; readings cleared");
        invalidateBME();
        bmeLastAttemptMs = millis();
        return;
    }

    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();

    // A dropped or noisy I2C link returns garbage (e.g. ~300F); reject anything outside the BME280's rated range.
    bool valid = !isnan(t) && t >= -40.0f && t <= 85.0f &&
                 !isnan(p) && p >= 30000.0f && p <= 110000.0f &&
                 !isnan(h) && h >= 0.0f && h <= 100.0f &&
                 bme.measurementAvailable();
    if (!valid) {
        DBG_PRINTLN("BME280: bad reading; reinit");
        invalidateBME();
        bmeLastAttemptMs = millis();
        return;
    }

    sys.envTempF    = t * 9.0f / 5.0f + 32.0f;
    sys.envPressure = p / 100.0f;
    sys.envHumidity = h;
    sys.envSensorOK = true;
}

void sensors_loop() {
    if (!i2cbus_ready() || bmeBusGeneration != i2cbus_generation()) {
        bmeBusGeneration = i2cbus_generation();
        if (sys.envSensorOK || bme.pending()) {
            DBG_PRINTLN("BME280: bus reset; init cancelled");
            invalidateBME();
            bme.cancel();
            bmeLastAttemptMs = millis();
        }
        if (!i2cbus_ready()) return;
    }
    if (bme.pending()) {
        BoundedBME280::InitState state = bme.poll();
        if (state == BoundedBME280::READY) {
            DBG_PRINTLN("BME280: OK; awaiting reading");
        } else if (state == BoundedBME280::FAILED) {
            invalidateBME();
            bmeLastAttemptMs = millis();
        }
        return;
    }
    if (!bme.ready() && !sys.envSensorOK && millis() - bmeLastAttemptMs >= BME_RETRY_MS) {
        bmeLastAttemptMs = millis();
        if (!bme.start()) invalidateBME();
    }
}

/* ============================================================
 *  INIT
 * ============================================================ */

bool sensors_init() {
    // BME280
    invalidateBME();
    bmeBusGeneration = i2cbus_generation();
    bmeLastAttemptMs = millis();
    bool ok = i2cbus_ready() && bme.start();

    // Pull DO high so an unplugged thermocouple board reads as a fault
    // (all ones) instead of a floating pin that can read 0 C (32 F).
    max31855.begin();
    pinMode(PIN_MAX31855_MISO, INPUT_PULLUP);

    // DS18B20
    watercal_init();
    waterSensors.begin();
    waterSensors.setWaitForConversion(false);

    scanWaterProbes();

    return ok;
}
