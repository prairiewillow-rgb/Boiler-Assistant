/*
 * Boiler Assistant - shared ArduinoJson configuration (v3.4.0 "Total Domination")
 * Every file must include this instead of <ArduinoJson.h> so all modules share
 * one configuration. 32-bit numbers save about 3.8 KB of flash; the controller
 * keeps its own math in double, only JSON values are rounded to float.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H

#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_POOL_CAPACITY 32
#include <ArduinoJson.h>

// Shared, non-inlined setters save repeated JSON proxy setup in non-LTO builds.
// Keys must have static storage: they are linked, not copied into the JSON pool.
void jsonSetUnsigned(JsonDocument& doc, const char* staticKey, uint32_t value);
void jsonSetSigned(JsonDocument& doc, const char* staticKey, int32_t value);
void jsonSetBool(JsonDocument& doc, const char* staticKey, bool value);

// Serializes into a String reserved once at the exact size. Returns an empty
// String (web server answers 503) if memory runs out, instead of the silently
// cut-off reply ArduinoJson produces when a growing String fails to expand.
// Consumes and clears the document, releasing its pools before the next
// request. Defined in WiFiAPI.cpp.
String jsonToString(JsonDocument& doc);

#endif
