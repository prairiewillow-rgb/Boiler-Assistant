/*
 * ============================================================
 *  Boiler Assistant – Burn Engine API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: BurnEngine.h
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
 *      v2.3‑Environmental Notes:
 *          - BurnEngine.cpp consumes EnvironmentalLogic v2.3
 *            (seasonal setpoint, clamp, ramp profile, fan bias)
 *          - API remains unchanged for backward compatibility
 *          - All seasonal logic is injected internally via
 *            env_logic_update() inside burnengine_compute()
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
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
