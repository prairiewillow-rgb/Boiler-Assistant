/*
 * Boiler Assistant - Burn Performance (v3.4.0 "Total Domination")
 * Active-period summaries only; graph history lives on the SD card.
 * Recovery means observed water temperature change, not combustion efficiency.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#include "BurnPerformance.h"
#include "DebugLog.h"
#include "SystemData.h"
#include "Sensors.h"
#include "DataLogger.h"
#include <math.h>
#include <limits.h>
#include <string.h>

namespace {
constexpr uint8_t BURN_COUNT = 8;
constexpr unsigned long MAX_OBSERVATION_GAP_MS = 5000UL;
constexpr int16_t MISSING = INT16_MIN;
struct Settings {
    int16_t low, high, minimum, maximum;
    uint8_t mode, fanMode;
};
struct Burn {
    unsigned long startMs, endMs;
    int16_t startF, endF;
    Settings settings;
    uint8_t endState;
    bool changed, complete, probesValid, restored;
};
// Per-period summary sampled once a second; only kept for the active period.
struct Stats {
    unsigned long lastMs;
    uint32_t samples, fanSum, exhaustSamples, outdoorSamples;
    int32_t exhaustSum, outdoorSum10;
    int16_t exhaustPeak;
    uint32_t stateSeconds[3];  // BOOST, RAMP, HOLD
};
Burn burns[BURN_COUNT] = {};
Burn current = {};
Stats stats = {};
uint8_t burnNext = 0, burnCount = 0;
bool observing = false, wasActive = false, recording = false;
bool anyCompleted = false;
unsigned long lastObservedMs = 0;
char controlProbeId[17] = {};

bool active() {
    return sys.burnState == BURN_BOOST || sys.burnState == BURN_RAMP ||
           sys.burnState == BURN_HOLD;
}
Settings settings() {
    return {sys.tankLowSetpointF, sys.tankHighSetpointF,
            static_cast<int16_t>(sys.clampMinPercent),
            static_cast<int16_t>(sys.clampMaxPercent),
            static_cast<uint8_t>(sys.controlMode), sys.deadzoneFanMode};
}
bool sameSettings(const Settings& a, const Settings& b) {
    return a.low == b.low && a.high == b.high && a.minimum == b.minimum &&
           a.maximum == b.maximum && a.mode == b.mode && a.fanMode == b.fanMode;
}
int16_t scaled(float value) {
    if (!isfinite(value) || value <= -3276 || value >= 3276) return MISSING;
    return static_cast<int16_t>(lroundf(value * 10));
}
int16_t tankTemperature(unsigned long now) {
    uint8_t i = sys.probeRoleMap[PROBE_TANK];
    if (i >= sys.waterProbeCount || sys.waterTempLastGoodMs[i] == 0 ||
        now - sys.waterTempLastGoodMs[i] > 3000UL) return MISSING;
    return scaled(sys.waterTempF[i]);
}
float unscaled(int16_t value) {
    return value == MISSING ? NAN : value / 10.0f;
}
void sampleStats(unsigned long now) {
    if (now - stats.lastMs < 1000UL) return;
    stats.lastMs = now;
    ++stats.samples;
    stats.fanSum += static_cast<uint32_t>(constrain(sys.fanFinal, 0, 100));
    if (sys.exhaustSensorOK && now - sys.exhaustLastGoodMs <= 1000UL &&
        isfinite(sys.exhaustSmoothF)) {
        int16_t f = static_cast<int16_t>(constrain(lroundf(sys.exhaustSmoothF), -400L, 3000L));
        stats.exhaustSum += f;
        if (!stats.exhaustSamples || f > stats.exhaustPeak) stats.exhaustPeak = f;
        ++stats.exhaustSamples;
    }
    if (sys.envSensorOK && isfinite(sys.envTempF)) {
        stats.outdoorSum10 += lroundf(sys.envTempF * 10.0f);
        ++stats.outdoorSamples;
    }
    if (sys.burnState == BURN_BOOST) ++stats.stateSeconds[0];
    else if (sys.burnState == BURN_RAMP) ++stats.stateSeconds[1];
    else if (sys.burnState == BURN_HOLD) ++stats.stateSeconds[2];
}
BurnRecord makeRecord(const Burn& b) {
    BurnRecord r = {};
    r.durationSec = (b.endMs - b.startMs) / 1000UL;
    r.startF = unscaled(b.startF);
    r.endF = unscaled(b.endF);
    r.low = b.settings.low; r.high = b.settings.high;
    r.minimum = b.settings.minimum; r.maximum = b.settings.maximum;
    r.mode = b.settings.mode; r.fanMode = b.settings.fanMode;
    r.endState = b.endState;
    r.changed = b.changed; r.complete = b.complete; r.probesValid = b.probesValid;
    r.avgFan = stats.samples ? static_cast<float>(stats.fanSum) / stats.samples : NAN;
    r.avgExhaustF = stats.exhaustSamples ?
                    static_cast<float>(stats.exhaustSum) / stats.exhaustSamples : NAN;
    r.peakExhaustF = stats.exhaustSamples ? stats.exhaustPeak : NAN;
    r.avgOutdoorF = stats.outdoorSamples ?
                    stats.outdoorSum10 / 10.0f / stats.outdoorSamples : NAN;
    r.boostSec = stats.stateSeconds[0];
    r.rampSec = stats.stateSeconds[1];
    r.holdSec = stats.stateSeconds[2];
    return r;
}
}

void burnperformance_calibrationChanged() {
    if (recording) current.changed = true;
}

void burnperformance_loop(unsigned long now) {
    bool isActive = active();
    Settings currentSettings = settings();
    int16_t tank = tankTemperature(now);
    char probeId[17] = {};
    sensors_waterProbeId(sys.probeRoleMap[PROBE_TANK], probeId);
    bool gap = observing && now - lastObservedMs > MAX_OBSERVATION_GAP_MS;
    if (isActive && (!observing || !wasActive)) {
        current = {};
        current.startMs = now;
        current.startF = tank;
        current.settings = currentSettings;
        current.complete = observing && !gap;
        current.probesValid = tank != MISSING;
        strcpy(controlProbeId, probeId);
        recording = true;
        stats = {};
        stats.lastMs = now - 1000UL;
    }
    if (recording) {
        sampleStats(now);
        current.changed = current.changed || !sameSettings(current.settings, currentSettings);
        current.complete = current.complete && !gap;
        current.probesValid = current.probesValid && tank != MISSING &&
                              strcmp(controlProbeId, probeId) == 0;
        if (!isActive) {
            current.endMs = now;
            current.endF = tank;
            current.endState = static_cast<uint8_t>(sys.burnState);
            burns[burnNext] = current;
            burnNext = (burnNext + 1) % BURN_COUNT;
            if (burnCount < BURN_COUNT) ++burnCount;
            recording = false;
            anyCompleted = true;
            BurnRecord record = makeRecord(current);
            datalogger_recordBurn(record);
        }
    }
    observing = true;
    wasActive = isActive;
    lastObservedMs = now;
}

bool burnperformance_canRestore() {
    return !anyCompleted;
}

void burnperformance_restore(const BurnRecord* records, const uint32_t* ageSec, uint8_t count) {
    if (anyCompleted) return;
    unsigned long now = millis();
    burnNext = burnCount = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const BurnRecord& r = records[i];
        Burn b = {};
        b.endMs = now - ageSec[i] * 1000UL;
        b.startMs = b.endMs - r.durationSec * 1000UL;
        b.startF = scaled(r.startF);
        b.endF = scaled(r.endF);
        b.settings = {r.low, r.high, r.minimum, r.maximum, r.mode, r.fanMode};
        b.endState = r.endState;
        b.changed = r.changed;
        b.complete = r.complete;
        b.probesValid = r.probesValid;
        b.restored = true;
        burns[burnNext] = b;
        burnNext = (burnNext + 1) % BURN_COUNT;
        if (burnCount < BURN_COUNT) ++burnCount;
    }
}
