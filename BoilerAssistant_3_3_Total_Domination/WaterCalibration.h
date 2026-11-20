#ifndef WATER_CALIBRATION_H
#define WATER_CALIBRATION_H

#include <Arduino.h>
#include "JsonConfig.h"

void watercal_init();
float watercal_offset(const uint8_t* rom);
bool watercal_probeOffset(uint8_t index, float& offset);
String watercal_json();
void watercal_buildJson(JsonDocument& doc);
bool watercal_save(const String& body, String& error);

#endif
