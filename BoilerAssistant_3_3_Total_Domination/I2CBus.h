/*
 * ============================================================
 *  Boiler Assistant - Shared I2C API (v3.3.9 "Total Domination")
 *  File: I2CBus.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Initialization, readiness, recovery requests and generation
 *    tracking for the shared LCD/keypad/BME bus. Main-loop
 *    servicing advances rate-limited bus recovery before devices.
 *    Permanently held lines require hardware investigation.
 *
 *  Version: Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
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
