/*
 * Boiler Assistant - Burn Performance (v3.4.0 "Total Domination")
 * Bounded active-period summaries; no RAM graph-history buffer.
 * Completed periods are also written to the optional SD card and the most
 * recent ones are reloaded from it after a restart.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#ifndef BURN_PERFORMANCE_H
#define BURN_PERFORMANCE_H
#include <Arduino.h>

// One completed active period (BOOST/RAMP/HOLD entry until IDLE or EMBER GUARD).
// Temperatures in F; NaN means missing. Averages use 1-second samples.
struct BurnRecord {
    uint32_t durationSec;
    float startF, endF;
    int16_t low, high, minimum, maximum;
    uint8_t mode, fanMode, endState;
    bool changed, complete, probesValid;
    float avgFan, avgExhaustF, peakExhaustF, avgOutdoorF;
    uint32_t boostSec, rampSec, holdSec;
};

void burnperformance_loop(unsigned long now);
void burnperformance_calibrationChanged();
// True while no period has completed since boot (restore is still allowed).
bool burnperformance_canRestore();
// Records oldest first; ageSec = seconds since each period ended.
void burnperformance_restore(const BurnRecord* records, const uint32_t* ageSec, uint8_t count);
#endif
