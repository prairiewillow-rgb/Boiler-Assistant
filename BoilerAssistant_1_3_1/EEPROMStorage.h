#pragma once
#include <Arduino.h>

void eeprom_init();
void eeprom_save_all();
void eeprom_save_setpoint();
void eeprom_save_burnlogic();
void eeprom_save_boost();
void eeprom_save_deadband();
void eeprom_save_clamps();
void eeprom_save_pid();
