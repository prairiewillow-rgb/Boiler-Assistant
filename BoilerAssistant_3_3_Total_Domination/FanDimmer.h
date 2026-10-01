#ifndef FANDIMMER_H
#define FANDIMMER_H

#include <Arduino.h>

bool fan_dimmer_init();
void fan_dimmer_setPercent(int percent);
bool fan_dimmer_phaseModeActive();

#endif