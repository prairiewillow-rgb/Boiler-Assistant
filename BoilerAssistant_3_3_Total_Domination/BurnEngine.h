/*
 * Boiler Assistant - BurnEngine
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef BURNENGINE_H
#define BURNENGINE_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Initialize burn engine state
void burnengine_init();

// Force a BOOST start (used by UI or AUTO TANK logic)
void burnengine_startBoost();

// Clear the pending sensor-fault confirmation timers after operator reset
void burnengine_resetSensorFault();
void burnengine_resetAlarms();

// Current learned multiplier used by adaptive fan control
float burnengine_getAdaptiveSlope();
bool burnengine_guardianPaused();
bool burnengine_getAdaptiveLearning();
void burnengine_setAdaptiveLearning(bool enabled);
void burnengine_resetAdaptiveLearning();
void burnengine_getAdaptiveDiagnostics(char* out, size_t size);

// Main compute function (dispatcher)
int burnengine_compute();

#endif

