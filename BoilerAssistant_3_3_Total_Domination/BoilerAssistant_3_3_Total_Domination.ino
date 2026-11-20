/*
 * Boiler Assistant 3.4.0 - Total Domination
 * Main firmware for Arduino UNO R4 WiFi
 *
 * Auto Tank and Continuous burn control, with LCD and keypad operation.
 * Monitors a type-K exhaust probe, DS18B20 water probes, and an optional
 * BME280 outdoor sensor for automatic seasonal profiles.
 * The local Wi-Fi dashboard shows live readings, linked boiler and loop
 * charts, and completed periods from the last 24 hours in Burn Times.
 * Optional exFAT SD logging writes one combined CSV per UTC day;
 * saved charts and Burn Times require a working card and valid time.
 *
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include <Arduino.h>
#include <Wire.h>
#include "Watchdog.h"

#include "SystemState.h"          // MUST be first project header
#include "EnvironmentalLogic.h"   // MUST be second
#include "SystemData.h"           // MUST be after both
#include "UI.h"
#include "BurnPerformance.h"
#include "DataLogger.h"
#include "DataShare.h"

#include "EEPROMStorage.h"
#include "Sensors.h"
#include "BurnEngine.h"
#include "FanControl.h"
#include "FanDimmer.h"
#include "Keypad_I2C.h"
#include "I2CBus.h"
#include "Pinout.h"

#include <WiFiS3.h>
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "WiFiProvisioning.h"
#include "OTAUpdater.h"
#include "PushNotify.h"

/* ============================================================
 *  COMPATIBILITY SHIMS (v2.2 -> v3.3.6)
 * ============================================================ */
#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

/* ============================================================
 *  GLOBAL STATE (minimal shims + runtime)
 * ============================================================ */

// UI state
UIState uiState      = UI_HOME;
bool    uiNeedRedraw = true;

// UI edit buffers
String newSetpointValue;
String boostTimeEditValue;
String deadbandEditValue;
String clampMinEditValue;
String clampMaxEditValue;
String emberGuardianEditValue;
String flueLowEditValue;
String flueRecEditValue;
String tankLowEditValue;
String tankHighEditValue;

/* Forward declarations */
double exhaust_readF_cached();

/* Five-second low-pass, advanced only by fresh valid sensor samples. */
double smoothExhaustF(double rawF) {
    static unsigned long lastSampleMs = 0;
    const unsigned long sampleMs = sys.exhaustLastGoodMs;
    if (!isfinite(rawF) || !sys.exhaustSensorOK) {
        return sys.exhaustSmoothF;
    }

    if (!isfinite(sys.exhaustSmoothF)) {
        sys.exhaustSmoothF = rawF;
    } else if (sampleMs != lastSampleMs) {
        const float elapsedMs = static_cast<float>(sampleMs - lastSampleMs);
        const float alpha = elapsedMs / (5000.0f + elapsedMs);
        sys.exhaustSmoothF += alpha * (rawF - sys.exhaustSmoothF);
    }
    lastSampleMs = sampleMs;
    return sys.exhaustSmoothF;
}

/* ============================================================
 *  SETUP
 * ============================================================ */

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, HIGH);   // default CLOSED

    fan_dimmer_init();

    Serial.println();
    Serial.println("=== Boiler Assistant v3.4.0 Boot ===");
    watchdog_bootReport();

    i2cbus_init();

    // SystemData must be initialized before EEPROM populates it
    systemdata_init();

    // Load all EEPROM-backed settings into sys.*
    eeprom_init();
    datashare_init();

    // Sensors + logic
    sensors_init();
    env_logic_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    // Optional SD card: a missing card is reported, never fatal.
    datalogger_init();

    // Saved credentials retry offline; AP setup is for unprovisioned units.
    wifi_prov_init();

    if (!wifi_prov_isAPMode()) {
        wifiapi_init();
        mqtt_init();
    }

    // Hardware watchdog (5 s) fed from SysTick while the loop checks in
    // within 30 s; WDT.begin(16000) exceeded the UNO R4 maximum and failed.
    watchdog_begin();
}

/* ============================================================
 *  LOOP
 * ============================================================ */

void loop() {

    unsigned long loopStartMs = millis();
    watchdog_kick();
    watchdog_stage(WD_SENSORS);
    i2cbus_loop();
    sensors_loop();
    unsigned long now = millis();

    // 0) Keypad
    watchdog_stage(WD_KEYPAD);
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        double smoothedKeyExhaust = smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothedKeyExhaust, sys.fanFinal);
        uiNeedRedraw = true;
    }

    // 1) Sensors
    watchdog_stage(WD_PROBES);
    static unsigned long lastBME = 0;
    if (now - lastBME > 3000) {
        sensors_readBME280();
        env_logic_update(now);
        lastBME = now;
    }

    // Self-timed: one conversion every 500 ms, one probe read per pass.
    sensors_readWaterProbes();


    static unsigned long lastProbeScan = 0;
    if (now - lastProbeScan >= 60000UL) {
        sensors_rescanWaterProbes();
        lastProbeScan = now;
    }

    // 2) Burn engine - exhaust pipeline
    watchdog_stage(WD_BURN);
    double rawExh = exhaust_readF_cached();
    sys.exhaustRawF = rawExh;                    // live raw flue temp for Guardian
    double smoothedExh = smoothExhaustF(rawExh);
    sys.exhaustSmoothF = smoothedExh;            // preserve float precision for control

    int demand = burnengine_compute();
    sys.fanDemand = demand;

    // 3) Fan control (single source of truth)
    int fanPercent = fancontrol_apply(demand);

    fanPercent = fan_dimmer_setPercent(fanPercent);

    // 4) Update SystemData snapshot for UI / WiFi / MQTT

    sys.fanFinal = fanPercent;
    burnperformance_loop(millis());

    // SD logging (observation only).
    watchdog_stage(WD_SDLOG);
    datalogger_loop(millis());

    sys.uptimeMs = now;

    // 5) WiFi + MQTT (only when NOT in AP mode)
    unsigned long networkStartMs = millis();
    unsigned long ioStartedMs = millis();
    // Stop further clients in this pass after an overlong bridge operation.
    // Recovery is serviced separately even while regular I/O is quarantined.
    watchdog_stage(WD_LINK);
    bool ioReady = wifi_prov_prepareIo();
    wifi_prov_networkLoop();
    if (ioReady) wifi_prov_finishIo(ioStartedMs, "link monitor", WIFI_PASS_LIMIT_MS);
#if BOILER_AUTO_UPDATE_CHECKS_ENABLED
    if (!wifi_prov_isAPMode() && sys.wifiOK &&
        sys.controlMode == RUNMODE_AUTO_TANK &&
        sys.burnState == BURN_IDLE && sys.safetyState == SAFETY_OK &&
        wifi_prov_prepareIo()) {
        // This bounded OTA check may take 10s; only run when safe and do not
        // treat its expected timeout as an unresponsive runtime modem call.
        watchdog_stage(WD_OTA);
        ota_dailyCheckLoop(true);
    }
#endif
    if (wifi_prov_networkChangePending() ||
        (!wifi_prov_isAPMode() && sys.wifiOK && wifi_prov_prepareIo())) {
        ioStartedMs = millis();
        watchdog_stage(WD_HTTP);
        wifiapi_loop();
        wifi_prov_finishIo(ioStartedMs, "HTTP", WIFI_PASS_LIMIT_MS);
        // A stalled phone send may take up to WIFI_SEND_LIMIT_MS; keep the
        // following MQTT/push steps inside a fresh watchdog window.
        watchdog_kick();
    }
    if (!wifi_prov_isAPMode() && sys.wifiOK && wifi_prov_prepareIo()) {
        ioStartedMs = millis();
        watchdog_stage(WD_MQTT);
        mqtt_loop();
        wifi_prov_finishIo(ioStartedMs, "MQTT", WIFI_PASS_LIMIT_MS);
    }
    if (!wifi_prov_isAPMode()) {
        ioStartedMs = millis();
        ioReady = wifi_prov_prepareIo();
        watchdog_stage(WD_PUSH);
        pushnotify_loop();
        if (ioReady) wifi_prov_finishIo(ioStartedMs, "push", WIFI_PASS_LIMIT_MS);
    }
    sys.networkLastMs = millis() - networkStartMs;
    if (sys.networkLastMs > sys.networkMaxMs) sys.networkMaxMs = sys.networkLastMs;

    // 6) UI
    watchdog_stage(WD_SCREEN);
    ui_showScreen(uiState, sys.exhaustSmoothF, fanPercent);

    // 7) Provisioning AP handler
    watchdog_stage(WD_PROV);
    wifi_prov_loop();
    sys.loopLastMs = millis() - loopStartMs;
    if (sys.loopLastMs > sys.loopMaxMs) sys.loopMaxMs = sys.loopLastMs;
}
