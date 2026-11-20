#include "WaterCalibration.h"
#include "DebugLog.h"
#include "Sensors.h"
#include "BurnPerformance.h"
#include "JsonConfig.h"
#include <EEPROM.h>
#include <math.h>
#include <string.h>

namespace {
constexpr int ADDRESS = 1536;
constexpr uint32_t MAGIC = 0x57434131UL;
struct Entry {
    uint8_t rom[8];
    float offset;
};
struct Saved {
    uint32_t magic;
    Entry entries[MAX_WATER_PROBES];
};
Saved saved = {};
bool storageReady = false;

uint16_t checksum(const Saved& value) {
    uint16_t crc = 0xFFFF;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t i = 0; i < sizeof(value); ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}
bool occupied(const Entry& e) {
    return e.rom[0] != 0;
}
bool decodeId(const char* id, uint8_t* rom) {
    if (!id || strlen(id) != 16) return false;
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t value = 0;
        for (uint8_t j = 0; j < 2; ++j) {
            char c = id[i * 2 + j];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
            value = value * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
        }
        rom[i] = value;
    }
    return rom[0] != 0;
}
bool valid(const Saved& value) {
    if (value.magic != MAGIC) return false;
    for (uint8_t i = 0; i < MAX_WATER_PROBES; ++i) {
        const Entry& e = value.entries[i];
        if (!isfinite(e.offset) || fabsf(e.offset) > 10) return false;
        if (!occupied(e)) continue;
        for (uint8_t j = 0; j < i; ++j)
            if (memcmp(e.rom, value.entries[j].rom, 8) == 0) return false;
    }
    return true;
}
}

void watercal_init() {
    storageReady = ADDRESS + sizeof(Saved) + sizeof(uint16_t) <= static_cast<size_t>(EEPROM.length());
    saved = {};
    saved.magic = MAGIC;
    if (!storageReady) {
        DBG_PRINTLN("WCal: no EEPROM");
        return;
    }
    Saved candidate;
    uint16_t crc;
    EEPROM.get(ADDRESS, candidate);
    EEPROM.get(ADDRESS + sizeof(candidate), crc);
    if (valid(candidate) && checksum(candidate) == crc) saved = candidate;
    else DBG_PRINTLN("WCal: none; zero");
}

float watercal_offset(const uint8_t* rom) {
    for (const Entry& e : saved.entries)
        if (occupied(e) && memcmp(e.rom, rom, 8) == 0) return e.offset;
    return 0;
}

bool watercal_probeOffset(uint8_t index, float& offset) {
    char id[17];
    uint8_t rom[8];
    if (!sensors_waterProbeId(index, id) || !decodeId(id, rom)) return false;
    offset = watercal_offset(rom);
    return true;
}

String watercal_json() {
    DynamicJsonDocument doc(2048);
    watercal_buildJson(doc);
    return jsonToString(doc);
}

void watercal_buildJson(JsonDocument& doc) {
    doc.clear();
    doc["storage_available"] = storageReady;
    JsonArray probes = doc.createNestedArray("probes");
    for (uint8_t i = 0; i < sys.waterProbeCount; ++i) {
        char id[17];
        uint8_t rom[8];
        if (!sensors_waterProbeId(i, id) || !decodeId(id, rom)) continue;
        JsonObject p = probes.createNestedObject();
        p["id"] = id;
        p["name"] = sys.waterProbeNames[i];
        p["offset_f"] = watercal_offset(rom);
        p["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
        bool fresh = sys.waterTempLastGoodMs[i] != 0 &&
                     millis() - sys.waterTempLastGoodMs[i] <= 3000UL && isfinite(sys.waterTempF[i]);
        if (fresh) {
            p["corrected_f"] = sys.waterTempF[i];
            p["uncalibrated_f"] = sys.waterTempF[i] - watercal_offset(rom);
        }
    }
}

bool watercal_save(const String& body, String& error) {
    DynamicJsonDocument doc(384);
    uint8_t rom[8];
    if (!storageReady) error = "Calibration EEPROM unavailable";
    else if (deserializeJson(doc, body) || !doc["offset_f"].is<float>() ||
             !doc["id"].is<const char*>() || !decodeId(doc["id"], rom))
        error = "Invalid probe ID or offset";
    else {
        float offset = doc["offset_f"];
        int slot = -1, empty = -1;
        bool connected = false;
        for (uint8_t i = 0; i < sys.waterProbeCount; ++i) {
            char id[17];
            if (sensors_waterProbeId(i, id) && strcmp(id, doc["id"].as<const char*>()) == 0)
                connected = true;
        }
        for (uint8_t i = 0; i < MAX_WATER_PROBES; ++i) {
            if (!occupied(saved.entries[i]) && empty < 0) empty = i;
            if (memcmp(saved.entries[i].rom, rom, 8) == 0) slot = i;
        }
        if (slot < 0) slot = empty;
        if (!connected) error = "Probe no longer connected; reload calibration";
        else if (!isfinite(offset) || fabsf(offset) > 10) error = "Offset must be between -10 and +10 F";
        else if (slot < 0) error = "Offset storage full; reconnect an old probe and reset its offset to zero";
        else {
            Saved candidate = saved;
            if (offset == 0) candidate.entries[slot] = {};
            else {
                memcpy(candidate.entries[slot].rom, rom, 8);
                candidate.entries[slot].offset = offset;
            }
            uint16_t crc = checksum(candidate);
            EEPROM.put(ADDRESS, candidate);
            EEPROM.put(ADDRESS + sizeof(candidate), crc);
            Saved verified;
            uint16_t storedCrc;
            EEPROM.get(ADDRESS, verified);
            EEPROM.get(ADDRESS + sizeof(verified), storedCrc);
            if (memcmp(&verified, &candidate, sizeof(candidate)) != 0 || storedCrc != crc)
                error = "Calibration write verification failed";
            else {
                float delta = offset - watercal_offset(rom);
                saved = candidate;
                for (uint8_t i = 0; i < sys.waterProbeCount; ++i) {
                    char id[17];
                    if (sensors_waterProbeId(i, id) && strcmp(id, doc["id"].as<const char*>()) == 0 &&
                        isfinite(sys.waterTempF[i])) sys.waterTempF[i] += delta;
                }
                if (delta != 0) {
                    burnperformance_calibrationChanged();
                }
                DBG_PRINTLN("WCal: saved");
                return true;
            }
        }
    }
    DBG_PRINT("WCal: rejected: ");
    DBG_PRINTLN(error);
    return false;
}
