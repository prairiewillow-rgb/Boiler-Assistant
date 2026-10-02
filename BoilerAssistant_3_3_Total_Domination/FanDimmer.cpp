/*
 * ============================================================
 *  Boiler Assistant â€“ Fan Dimmer Module (v3.3.4 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanDimmer.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Fan output driver for the PWM/dimmer hardware. When a valid
 *    zero-cross signal is present on D0, the fan output on D5 uses
 *    zero-cross-synchronized PSM phase control. Without a valid
 *    zero-cross signal, output automatically falls back to legacy
 *    PWM so existing installations keep working unchanged.
 *
 *    Responsibilities:
 *      â€¢ Zero-cross detection with half-cycle validation
 *      â€¢ Phase-fired PSM pulses timed from each zero-cross
 *      â€¢ Automatic fallback to analogWrite PWM when Z-C is absent
 *      â€¢ Non-blocking, timer/interrupt-driven output
 *
 *  Architectural Notes:
 *      - This module owns the physical fan output pin only.
 *      - Fan demand decisions live in FanControl; state logic in
 *        BurnEngine; both call fan_dimmer_setPercent().
 *
 *  Version:
 *      Boiler Assistant v3.3.4 "Total Domination"
 * ============================================================
 */

#include "FanDimmer.h"
#include "Pinout.h"
#include <FspTimer.h>

static const float PHASE_TIMER_HZ = 20000.0f;
// 100us gate pulse: on an inductive motor load the triac needs enough
// time at sufficient instantaneous voltage to reach latching current
// before the gate pulse ends, or it snaps back off and the fan just hums.
static const uint32_t PSM_PULSE_WIDTH_US = 100UL;
// Never fire earlier than 1ms after zero-cross (~65V instantaneous on
// 120V mains). Firing closer to the cross cannot latch the triac.
static const uint32_t MIN_FIRE_DELAY_US = 1000UL;
static const uint32_t END_GUARD_US = 300UL;
static const uint32_t ZC_MIN_HALF_CYCLE_US = 7000UL;
static const uint32_t ZC_MAX_HALF_CYCLE_US = 11000UL;
static const uint32_t ZC_TIMEOUT_US = 25000UL;

static FspTimer phaseTimer;
static bool phaseTimerReady = false;
static bool outputUsesPhaseControl = false;
static volatile bool phaseControlEnabled = false;
static volatile uint8_t requestedPercent = 0;

static volatile uint32_t zeroCrossSequence = 0;
static volatile uint32_t zeroCrossAtUs = 0;
static volatile uint32_t previousZeroCrossAtUs = 0;
static volatile uint32_t measuredHalfCycleUs = 8333UL;
static volatile uint8_t validZeroCrossIntervals = 0;

static uint32_t servicedZeroCrossSequence = 0;
static uint32_t fireAtUs = 0;
static uint32_t pulseStartedAtUs = 0;
static bool firePending = false;
static bool psmPulseHigh = false;

static void fan_zeroCrossIsr() {
    uint32_t nowUs = micros();
    uint32_t previousUs = previousZeroCrossAtUs;

    if (previousUs != 0) {
        uint32_t intervalUs = nowUs - previousUs;
        if (intervalUs >= ZC_MIN_HALF_CYCLE_US &&
            intervalUs <= ZC_MAX_HALF_CYCLE_US) {
            measuredHalfCycleUs = intervalUs;
            if (validZeroCrossIntervals < 3) validZeroCrossIntervals++;
        } else {
            validZeroCrossIntervals = 0;
        }
    }

    previousZeroCrossAtUs = nowUs;
    zeroCrossAtUs = nowUs;
    zeroCrossSequence++;
}

static void fan_phaseTimerCallback(timer_callback_args_t*) {
    if (!phaseControlEnabled) return;

    uint32_t latestCrossUs = zeroCrossAtUs;
    uint32_t nowUs = micros();
    if (latestCrossUs == 0 || nowUs - latestCrossUs > ZC_TIMEOUT_US) {
        firePending = false;
        psmPulseHigh = false;
        digitalWrite(PIN_FAN_PWM, requestedPercent > 0 ? HIGH : LOW);
        return;
    }

    uint32_t sequence = zeroCrossSequence;
    if (sequence != servicedZeroCrossSequence) {
        servicedZeroCrossSequence = sequence;
        psmPulseHigh = false;
        digitalWrite(PIN_FAN_PWM, LOW);

        uint8_t percent = requestedPercent;
        if (percent >= 100) {
            // Full power: hold the gate HIGH for the entire half-cycle.
            // 100% would otherwise fire 1ms after the zero-cross where
            // voltage is too low to latch the triac on a motor load â€”
            // the fan hums but never spins. A solid gate makes 100%
            // behave like a hard-wired connection (matches the ZC
            // timeout fallback above).
            digitalWrite(PIN_FAN_PWM, HIGH);
            firePending = false;
        } else if (percent > 0) {
            uint32_t halfCycleUs = measuredHalfCycleUs;
            uint32_t maxDelayUs = halfCycleUs > END_GUARD_US
                                      ? halfCycleUs - END_GUARD_US
                                      : MIN_FIRE_DELAY_US;
            uint32_t delayRangeUs = maxDelayUs - MIN_FIRE_DELAY_US;
            uint32_t delayUs = MIN_FIRE_DELAY_US +
                (delayRangeUs * (uint32_t)(100 - percent)) / 100UL;
            fireAtUs = zeroCrossAtUs + delayUs;
            firePending = true;
        } else {
            firePending = false;
        }
    }

    nowUs = micros();
    if (firePending && (int32_t)(nowUs - fireAtUs) >= 0) {
        digitalWrite(PIN_FAN_PWM, HIGH);
        pulseStartedAtUs = nowUs;
        psmPulseHigh = true;
        firePending = false;
    }

    if (psmPulseHigh && nowUs - pulseStartedAtUs >= PSM_PULSE_WIDTH_US) {
        digitalWrite(PIN_FAN_PWM, LOW);
        psmPulseHigh = false;
    }
}

bool fan_dimmer_init() {
    pinMode(PIN_FAN_PWM, OUTPUT);
    analogWrite(PIN_FAN_PWM, 0);

    uint8_t timerType = GPT_TIMER;
    int8_t timerChannel = FspTimer::get_available_timer(timerType);
    if (timerChannel < 0 ||
        !phaseTimer.begin(TIMER_MODE_PERIODIC,
                          timerType,
                          (uint8_t)timerChannel,
                          PHASE_TIMER_HZ,
                          50.0f,
                          fan_phaseTimerCallback,
                          nullptr) ||
        !phaseTimer.setup_overflow_irq() ||
        !phaseTimer.open() ||
        !phaseTimer.start()) {
        phaseTimer.end();
        return false;
    }

    phaseTimerReady = true;
    // INPUT_PULLUP: if the Z-C wire is loose or the module output is
    // open-collector, a floating pin would chatter and storm the ISR.
    pinMode(PIN_FAN_ZERO_CROSS, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FAN_ZERO_CROSS),
                    fan_zeroCrossIsr,
                    RISING);
    return true;
}

void fan_dimmer_setPercent(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    uint32_t lastCrossUs;
    uint32_t lastHalfCycleUs;
    uint8_t validIntervals;
    noInterrupts();
    requestedPercent = (uint8_t)percent;
    lastCrossUs = zeroCrossAtUs;
    lastHalfCycleUs = measuredHalfCycleUs;
    validIntervals = validZeroCrossIntervals;
    interrupts();

    uint32_t nowUs = micros();
    bool zcIsValid = phaseTimerReady &&
                     validIntervals >= 3 &&
                     lastHalfCycleUs >= ZC_MIN_HALF_CYCLE_US &&
                     lastHalfCycleUs <= ZC_MAX_HALF_CYCLE_US &&
                     lastCrossUs != 0 &&
                     nowUs - lastCrossUs <= ZC_TIMEOUT_US;

    if (zcIsValid) {
        if (!outputUsesPhaseControl) {
            phaseTimer.stop();
            pinMode(PIN_FAN_PWM, OUTPUT);
            digitalWrite(PIN_FAN_PWM, LOW);
            noInterrupts();
            phaseControlEnabled = true;
            interrupts();
            outputUsesPhaseControl = true;
            phaseTimer.start();
        }
    }

    if (outputUsesPhaseControl) {
        noInterrupts();
        requestedPercent = (uint8_t)percent;
        interrupts();
        return;
    }

    analogWrite(PIN_FAN_PWM, (long)percent * 255L / 100L);
}

bool fan_dimmer_phaseModeActive() {
    return outputUsesPhaseControl;
}