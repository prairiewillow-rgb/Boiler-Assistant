/*
 * ============================================================
 *  Boiler Assistant – Fan Dimmer API (v3.3.7 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanDimmer.h
 *  License: CC BY-NC-SA 4.0
 *
 *  Public interface for the fan dimmer output driver. Callers set
 *  the requested fan percentage; the module selects zero-cross PSM
 *  phase control or legacy PWM automatically.
 *
 *  Version:
 *      Boiler Assistant v3.3.7 "Total Domination"
 * ============================================================
 */

#ifndef FANDIMMER_H
#define FANDIMMER_H

#include <Arduino.h>

bool fan_dimmer_init();
void fan_dimmer_setPercent(int percent);
bool fan_dimmer_phaseModeActive();

#endif