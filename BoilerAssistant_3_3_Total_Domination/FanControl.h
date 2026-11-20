/*
 * Boiler Assistant - FanControl
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef FANCONTROL_H
#define FANCONTROL_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Initialize fan control system
void fancontrol_init();

// Apply fan demand and return final fan percent
// (Handles Guardian hard-kill internally)
int fancontrol_apply(int demand);

#endif
