/*
 * ============================================================
 *  Boiler Assistant – Fan Control API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: FanControl.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the Fan Control module.
 *      Provides the procedural API used by:
 *          • BurnEngine (raw demand input)
 *          • main.cpp (real‑time loop)
 *          • UI + WiFiAPI (telemetry export)
 *
 *      Responsibilities:
 *          - Apply smoothing to raw fan demand
 *          - Enforce clampMinPercent / clampMaxPercent
 *          - Implement hysteresis mode (fan can turn OFF)
 *          - Implement clamp mode (fan never turns OFF)
 *          - Apply BOOST and SAFETY overrides
 *          - Reset smoothing on state transitions
 *
 *      Notes:
 *          - All logic implemented in FanControl.cpp
 *          - No blocking calls or hardware writes occur here
 *          - Fully compatible with BurnEngine v2.3‑Environmental
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#ifndef FANCONTROL_H
#define FANCONTROL_H

#include <stdint.h>

// Initialize fan control system
void fancontrol_init();

// Apply fan control logic and return final fan percent
int fancontrol_apply(int demand);

// Internal compute function (used by apply)
int fan_compute(int demand);

#endif

