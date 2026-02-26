#pragma once
#include <Arduino.h>

// ============================================================
//  Initialization
// ============================================================
void sensors_init();

// ============================================================
//  Exhaust Temperature (Thermocouple)
// ============================================================
double exhaust_readF_cached();
double smoothExhaustF(double rawF);

// MAX31855 direct read (°F)
double readMAX31855_F();

// ============================================================
//  Environmental Sensor (BME280) – Stub
// ============================================================
float env_readTempF();
float env_readHumidity();
float env_readPressure();

// ============================================================
//  Water Temperature Probes (DS18B20) – Stub
// ============================================================
void sensors_readWaterTemps();
