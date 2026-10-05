/*
 * ============================================================
 *  Boiler Assistant - Fan Dimmer Module (v3.3.9 "Total Domination")
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
 *      - Zero-cross detection with half-cycle validation
 *      - Phase-fired PSM pulses timed from each zero-cross
 *      - Automatic fallback to analogWrite PWM when Z-C is absent
 *      - Non-blocking, timer/interrupt-driven output
 *
 *  Architectural Notes:
 *      - This module owns the physical fan output pin only.
 *      - Phase timer starts only with validated Z-C and nonzero demand.
 *      - Excessive Z-C edges mask the input IRQ for one second;
 *        main-loop servicing applies the existing PWM fallback.
 *      - Fan demand decisions live in FanControl; state logic in
 *        BurnEngine; both call fan_dimmer_setPercent().
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#include "FanDimmer.h"
#include "Pinout.h"
#include <FspTimer.h>
#include <FspLinkIrq.h>

static const float PHASE_TIMER_HZ = 20000.0f;
// 100us gate pulse: on an inductive motor load the triac needs enough
// time at sufficient instantaneous voltage to reach latching current
// before the gate pulse ends, or it snaps back off and the fan just hums.
static const uint32_t PSM_PULSE_WIDTH_US = 100UL;
// Never fire earlier than 1ms after zero-cross (~65V instantaneous on
// 120V mains). Firing closer to the cross cannot latch the triac.
static const uint32_t MIN_FIRE_DELAY_US = 1000UL;
static const uint32_t END_GUARD_US = 300UL;
// Valid half-cycle window tightened to real 60Hz mains (8333us) +/- ~4%.
// The old 7000-11000us band was wide enough that random chatter on a
// floating Z-C pin (modules with internal zero-cross, no Z-C wire) could
// falsely validate and lock the output into the 20kHz phase timer,
// starving the CPU and freezing the controller.
static const uint32_t ZC_MIN_HALF_CYCLE_US = 8000UL;
static const uint32_t ZC_MAX_HALF_CYCLE_US = 8700UL;
static const uint32_t ZC_TIMEOUT_US = 25000UL;
// Require this many CONSECUTIVE in-band half-cycles before trusting the
// signal, and drop out of phase mode if intervals fall out of band.
static const uint8_t ZC_REQUIRED_CONSECUTIVE = 8;

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
static int zeroCrossIrqIndex = -1;
static volatile bool zeroCrossSuppressed = false;
static volatile uint32_t zeroCrossSuppressedAtUs = 0;
static volatile uint32_t edgeWindowUs = 0;
static volatile uint8_t edgeWindowCount = 0;
static bool noiseLogged = false;

static uint32_t servicedZeroCrossSequence = 0;
static uint32_t fireAtUs = 0;
static uint32_t pulseStartedAtUs = 0;
static bool firePending = false;
static bool psmPulseHigh = false;

static void fan_zeroCrossIsr() {
    uint32_t nowUs = micros();
    if (nowUs - edgeWindowUs >= 10000UL) {
        edgeWindowUs = nowUs;
        edgeWindowCount = 0;
    }
    if (++edgeWindowCount > 32) {
        validZeroCrossIntervals = 0;
        zeroCrossSuppressed = true;
        zeroCrossSuppressedAtUs = nowUs;
        // Mask the already-resolved IRQ: no heap allocation or Serial in ISR.
        if (zeroCrossIrqIndex >= 0)
            NVIC_DisableIRQ(static_cast<IRQn_Type>(zeroCrossIrqIndex));
        return;
    }
    uint32_t previousUs = previousZeroCrossAtUs;

    if (previousUs != 0) {
        uint32_t intervalUs = nowUs - previousUs;
        if (intervalUs >= ZC_MIN_HALF_CYCLE_US &&
            intervalUs <= ZC_MAX_HALF_CYCLE_US) {
            measuredHalfCycleUs = intervalUs;
            // Consecutive in-band intervals build confidence in a real signal.
            if (validZeroCrossIntervals < ZC_REQUIRED_CONSECUTIVE) validZeroCrossIntervals++;
        } else {
            // Any out-of-band interval resets confidence to zero: noise on a
            // floating pin can no longer accumulate scattered valid windows.
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
            // voltage is too low to latch the triac on a motor load -
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
        !phaseTimer.open()) {
        phaseTimer.end();
        Serial.println("Fan: phase timer unavailable; using PWM fallback");
        return false;
    }

    phaseTimerReady = true;
    // INPUT_PULLUP: if the Z-C wire is loose or the module output is
    // open-collector, a floating pin would chatter and storm the ISR.
    pinMode(PIN_FAN_ZERO_CROSS, INPUT_PULLUP);
    noInterrupts();
    attachInterrupt(digitalPinToInterrupt(PIN_FAN_ZERO_CROSS),
                    fan_zeroCrossIsr,
                    RISING);
    zeroCrossIrqIndex = getIrqIndexFromPin(PIN_FAN_ZERO_CROSS);
    interrupts();
    if (zeroCrossIrqIndex < 0) {
        detachInterrupt(digitalPinToInterrupt(PIN_FAN_ZERO_CROSS));
        phaseTimer.end();
        phaseTimerReady = false;
        Serial.println("Fan: zero-cross IRQ unavailable; using PWM fallback");
        return false;
    }
    return true;
}

void fan_dimmer_setPercent(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (zeroCrossSuppressed) {
        if (!noiseLogged) {
            noiseLogged = true;
            Serial.println("Fan: zero-cross interrupt noise; PWM fallback, retry in 1s");
        }
        if (micros() - zeroCrossSuppressedAtUs >= 1000000UL) {
            noInterrupts();
            previousZeroCrossAtUs = zeroCrossAtUs = 0;
            validZeroCrossIntervals = 0;
            edgeWindowCount = 0;
            edgeWindowUs = micros();
            zeroCrossSuppressed = false;
            R_BSP_IrqClearPending(static_cast<IRQn_Type>(zeroCrossIrqIndex));
            NVIC_ClearPendingIRQ(static_cast<IRQn_Type>(zeroCrossIrqIndex));
            NVIC_EnableIRQ(static_cast<IRQn_Type>(zeroCrossIrqIndex));
            interrupts();
            noiseLogged = false;
        }
    }

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
                     !zeroCrossSuppressed && percent > 0 &&
                     validIntervals >= ZC_REQUIRED_CONSECUTIVE &&
                     lastHalfCycleUs >= ZC_MIN_HALF_CYCLE_US &&
                     lastHalfCycleUs <= ZC_MAX_HALF_CYCLE_US &&
                     lastCrossUs != 0 &&
                     nowUs - lastCrossUs <= ZC_TIMEOUT_US;

    // If the signal degrades while in phase mode (wire unplugged, or it was
    // never a real Z-C source), drop back to plain PWM instead of staying in
    // the starved 20kHz phase state.
    if (outputUsesPhaseControl && !zcIsValid) {
        phaseTimer.stop();
        noInterrupts();
        phaseControlEnabled = false;
        interrupts();
        outputUsesPhaseControl = false;
        pinMode(PIN_FAN_PWM, OUTPUT);
        analogWrite(PIN_FAN_PWM, (long)percent * 255L / 100L);
        return;
    }

    if (zcIsValid) {
        if (!outputUsesPhaseControl) {
            phaseTimer.stop();
            pinMode(PIN_FAN_PWM, OUTPUT);
            digitalWrite(PIN_FAN_PWM, LOW);
            noInterrupts();
            phaseControlEnabled = true;
            interrupts();
            outputUsesPhaseControl = true;
            if (!phaseTimer.start()) {
                phaseControlEnabled = false;
                outputUsesPhaseControl = false;
                phaseTimerReady = false;
                Serial.println("Fan: phase timer start failed; using PWM fallback");
            }
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