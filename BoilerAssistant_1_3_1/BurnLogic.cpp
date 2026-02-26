/*
 * ============================================================
 *  Boiler Assistant – Burn Logic Module (v1.3.1)
 *  ------------------------------------------------------------
 *  File: BurnLogic.cpp
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Computes the *raw* fan percentage (0–100) using:
 *
 *          • Adaptive combustion mode
 *          • PID mode (three profiles: BELOW / NORMAL / ABOVE)
 *
 *      BurnLogic does NOT:
 *          • Apply clamp limits
 *          • Apply deadzone rules
 *          • Apply BOOST override
 *          • Output PWM
 *          • Control the damper
 *
 *      These behaviors are handled in FanControl.cpp.
 *
 *      Adaptive tracking functions are implemented in Adaptive.cpp.
 *
 * ============================================================
 */

#include "BurnLogic.h"
#include "SystemState.h"
#include "Adaptive.h"
#include <Arduino.h>


// ============================================================
//  SECTION: Internal PID State
// ============================================================

/**
 * pidIntegral   - Accumulated integral term
 * pidLastError  - Previous loop error
 * pidLastTime   - Timestamp of last PID update
 */
static double pidIntegral = 0;
static double pidLastError = 0;
static unsigned long pidLastTime = 0;


// ============================================================
//  burnlogic_init()
// ============================================================

void burnlogic_init() {
    pidIntegral = 0;
    pidLastError = 0;
    pidLastTime = millis();

    adaptive_reset();   // correctly calls Adaptive.cpp
}


// ============================================================
//  pid_compute()
// ============================================================

double pid_compute(double errorF) {

    unsigned long now = millis();
    double dt = (now - pidLastTime) / 1000.0;
    if (dt <= 0) dt = 0.001;

    pidLastTime = now;

    // Select PID profile based on error sign
    float kp, ki, kd;

    if (errorF < -10) {
        kp = pidBelowKp;
        ki = pidBelowKi;
        kd = pidBelowKd;
    }
    else if (errorF > 10) {
        kp = pidAboveKp;
        ki = pidAboveKi;
        kd = pidAboveKd;
    }
    else {
        kp = pidNormKp;
        ki = pidNormKi;
        kd = pidNormKd;
    }

    // PID terms
    pidIntegral += errorF * dt;
    double derivative = (errorF - pidLastError) / dt;
    pidLastError = errorF;

    double output = (kp * errorF) + (ki * pidIntegral) + (kd * derivative);

    // Clamp to 0–100%
    if (output < 0) output = 0;
    if (output > 100) output = 100;

    return output;
}


// ============================================================
//  burnlogic_compute()
// ============================================================

int burnlogic_compute(double exhaustF) {

    // --------------------------------------------------------
    // Compute error
    // --------------------------------------------------------
    double errorF = exhaustSetpoint - exhaustF;

    // --------------------------------------------------------
    // Update adaptive tracking (implemented in Adaptive.cpp)
    // --------------------------------------------------------
    adaptive_update(exhaustF);

    // --------------------------------------------------------
    // Mode Selection
    // --------------------------------------------------------
    if (burnLogicMode == 0) {
        // ADAPTIVE MODE
        double base = errorF * 0.8;     // proportional
        double slopeAdj = adaptiveSlope * lastRate;

        double out = base + slopeAdj;

        // Clamp to 0–100%
        if (out < 0) out = 0;
        if (out > 100) out = 100;

        return (int)(out + 0.5);
    }
    else {
        // PID MODE
        return (int)(pid_compute(errorF) + 0.5);
    }
}
