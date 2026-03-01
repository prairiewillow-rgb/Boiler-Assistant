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
