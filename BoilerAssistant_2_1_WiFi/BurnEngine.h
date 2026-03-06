/*
 * ============================================================
 *  Boiler Assistant – Burn Engine API (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: BurnEngine.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the Burn Engine module.
 *      Provides the procedural API used by:
 *          - main.cpp (real‑time loop)
 *          - UI system (state display + line 4 messaging)
 *          - WiFiAPI (telemetry export)
 *
 *      This header exposes:
 *          • burnengine_init()       – reset state + hardware
 *          • burnengine_startBoost() – force BOOST on power‑up
 *          • burnengine_compute()    – main state machine step
 *
 *  Notes:
 *      - All combustion logic lives in BurnEngine.cpp
 *      - No WiFi, UI, or blocking code is allowed here
 *      - State machine includes BOOST, RAMP, HOLD, SAFETY,
 *        and the v2.1 Ember Guardian lockout mode
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef BURN_ENGINE_H
#define BURN_ENGINE_H

#include <Arduino.h>
#include "SystemState.h"

// Procedural burn engine API (matches BurnEngine.cpp)
void burnengine_init();
void burnengine_startBoost();
int  burnengine_compute();

#endif
