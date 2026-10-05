/*
 * ============================================================
 *  Boiler Assistant - Main Firmware (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BoilerAssistant_3_3_9_Total_Domination.ino
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core deterministic firmware for the Boiler Assistant controller.
 *    Version 3.3.9 continues the Total Domination Architecture (TDA):
 *      - SystemData as the single source of truth
 *      - Timed/event-driven main loop with bounded sensor transactions
 *      - Unified keypad-driven UI with numeric selection everywhere
 *      - Fully transparent operator-facing logic and documentation
 *
 *    Subsystems coordinated by this module:
 *      - Environmental sensing (BME280)
 *      - Temperature sensing (DS18B20 + MAX31855 exhaust)
 *      - Burn engine logic (demand, ramp, hold, safety)
 *      - Fan control (clamps, ramping, startup kick, damper pre-delay)
 *      - Fan dimmer output (zero-cross PSM phase control, PWM fallback)
 *      - UI rendering (LCD) + keypad-driven operator interface
 *      - EEPROM-backed configuration and seasonal profiles
 *      - WiFi provisioning (saved STA retries, AP without credentials)
 *      - Incremental WiFi API + MQTT telemetry (synchronous modem I/O)
 *      - Push notifications (ntfy)
 *
 *  v3.3 Additions:
 *      - Adaptive fan curve with persistent learning
 *      - Fan-off deadband with one-PWM variable-speed control
 *      - Exhaust-probe fallback at max clamp fan output
 *      - Mode-aware tank-probe safety handling
 *      - Address-stable water probes with periodic rescanning
 *      - Named monitor-only probes for MQTT and dashboard telemetry
 *      - Live diagnostics through the web dashboard and MQTT
 *      - Authenticated remote alarm reset; run mode remains local-only
 *
 *  v3.3.1 Additions:
 *      - Damper opens before fan start (pre-fan delay)
 *      - 2-second startup kick before returning to requested speed
 *      - Zero-cross phase control with automatic PWM fallback
 *
 *  v3.3.9 Additions:
 *      - PCF8574-compatible 100 kHz shared I2C bus
 *      - Explicit bounded bus recovery on UNO R4 (timeout alone
 *        does not reset the Renesas I2C peripheral)
 *      - Checked LCD writes, status readback, reinitialization and
 *        cache invalidation without leaving the operator's current menu
 *      - Checked/rate-limited keypad scans and release-to-resume recovery
 *      - Visible-only, serialized dashboard polling every 5 seconds;
 *        settings/history refresh at most once a minute
 *      - HTTP requests/responses processed in bounded chunks per loop
 *      - MQTT 15-second keepalive (milliseconds API), short connection
 *        timeout, and incremental Home Assistant discovery publishing
 *      - Queued push delivery with incremental response reads and status
 *      - /api/state exposes loop/network last and maximum duration in ms
 *      - Saved WiFi credentials stay in STA mode during outages, with
 *        30/60/120s retry backoff and 5s stable-link socket restart
 *      - Runtime AT waits shortened to 3s; slow operations quarantine
 *        networking, with quiet/reboot waits and staged socket cleanup
 *      - Automatic bridge reset needs compatible ESP32-S3 firmware
 *        (0.5.0+ baseline); an unacknowledged reset stays offline
 *      - Network Info/telemetry use cached IP/RSSI; scheduled self-clean
 *        uses cached UTC (valid for 24h), never WiFi calls in control
 *      - BME280 calibration waits are asynchronous and deadline-checked
 *      - BME identity/configuration/sample verification; only valid
 *        measurements mark it healthy. Missing BME retries every 30s
 *        without resetting the shared LCD/keypad bus.
 *      - Fan phase timer runs only with valid Z-C and nonzero demand;
 *        noisy Z-C IRQ is masked for 1s before retrying PWM fallback
 *      - Runtime OneWire rescans have address/time budgets
 *      - Rebuild from source: existing build artifacts predate these fixes
 *
 *  Architectural Notes:
 *      - I2C transactions are bounded; recovery retries are rate-limited
 *      - All subsystems operate on timed or event-driven cadence
 *      - WiFiS3 modem calls/connect remain synchronous; requested transport
 *        timeouts and chunking reduce stalls but do not guarantee latency
 *      - A top-level modem operation may contain multiple 3s AT waits;
 *        this is not hard real-time. Interactive OTA retains its own waits.
 *      - UI executes last to ensure stable system state before rendering
 *      - Pinout.h and SystemState.h are the authoritative hardware/state contracts
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WDT.h>

#include "SystemState.h"          // MUST be first project header
#include "EnvironmentalLogic.h"   // MUST be second
#include "SystemData.h"           // MUST be after both
#include "UI.h"

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
String envSeasonEditValue;
String envSetpointEditValue;
String envLockoutEditValue;

/* Forward declarations */
double exhaust_readF_cached();

/* Local implementation of exhaust smoothing */
double smoothExhaustF(double rawF) {
    if (isnan(rawF)) {
        return sys.exhaustSmoothF;
    }

    if (isnan(sys.exhaustSmoothF)) {
        sys.exhaustSmoothF = rawF;
    } else {
        sys.exhaustSmoothF = sys.exhaustSmoothF * 0.8 + rawF * 0.2;
    }
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
    Serial.println("=== Boiler Assistant v3.3.9 Boot ===");

    i2cbus_init();

    // SystemData must be initialized before EEPROM populates it
    systemdata_init();

    // Load all EEPROM-backed settings into sys.*
    eeprom_init();

    // Sensors + logic
    sensors_init();
    env_logic_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    // Saved credentials retry offline; AP setup is for unprovisioned units.
    wifi_prov_init();

    if (!wifi_prov_isAPMode()) {
        wifiapi_init();
        mqtt_init();
    }

    // Retain the existing watchdog interval; modem commands remain synchronous.
    WDT.begin(16000);
}

/* ============================================================
 *  LOOP
 * ============================================================ */

void loop() {

    unsigned long loopStartMs = millis();
    WDT.refresh();
    i2cbus_loop();
    sensors_loop();
    unsigned long now = millis();

    // 0) Keypad
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        double smoothedKeyExhaust = smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothedKeyExhaust, sys.fanFinal);
        uiNeedRedraw = true;
    }

    // 1) Sensors
    static unsigned long lastBME = 0;
    if (now - lastBME > 3000) {
        sensors_readBME280();
        env_logic_update(now);
        lastBME = now;
    }

    static unsigned long lastWaterRead = 0;
    if (now - lastWaterRead > 500) {
        sensors_readWaterProbes();
        lastWaterRead = now;
    }

    systemdata_recordWaterHistory(now);

    static unsigned long lastProbeScan = 0;
    if (now - lastProbeScan >= 60000UL) {
        sensors_rescanWaterProbes();
        lastProbeScan = now;
    }

    // 2) Burn engine - exhaust pipeline
    double rawExh = exhaust_readF_cached();
    sys.exhaustRawF = rawExh;                    // live raw flue temp for Guardian
    double smoothedExh = smoothExhaustF(rawExh);
    sys.exhaustSmoothF = smoothedExh;            // preserve float precision for control

    int demand = burnengine_compute();
    sys.fanDemand = demand;

    // 3) Fan control (single source of truth)
    int fanPercent = fancontrol_apply(demand);

    fan_dimmer_setPercent(fanPercent);

    // 4) Update SystemData snapshot for UI / WiFi / MQTT

    sys.fanFinal = fanPercent;

    sys.uptimeMs = now;

    // 5) WiFi + MQTT (only when NOT in AP mode)
    unsigned long networkStartMs = millis();
    unsigned long ioStartedMs = millis();
    // Stop further clients in this pass after an overlong bridge operation.
    // Recovery is serviced separately even while regular I/O is quarantined.
    bool ioReady = wifi_prov_prepareIo();
    wifi_prov_networkLoop();
    if (ioReady) wifi_prov_finishIo(ioStartedMs, "link monitor");
    if (!wifi_prov_isAPMode() && sys.wifiOK && wifi_prov_prepareIo()) {
        ioStartedMs = millis();
        wifiapi_loop();
        wifi_prov_finishIo(ioStartedMs, "HTTP");
    }
    if (!wifi_prov_isAPMode() && sys.wifiOK && wifi_prov_prepareIo()) {
        ioStartedMs = millis();
        mqtt_loop();
        wifi_prov_finishIo(ioStartedMs, "MQTT");
    }
    if (!wifi_prov_isAPMode()) {
        ioStartedMs = millis();
        ioReady = wifi_prov_prepareIo();
        pushnotify_loop();
        if (ioReady) wifi_prov_finishIo(ioStartedMs, "push");
    }
    sys.networkLastMs = millis() - networkStartMs;
    if (sys.networkLastMs > sys.networkMaxMs) sys.networkMaxMs = sys.networkLastMs;

    // 6) UI
    ui_showScreen(uiState, sys.exhaustSmoothF, fanPercent);

    // 7) Provisioning AP handler
    wifi_prov_loop();
    sys.loopLastMs = millis() - loopStartMs;
    if (sys.loopLastMs > sys.loopMaxMs) sys.loopMaxMs = sys.loopLastMs;
}
