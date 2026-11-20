/*
 * Boiler Assistant - I2CBus
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <Arduino.h>

void i2cbus_init();
void i2cbus_loop();
void i2cbus_requestRecovery();
bool i2cbus_ready();
uint32_t i2cbus_generation();

#endif
