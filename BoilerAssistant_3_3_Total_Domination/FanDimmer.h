/*
 * ============================================================
 *  Boiler Assistant - Fan Dimmer API (v3.3.11 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanDimmer.h
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *  Public interface for the fan dimmer output driver. Callers set
 *  the requested fan percentage; the module selects zero-cross PSM
 *  phase control or, in Auto Tank only, legacy PWM automatically.
 *
 *  Version:
 *      Boiler Assistant v3.3.11 "Total Domination"
 * ============================================================
 */

#ifndef FANDIMMER_H
#define FANDIMMER_H

#include <Arduino.h>

bool fan_dimmer_init();
int fan_dimmer_setPercent(int percent);
bool fan_dimmer_phaseModeActive();
bool fan_dimmer_boilerCallReady();
bool fan_dimmer_takeCallInterrupted();
// Bits: timer, recent edge, valid periods, stable, noise, phase, command, Continuous.
int fan_dimmer_diagnostics();

#endif