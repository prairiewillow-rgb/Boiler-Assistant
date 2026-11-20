/*
 * Boiler Assistant - FanDimmer
 * License: CC BY-NC-SA 4.0
 */

#ifndef FANDIMMER_H
#define FANDIMMER_H

#include <Arduino.h>

bool fan_dimmer_init();
int fan_dimmer_setPercent(int percent);
bool fan_dimmer_phaseModeActive();
// Qualified ZC for at least 500ms, even when requested fan output is zero.
bool fan_dimmer_boilerCallReady();
bool fan_dimmer_takeCallInterrupted();
// Bits: timer, recent edge, valid periods, stable, noise mask, phase, command, Continuous.
int fan_dimmer_diagnostics();

#endif