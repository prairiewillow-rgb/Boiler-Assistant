#include "DataShare.h"
#include "DataLogger.h"
#include <EEPROM.h>
#include <string.h>

namespace {
constexpr int ADDRESS = 2048;
constexpr uint32_t MAGIC = 0x44534831UL;
struct Saved {
    uint32_t magic;
    char make[BOILER_TEXT_MAX + 1];
    char model[BOILER_TEXT_MAX + 1];
    uint8_t consentVersion;  // 0 = not agreed
    uint8_t reserved[15];
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

// Printable ASCII only, outer spaces trimmed.
bool cleanText(const char* in, char* out) {
    if (!in) in = "";
    while (*in == ' ') ++in;
    size_t n = strlen(in);
    while (n && in[n - 1] == ' ') --n;
    if (n > BOILER_TEXT_MAX) return false;
    for (size_t i = 0; i < n; ++i)
        if (in[i] < 0x20 || in[i] > 0x7E) return false;
    memcpy(out, in, n);
    out[n] = '\0';
    return true;
}

bool store(const Saved& candidate) {
    if (memcmp(&candidate, &saved, sizeof(saved)) == 0) return true;
    uint16_t crc = checksum(candidate);
    EEPROM.put(ADDRESS, candidate);
    EEPROM.put(ADDRESS + sizeof(candidate), crc);
    Saved verified;
    uint16_t storedCrc;
    EEPROM.get(ADDRESS, verified);
    EEPROM.get(ADDRESS + sizeof(verified), storedCrc);
    if (memcmp(&verified, &candidate, sizeof(candidate)) != 0 || storedCrc != crc) return false;
    saved = candidate;
    return true;
}
}

void datashare_init() {
    storageReady = ADDRESS + sizeof(Saved) + sizeof(uint16_t) <= static_cast<size_t>(EEPROM.length());
    saved = {};
    saved.magic = MAGIC;
    if (!storageReady) return;
    Saved candidate;
    uint16_t crc;
    EEPROM.get(ADDRESS, candidate);
    EEPROM.get(ADDRESS + sizeof(candidate), crc);
    if (candidate.magic == MAGIC && checksum(candidate) == crc &&
        memchr(candidate.make, 0, sizeof(candidate.make)) &&
        memchr(candidate.model, 0, sizeof(candidate.model))) saved = candidate;
}

const char* datashare_make() { return saved.make; }
const char* datashare_model() { return saved.model; }
bool datashare_consent() { return saved.consentVersion == DATASHARE_CONSENT_VERSION; }
bool datashare_active() { return datashare_consent() && datalogger_ready(); }

bool datashare_set(const char* make, const char* model, bool consent, const char*& error) {
    Saved candidate = saved;
    if (!storageReady) error = "Boiler info storage unavailable";
    else if (!cleanText(make, candidate.make) || !cleanText(model, candidate.model))
        error = "Boiler make/model: up to 31 plain characters";
    else if (consent && !datashare_consent() && !datalogger_ready())
        error = "Data sharing needs a working SD card";
    else if (consent && (!candidate.make[0] || !candidate.model[0]))
        error = "Enter boiler make and model to share data";
    else {
        candidate.consentVersion = consent ? DATASHARE_CONSENT_VERSION : 0;
        if (store(candidate)) return true;
        error = "Boiler info write verification failed";
    }
    return false;
}

void datashare_clearConsent() {
    if (!storageReady || saved.consentVersion == 0) return;
    Saved candidate = saved;
    candidate.consentVersion = 0;
    store(candidate);
}
