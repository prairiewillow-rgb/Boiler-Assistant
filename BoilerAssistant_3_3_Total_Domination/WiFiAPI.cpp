/*
 * Boiler Assistant - WiFiAPI
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "WiFiAPI.h"
#include "DebugLog.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include "WiFiProvisioning.h"
#include "EEPROMStorage.h"
#include "ConfigValidation.h"
#include "DashboardHTML.h"
#include "DashboardGz.h"
#include "WaterCalibration.h"
#include "FanDimmer.h"
#include "BurnEngine.h"
#include "PushNotify.h"
#include "Sensors.h"
#include "HttpBridgeReceive.h"
#include "BurnPerformance.h"
#include "DataLogger.h"
#include "DataShare.h"
#include "OTAUpdater.h"
#include "Watchdog.h"
#include <malloc.h>
#include <unistd.h>
#include <stdio.h>

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "JsonConfig.h"
#include <math.h>
#include <utility>

extern SystemData sys;

constexpr uint32_t dashboardFnv1a(const char* s, size_t n) {
    uint32_t h = 2166136261UL;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint8_t>(s[i]);
        h *= 16777619UL;
    }
    return h;
}
static_assert(sizeof(DASHBOARD_HTML) == DASHBOARD_SOURCE_LENGTH + 1 &&
              dashboardFnv1a(DASHBOARD_HTML, DASHBOARD_SOURCE_LENGTH) == DASHBOARD_SOURCE_FNV1A,
              "DashboardGz.h is stale: run tools\\make_dashboard_gz.ps1");

/* ============================================================
 *  WiFi Credentials (from provisioning)
 * ============================================================ */

static const char* getWifiSSID() {
    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0)
        return runtimeCreds.ssid;
    return "";
}

static const char* getWifiPASS() {
    if (runtimeCreds.hasCredentials && runtimeCreds.pass[0] != 0)
        return runtimeCreds.pass;
    return "";
}

/* ============================================================
 *  HTTP Server
 * ============================================================ */

WiFiServer server(80);

enum HttpPhase { HTTP_IDLE, HTTP_RECEIVING, HTTP_SENDING };
static HttpPhase httpPhase = HTTP_IDLE;
static HttpBridgeClient httpClient;
static unsigned long networkChangeQueuedMs = 0;
static String httpHeaders;
static String httpBody;
static size_t httpResponseHeaderLength = 0;
static String httpResponseBody;
static const char* httpResponseData = nullptr;
static size_t httpResponseLength = 0;
static size_t httpResponseOffset = 0;
static size_t httpHeaderOffset = 0;
static bool httpHeadersComplete = false;
static int httpContentLength = 0;
static unsigned long httpStartedMs = 0;
static unsigned long httpLastWriteMs = 0;
static unsigned long httpLastReadMs = 0;
static bool httpUseAccept = false;
// Field diagnostics for the 30 s "NET:" serial status line.
static uint16_t httpServed = 0, httpIdleClosed = 0, httpRecvTimeouts = 0,
                httpSendTimeouts = 0, httpWriteFails = 0, httpRestarts = 0;
static unsigned long httpLastServedMs = 0;
static bool httpStartedBefore = false;
// Only the request line and the three headers the firmware uses are kept, so
// browser headers (cookies, user agent) cost no heap and cannot fragment it.
static const size_t HTTP_HEADER_LIMIT = 8192;
static const size_t HTTP_REQUEST_LINE_LIMIT = 512;
static char httpHeaderLine[48];
static uint8_t httpHeaderLineLength = 0;
static bool httpHeaderLineFirst = true;
static bool httpHeaderLineTruncated = false;
static size_t httpHeaderBytes = 0;
static bool httpTokenOk = false;
static bool httpBadFraming = false;
static bool httpContentLengthSeen = false;
static bool httpDashboardNotModified = false;
static const size_t HTTP_BODY_LIMIT = 2048;
static const size_t HTTP_READ_CHUNK = HttpBridgeReceive::MAX_BYTES;
static const size_t HTTP_WRITE_CHUNK = 512;
static const size_t HTTP_RESPONSE_HEADER_LIMIT = 384;
static const unsigned long HTTP_RECEIVE_TIMEOUT_MS = 5000UL;
static const unsigned long HTTP_SEND_TIMEOUT_MS = 30000UL;
// SD downloads can take minutes, so they time out only when no bytes move.
static const unsigned long HTTP_STREAM_STALL_MS = 15000UL;

static bool httpStreaming = false;
static size_t httpStreamFill = 0;
static size_t httpStreamPos = 0;
static uint32_t httpStreamRemaining = 0;
static unsigned long httpLastProgressMs = 0;

// Once all response headers have been sent, SD downloads can reuse both the
// header region and the request/JSON region without a separate allocation.
static uint8_t httpIoStorage[HTTP_RESPONSE_HEADER_LIMIT + HTTP_READ_CHUNK];
static_assert(sizeof(httpIoStorage) >= HTTP_WRITE_CHUNK, "SD download chunk must fit HTTP storage");
static uint8_t* const httpIoBuffer = httpIoStorage + HTTP_RESPONSE_HEADER_LIMIT;
static char* responseHeaderBuffer() {
    return reinterpret_cast<char*>(httpIoStorage);
}
// The state and diag replies are sent straight from their JSON document, a
// piece at a time, instead of first copying the whole reply into a String.
// That copy (~1.5 KB on top of the ~2 KB document) no longer fits in the heap.
static JsonDocument* httpResponseDoc = nullptr;

// Serializes only bytes [skip, skip + capacity) of a document into a buffer.
class JsonChunkWriter {
public:
    JsonChunkWriter(uint8_t* buffer, size_t skip, size_t capacity)
        : buffer_(buffer), skip_(skip), capacity_(capacity) {}
    size_t write(uint8_t c) {
        if (skip_) { --skip_; return 1; }
        if (count_ < capacity_) buffer_[count_++] = c;
        return 1;
    }
    size_t write(const uint8_t* s, size_t n) {
        for (size_t i = 0; i < n; ++i) write(s[i]);
        return n;
    }
    size_t count() const { return count_; }
private:
    uint8_t* buffer_;
    size_t skip_;
    size_t capacity_;
    size_t count_ = 0;
};

static void closeHttp() {
    if (httpResponseDoc) {
        httpResponseDoc->clear();
        httpResponseDoc = nullptr;
    }
    if (httpStreaming) {
        datalogger_closeDownload();
        httpStreaming = false;
    }
    httpClient.stop();
    httpHeaders = String();
    httpBody = String();
    httpResponseHeaderLength = 0;
    httpResponseBody = String();
    httpResponseData = nullptr;
    httpPhase = HTTP_IDLE;
}

static void queueResponse(const char* status, const char* contentType,
                          const char* data, size_t length,
                          const char* extraHeaders = "",
                          const char* cacheControl = "no-store") {
    char lengthHeader[48] = "";
    if (strcmp(status, "304 Not Modified") != 0)
        snprintf(lengthHeader, sizeof(lengthHeader), "Content-Length: %u\r\n",
                 static_cast<unsigned int>(length));
    int headerLength = snprintf(responseHeaderBuffer(), HTTP_RESPONSE_HEADER_LIMIT,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: %s\r\n"
        "%sConnection: close\r\n%s\r\n",
        status, contentType, cacheControl, extraHeaders, lengthHeader);
    if (headerLength < 0 || static_cast<size_t>(headerLength) >= HTTP_RESPONSE_HEADER_LIMIT) {
        DBG_PRINTLN("HTTP: hdr too big");
        closeHttp();
        return;
    }
    httpResponseHeaderLength = headerLength;
    httpResponseData = data;
    httpResponseLength = length;
    httpResponseOffset = httpHeaderOffset = 0;
    httpStartedMs = millis();
    httpLastWriteMs = millis() - 10UL;
    httpPhase = HTTP_SENDING;
}

String jsonToString(JsonDocument& doc) {
    doc.shrinkToFit();
    String out;
    size_t length = measureJson(doc);
    bool complete = !doc.overflowed() && out.reserve(length) &&
                    serializeJson(doc, out) == length && out.length() == length;
    doc.clear();
    if (!complete) return String();
    return out;
}

void __attribute__((noinline)) jsonSetUnsigned(JsonDocument& doc, const char* staticKey,
                                             uint32_t value) {
    doc[JsonString(staticKey, true)] = value;
}

void __attribute__((noinline)) jsonSetSigned(JsonDocument& doc, const char* staticKey,
                                           int32_t value) {
    doc[JsonString(staticKey, true)] = value;
}

void __attribute__((noinline)) jsonSetBool(JsonDocument& doc, const char* staticKey, bool value) {
    doc[JsonString(staticKey, true)] = value;
}

static void queueJson(const char* status, String json) {
    if (!json) {
        Serial.println("HTTP: no mem");
        static const char error[] = "{\"error\":\"insufficient memory\"}";
        queueResponse("503 Service Unavailable", "application/json", error, sizeof(error) - 1);
        return;
    }
    httpResponseBody = std::move(json);
    queueResponse(status, "application/json",
                  httpResponseBody.c_str(), httpResponseBody.length());
}

// Sends doc without a String copy; closeHttp() clears it after the reply.
static void queueDoc(JsonDocument& doc) {
    httpHeaders = String();
    httpBody = String();
    doc.shrinkToFit();
    if (doc.overflowed()) {
        doc.clear();
        Serial.println("HTTP: no mem");
        static const char error[] = "{\"error\":\"insufficient memory\"}";
        queueResponse("503 Service Unavailable", "application/json", error, sizeof(error) - 1);
        return;
    }
    httpResponseDoc = &doc;
    queueResponse("200 OK", "application/json", nullptr, measureJson(doc));
}

// Copies one query parameter from the request line, decoding %XX escapes.
static bool queryParam(const String& req, const char* name, char* out, size_t outSize) {
    int q = req.indexOf('?');
    int end = req.indexOf(' ', req.indexOf(' ') + 1);
    if (q < 0 || end < 0 || q > end || outSize == 0) return false;
    size_t nameLength = strlen(name);
    int pos = q + 1;
    while (pos < end) {
        int amp = req.indexOf('&', pos);
        if (amp < 0 || amp > end) amp = end;
        if (amp - pos > static_cast<int>(nameLength) && req[pos + nameLength] == '=' &&
            strncmp(req.c_str() + pos, name, nameLength) == 0) {
            size_t n = 0;
            for (int i = pos + nameLength + 1; i < amp; ++i) {
                char c = req[i];
                if (c == '%' && i + 2 < amp) {
                    char hex[3] = { req[i + 1], req[i + 2], 0 };
                    c = static_cast<char>(strtol(hex, nullptr, 16));
                    i += 2;
                }
                if (n + 1 >= outSize) return false;
                out[n++] = c;
            }
            out[n] = '\0';
            return true;
        }
        pos = amp + 1;
    }
    return false;
}

// Route handlers called from dispatchHttp() must stay out of line. When the
// compiler inlined them, every request (even the 2 s state poll) reserved
// stack for all their buffers at once and overran the 1 KB stack into the heap.
#define HTTP_HANDLER static __attribute__((noinline))

HTTP_HANDLER void startSdDownload(const String& req) {
    char path[28];
    uint32_t size = 0;
    uint32_t start = 0;
    if (!queryParam(req, "f", path, sizeof(path)) || !datalogger_validPath(path)) {
        queueJson("400 Bad Request", "{\"error\":\"invalid file\"}");
        return;
    }
    char startText[12];
    bool hasStart = queryParam(req, "start", startText, sizeof(startText));
    if (!hasStart && (req.indexOf("&start=") >= 0 || req.indexOf("?start=") >= 0)) {
        queueJson("400 Bad Request", "{\"error\":\"invalid history offset\"}");
        return;
    }
    if (hasStart) {
        bool valid = startText[0] != '\0' &&
                     (!strncmp(path, "HISTORY/", 8) || !strncmp(path, "LOGS/", 5));
        for (const char* p = startText; valid && *p; ++p) {
            if (*p < '0' || *p > '9' || start > (UINT32_MAX - (*p - '0')) / 10UL)
                valid = false;
            else start = start * 10UL + (*p - '0');
        }
        if (!valid) {
            queueJson("400 Bad Request", "{\"error\":\"invalid history offset\"}");
            return;
        }
    }
    uint32_t count = 0;
    char countText[12];
    bool hasCount = queryParam(req, "count", countText, sizeof(countText));
    if (hasCount || req.indexOf("&count=") >= 0 || req.indexOf("?count=") >= 0) {
        bool valid = hasCount && countText[0] != '\0' &&
                     (!strncmp(path, "HISTORY/", 8) || !strncmp(path, "LOGS/", 5));
        for (const char* p = countText; valid && *p; ++p) {
            if (*p < '0' || *p > '9' || count > (UINT32_MAX - (*p - '0')) / 10UL)
                valid = false;
            else count = count * 10UL + (*p - '0');
        }
        if (!valid || count == 0 || count > 16384UL) {
            queueJson("400 Bad Request", "{\"error\":\"invalid history count\"}");
            return;
        }
    }
    if (datalogger_downloadBusy()) {
        queueJson("503 Service Unavailable", "{\"error\":\"SD batch in progress; retry shortly\"}");
        return;
    }
    if (!datalogger_openDownload(path, size, start)) {
        queueJson("404 Not Found", "{\"error\":\"file unavailable\"}");
        return;
    }
    if (hasCount && size > count) size = count;
    char extra[96];
    const char* name = strchr(path, '/') + 1;
    snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\nX-SD-Offset: %lu\r\n",
             name, static_cast<unsigned long>(start));
    httpStreaming = true;
    httpStreamFill = httpStreamPos = 0;
    httpStreamRemaining = size;
    httpLastProgressMs = millis();
    queueResponse("200 OK", "text/csv", nullptr, size, extra);
}

/* ============================================================
 *  Retry Timer
 * ============================================================ */


/* ============================================================
 *  JSON Documents
 * ============================================================ */

static StaticJsonDocument<4096> stateDoc;
static StaticJsonDocument<512> settingsDoc;

/* ============================================================
 *  Helpers
 * ============================================================ */

static void sendJson(WiFiClient&, String json) {
    queueJson("200 OK", std::move(json));
}

static void sendNotFound(WiFiClient&) {
    queueJson("404 Not Found", "{\"error\":\"not found\"}");
}

static void sendUnauthorized(WiFiClient&) {
    queueJson("401 Unauthorized", "{\"error\":\"authentication required\"}");
}

static bool hasApiToken() {
    return httpTokenOk;
}

static const char* headerValue(const char* line, const char* name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < n; ++i) {
        if (tolower(static_cast<unsigned char>(line[i])) != name[i]) return nullptr;
    }
    line += n;
    while (*line == ' ' || *line == '\t') ++line;
    return line;
}

static void dashboardEtag(char* out, size_t size) {
    snprintf(out, size, "W/\"%08lX\"", static_cast<unsigned long>(DASHBOARD_SOURCE_FNV1A));
}

// Called for each complete header line (not the request line).
static void processHeaderLine() {
    char* line = httpHeaderLine;
    size_t length = httpHeaderLineLength;
    while (length && (line[length - 1] == ' ' || line[length - 1] == '\t')) --length;
    line[length] = '\0';
    const char* value;
    if (headerValue(line, "transfer-encoding:")) {
        httpBadFraming = true;
    } else if ((value = headerValue(line, "content-length:")) != nullptr) {
        if (httpContentLengthSeen || httpHeaderLineTruncated || *value == '\0') {
            httpBadFraming = true;
            return;
        }
        httpContentLengthSeen = true;
        int contentLength = 0;
        for (; *value; ++value) {
            if (*value < '0' || *value > '9') { httpBadFraming = true; return; }
            contentLength = contentLength * 10 + (*value - '0');
            if (contentLength > static_cast<int>(HTTP_BODY_LIMIT)) { httpBadFraming = true; return; }
        }
        httpContentLength = contentLength;
    } else if ((value = headerValue(line, "x-boiler-token:")) != nullptr) {
        httpTokenOk = !httpHeaderLineTruncated && runtimeCreds.controlPass[0] != '\0' &&
                      strcmp(value, runtimeCreds.controlPass) == 0;
    } else if ((value = headerValue(line, "if-none-match:")) != nullptr) {
        char etag[16];
        dashboardEtag(etag, sizeof(etag));
        if (!httpHeaderLineTruncated && strcmp(value, etag) == 0)
            httpDashboardNotModified = true;
    }
}

static void resetHttpRequest() {
    httpHeaders = String();
    httpBody = String();
    httpHeaderLineLength = 0;
    httpHeaderLineFirst = true;
    httpHeaderLineTruncated = false;
    httpHeaderBytes = 0;
    httpTokenOk = false;
    httpBadFraming = false;
    httpContentLengthSeen = false;
    httpDashboardNotModified = false;
    httpHeadersComplete = false;
    httpContentLength = 0;
}

// Feeds one received header byte. Returns false after queueing an error reply.
static bool receiveHeaderByte(char c) {
    if (++httpHeaderBytes > HTTP_HEADER_LIMIT) {
        Serial.println("HTTP: headers too large");
        queueJson("431 Request Header Fields Too Large", "{\"error\":\"headers too large\"}");
        return false;
    }
    if (c == '\r') return true;
    if (c != '\n') {
        if (httpHeaderLineFirst) {
            if (httpHeaders.length() >= HTTP_REQUEST_LINE_LIMIT) {
                queueJson("414 URI Too Long", "{\"error\":\"request too long\"}");
                return false;
            }
            if (!httpHeaders.concat(c)) {
                Serial.println("HTTP: no mem");
                static const char error[] = "{\"error\":\"insufficient memory\"}";
                queueResponse("503 Service Unavailable", "application/json",
                              error, sizeof(error) - 1);
                return false;
            }
        } else if (httpHeaderLineLength < sizeof(httpHeaderLine) - 1) {
            httpHeaderLine[httpHeaderLineLength++] = c;
        } else {
            httpHeaderLineTruncated = true;
        }
        return true;
    }
    if (httpHeaderLineFirst) {
        httpHeaderLineFirst = false;
        return true;
    }
    if (httpHeaderLineLength == 0 && !httpHeaderLineTruncated) {
        httpHeadersComplete = true;
        if (httpBadFraming) {
            DBG_PRINTLN("HTTP: body big");
            queueJson("400 Bad Request", "{\"error\":\"invalid content length or encoding\"}");
            return false;
        }
        if (httpContentLength > 0 && !httpBody.reserve(httpContentLength)) {
            Serial.println("HTTP: no mem");
            queueJson("503 Service Unavailable", "{\"error\":\"insufficient memory\"}");
            return false;
        }
        return true;
    }
    processHeaderLine();
    httpHeaderLineLength = 0;
    httpHeaderLineTruncated = false;
    return true;
}

/* ============================================================
 *  JSON Builders
 * ============================================================ */

// Also written to the SD log as ALARM lines (DataLogger).
const char* wifiapi_alertText() {
    const char* alertText = "";
    if (sys.emberGuardianLatched || sys.burnState == BURN_EMBER_GUARD) {
        alertText = "EMBER GUARDIAN ACTIVE - RESET REQUIRED";
    } else if (sys.safetyState == SAFETY_HIGHTEMP) {
        alertText = "HIGH TEMPERATURE LOCKOUT";
    } else if (sys.safetyState == SAFETY_SENSOR_FAULT) {
        alertText = (sys.sensorFaultMask & SENSOR_FAULT_TANK)
                        ? "TANK PROBE FAULT - SYSTEM STOPPED"
                        : "SENSOR FAULT - SYSTEM STOPPED";
    } else if (sys.exhaustFallbackActive) {
        alertText = "EXHAUST PROBE FAULT";
    }
    static char missingText[48];
    const char* missing = sensors_missingProbe();
    if (!alertText[0] && missing) {
        snprintf(missingText, sizeof(missingText), "PROBE MISSING: %s", missing);
        alertText = missingText;
    }
    return alertText;
}

HTTP_HANDLER void buildStateJson() {
    stateDoc.clear();
    stateDoc["fan_diag"] = fan_dimmer_diagnostics();

    const char* burnText =
        (sys.burnState == BURN_IDLE) ? "IDLE" :
        (sys.burnState == BURN_BOOST) ? "BOOST" :
        (sys.burnState == BURN_RAMP) ? "RAMP" :
        (sys.burnState == BURN_HOLD) ? "HOLD" :
        (sys.burnState == BURN_EMBER_GUARD) ? "EMBER_GUARD" : "UNKNOWN";
    const char* safetyText =
        (sys.safetyState == SAFETY_OK) ? "OK" :
        (sys.safetyState == SAFETY_HIGHTEMP) ? "HIGH_TEMP" : "SENSOR_FAULT";
    bool tankSensorOK = false;
    float tankTemp = NAN;
    if (sys.waterProbeCount > 0) {
        uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
        if (tankProbe < sys.waterProbeCount) {
            tankTemp = sys.waterTempF[tankProbe];
            tankSensorOK = !isnan(tankTemp) &&
                           millis() - sys.waterTempLastGoodMs[tankProbe] <= 3000UL;
        }
    }

    stateDoc["exhaust_raw"]   = sys.exhaustRawF;
    stateDoc["exhaust_smooth"] = sys.exhaustSmoothF;
    jsonSetSigned(stateDoc, "exhaust_setpoint", sys.exhaustSetpoint);
    jsonSetSigned(stateDoc, "deadband", sys.deadbandF);
    jsonSetSigned(stateDoc, "fan", sys.fanFinal);
    jsonSetSigned(stateDoc, "fan_demand", sys.fanDemand);
    jsonSetSigned(stateDoc, "fan_final", sys.fanFinal);
    jsonSetSigned(stateDoc, "fan_pwm_percent", sys.fanFinal);
    stateDoc["adaptive_slope"] = burnengine_getAdaptiveSlope();
    jsonSetBool(stateDoc, "adaptive_learning", burnengine_getAdaptiveLearning());
    char adaptiveDiagnostics[200];
    burnengine_getAdaptiveDiagnostics(adaptiveDiagnostics, sizeof(adaptiveDiagnostics));
    stateDoc["adaptive_learning_status"] = adaptiveDiagnostics;
    jsonSetSigned(stateDoc, "burn_state", sys.burnState);
    // These point at string literals, so the document can link them instead of copying each one.
    stateDoc["burn_state_text"] = JsonString(burnText, true);
    jsonSetBool(stateDoc, "damper_delay_active", sys.burnState == BURN_BOOST &&
                                               sys.boostActive && sys.damperDelayActive);
    jsonSetSigned(stateDoc, "safety_state", sys.safetyState);
    stateDoc["safety_text"] = JsonString(safetyText, true);
    jsonSetBool(stateDoc, "exhaust_fallback", sys.exhaustFallbackActive);
    jsonSetBool(stateDoc, "ember_guard_paused", burnengine_guardianPaused());
    jsonSetBool(stateDoc, "ember_guard_timer_active", sys.emberGuardianTimerActive);
    if (sys.emberGuardianTimerActive) {
        unsigned long elapsed = millis() - sys.emberGuardianStartMs;
        unsigned long limit = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;
        jsonSetUnsigned(stateDoc, "ember_guard_remaining_s", elapsed < limit ? (limit - elapsed) / 1000UL : 0);
    }
    stateDoc["alert"] = wifiapi_alertText();
    stateDoc["tank_temp"] = tankTemp;
    jsonSetBool(stateDoc, "tank_sensor_ok", tankSensorOK);
    jsonSetBool(stateDoc, "exhaust_sensor_ok", sys.exhaustSensorOK);
    if (sys.exhaustLastGoodMs) jsonSetUnsigned(stateDoc, "exhaust_age_ms", millis() - sys.exhaustLastGoodMs);
    jsonSetBool(stateDoc, "wifi_ok", sys.wifiOK);
    stateDoc["display_name"] = runtimeCreds.displayName;
    jsonSetSigned(stateDoc, "tank_low", sys.tankLowSetpointF);
    jsonSetSigned(stateDoc, "tank_high", sys.tankHighSetpointF);
    jsonSetBool(stateDoc, "self_clean_enabled", sys.selfCleanEnabled);
    jsonSetBool(stateDoc, "self_clean_due", sys.selfCleanDue);
    jsonSetBool(stateDoc, "self_clean_active", sys.selfCleanActive);
    jsonSetBool(stateDoc, "self_clean_manual", sys.selfCleanManualRequested);
    jsonSetUnsigned(stateDoc, "self_clean_burn_count", sys.selfCleanBurnCount);
    jsonSetUnsigned(stateDoc, "self_clean_interval", sys.selfCleanIntervalBurns);
    jsonSetBool(stateDoc, "push_enabled", sys.pushEnabled);
    jsonSetBool(stateDoc, "push_configured", sys.pushEnabled && sys.pushTopic[0] != '\0');
    stateDoc["push_status"] = JsonString(pushnotify_status(), true);
    jsonSetUnsigned(stateDoc, "loop_last_ms", sys.loopLastMs);
    jsonSetUnsigned(stateDoc, "loop_max_ms", sys.loopMaxMs);
    jsonSetUnsigned(stateDoc, "network_last_ms", sys.networkLastMs);
    jsonSetUnsigned(stateDoc, "network_max_ms", sys.networkMaxMs);

    if (sys.wifiOK && wifi_prov_cachedRSSI() != 0) {
        stateDoc["rssi"] = wifi_prov_cachedRSSI();
    } else {
        stateDoc["rssi"] = nullptr;
    }

    JsonObject env = stateDoc.createNestedObject("env");
    env["sensor_ok"] = sys.envSensorOK;
    env["temp_f"]   = sys.envTempF;
    env["humidity"] = sys.envHumidity;
    env["pressure"] = sys.envPressure;
    env["units"]    = JsonString(sys.envUnitsMetric ? "metric" : "imperial", true);

    JsonArray water = stateDoc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        JsonObject probe = water.createNestedObject();
        probe["index"] = i;
        char id[17];
        sensors_waterProbeId(i, id);
        probe["id"] = id;
        probe["name"] = sys.waterProbeNames[i];
        probe["temp_f"] = sys.waterTempF[i];
        probe["sensor_ok"] = millis() - sys.waterTempLastGoodMs[i] <= 3000UL;
        probe["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
        if (sys.waterTempLastGoodMs[i]) probe["age_ms"] = millis() - sys.waterTempLastGoodMs[i];
    }

    queueDoc(stateDoc);
}

HTTP_HANDLER void buildSettingsJson() {
    settingsDoc.clear();

    jsonSetSigned(settingsDoc, "exhaust_setpoint", sys.exhaustSetpoint);
    jsonSetSigned(settingsDoc, "control_mode", sys.controlMode);
    jsonSetSigned(settingsDoc, "deadband", sys.deadbandF);
    jsonSetBool(settingsDoc, "adaptive_learning", burnengine_getAdaptiveLearning());
    jsonSetSigned(settingsDoc, "boost_time", sys.boostTimeSeconds);
    jsonSetUnsigned(settingsDoc, "boost_fan_pct", sys.boostFanPercent);
    jsonSetUnsigned(settingsDoc, "damper_delay", sys.damperDelaySeconds);
    jsonSetSigned(settingsDoc, "clamp_min", sys.clampMinPercent);
    jsonSetSigned(settingsDoc, "clamp_max", sys.clampMaxPercent);
    jsonSetUnsigned(settingsDoc, "deadzone_fan", sys.deadzoneFanMode);
    jsonSetSigned(settingsDoc, "ember_minutes", sys.emberGuardianTimerMinutes);
    jsonSetSigned(settingsDoc, "flue_low", sys.flueLowThreshold);
    jsonSetSigned(settingsDoc, "flue_recovery", sys.flueRecoveryThreshold);
    jsonSetSigned(settingsDoc, "tank_low", sys.tankLowSetpointF);
    jsonSetSigned(settingsDoc, "tank_high", sys.tankHighSetpointF);
    jsonSetUnsigned(settingsDoc, "env_units", sys.envUnitsMetric);
    jsonSetBool(settingsDoc, "self_clean_enabled", sys.selfCleanEnabled);
    jsonSetUnsigned(settingsDoc, "self_clean_interval", sys.selfCleanIntervalBurns);
    jsonSetUnsigned(settingsDoc, "self_clean_start_hour", sys.selfCleanStartHour);
    jsonSetUnsigned(settingsDoc, "self_clean_end_hour", sys.selfCleanEndHour);
    jsonSetSigned(settingsDoc, "self_clean_utc_offset", sys.selfCleanUtcOffsetMinutes);
    jsonSetBool(settingsDoc, "self_clean_dst", sys.selfCleanDstEnabled);
    jsonSetBool(settingsDoc, "push_enabled", sys.pushEnabled);
    settingsDoc["push_topic"]    = sys.pushTopic;
    settingsDoc["display_name"]  = runtimeCreds.displayName;
    settingsDoc["boiler_make"]   = datashare_make();
    settingsDoc["boiler_model"]  = datashare_model();
    jsonSetSigned(settingsDoc, "share_data", datashare_consent() ? 1 : 0);
    jsonSetBool(settingsDoc, "share_active", datashare_active());
    jsonSetBool(settingsDoc, "sd_ready", datalogger_ready());
    queueDoc(settingsDoc);
}

static void __attribute__((noinline)) setSeasonNumber(JsonObject row, const char* key, int32_t value) {
    row[JsonString(key, true)] = value;
}

HTTP_HANDLER void buildSeasonalJson() {
    stateDoc.clear();
    JsonDocument& doc = stateDoc;
    jsonSetBool(doc, "auto_enabled", sys.envAutoSeasonEnabled);
    jsonSetUnsigned(doc, "lockout_hours", sys.envModeLockoutSec / 3600UL);
    jsonSetUnsigned(doc, "legacy_mode", sys.envSeasonMode);
    JsonArray seasons = doc.createNestedArray("seasons");
    for (uint8_t i = 0; i < 4; ++i) {
        EnvSeason season = static_cast<EnvSeason>(i);
        JsonObject row = seasons.createNestedObject();
        setSeasonNumber(row, "start_f", *ui_getSeasonStartPtr(season));
        setSeasonNumber(row, "hysteresis_f", *ui_getSeasonBufferPtr(season));
        setSeasonNumber(row, "exhaust_f", *ui_getSeasonSetpointPtr(season));
        setSeasonNumber(row, "tank_low_f", *ui_getSeasonTankLowPtr(season));
        setSeasonNumber(row, "tank_high_f", *ui_getSeasonTankHighPtr(season));
        setSeasonNumber(row, "fan_max_pct", *ui_getSeasonClampMaxPtr(season));
        setSeasonNumber(row, "boost_time", sys.envBoostTimeSeconds[i]);
        setSeasonNumber(row, "boost_fan_pct", sys.envBoostFanPercent[i]);
        setSeasonNumber(row, "damper_delay", sys.envDamperDelaySeconds[i]);
    }
    doc["active_season"] = sys.envActiveSeason;
    if (sys.envSensorOK && isfinite(sys.envTempF)) doc["outdoor_f"] = sys.envTempF;
    else doc["outdoor_f"] = nullptr;
    jsonSetBool(doc, "sensor_ok", sys.envSensorOK && isfinite(sys.envTempF));
    unsigned long elapsed = (millis() - sys.envSeasonChangedMs) / 1000UL;
    jsonSetUnsigned(doc, "lockout_remaining_s", elapsed < sys.envModeLockoutSec
        ? sys.envModeLockoutSec - elapsed : 0);
    JsonObject applied = doc.createNestedObject("applied");
    setSeasonNumber(applied, "exhaust_f", sys.exhaustSetpoint);
    setSeasonNumber(applied, "tank_low_f", sys.tankLowSetpointF);
    setSeasonNumber(applied, "tank_high_f", sys.tankHighSetpointF);
    setSeasonNumber(applied, "fan_min_pct", sys.clampMinPercent);
    setSeasonNumber(applied, "fan_max_pct", sys.clampMaxPercent);
    setSeasonNumber(applied, "boost_time", sys.boostTimeSeconds);
    setSeasonNumber(applied, "boost_fan_pct", sys.boostFanPercent);
    setSeasonNumber(applied, "damper_delay", sys.damperDelaySeconds);
    queueDoc(doc);
}

static bool rejectSeasonal(String& error, const char* message) {
    DBG_PRINT("HTTP: season bad: ");
    DBG_PRINTLN(message);
    error = message;
    return false;
}

static bool saveSeasonal(const String& body, String& error) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, body) || !doc.is<JsonObject>())
        return rejectSeasonal(error, "Invalid seasonal JSON or insufficient memory");
    if (!doc["auto_enabled"].is<bool>() || !doc["lockout_hours"].is<int>() ||
        !validLockoutHours(doc["lockout_hours"].as<int>()) ||
        !doc["seasons"].is<JsonArray>() || doc["seasons"].size() != 4)
        return rejectSeasonal(error, "Expected auto_enabled, lockout_hours (0-99) and four seasons");

    struct Profile {
        int start, hysteresis, exhaust, low, high, maximum, boost, boostFan, delay;
    };
    Profile profiles[4];
    for (uint8_t i = 0; i < 4; ++i) {
        JsonObjectConst row = doc["seasons"][i].as<JsonObjectConst>();
        if (row.isNull() || !row["start_f"].is<int>() ||
            !row["hysteresis_f"].is<int>() || !row["exhaust_f"].is<int>() ||
            !row["tank_low_f"].is<int>() || !row["tank_high_f"].is<int>() ||
            !row["fan_max_pct"].is<int>() || !row["boost_time"].is<int>() ||
            !row["boost_fan_pct"].is<int>() || !row["damper_delay"].is<int>())
            return rejectSeasonal(error, "Every seasonal value must be an integer");
        Profile& p = profiles[i];
        p.start = row["start_f"];
        p.hysteresis = row["hysteresis_f"];
        p.exhaust = row["exhaust_f"];
        p.low = row["tank_low_f"];
        p.high = row["tank_high_f"];
        p.maximum = row["fan_max_pct"];
        p.boost = row["boost_time"];
        p.boostFan = row["boost_fan_pct"];
        p.delay = row["damper_delay"];
        if (!validSeasonStart(p.start) || !validSeasonHysteresis(p.hysteresis) ||
            !validExhaustSetpoint(p.exhaust) || !validTankSetpoint(p.low) ||
            !validTankSetpoint(p.high) || p.low >= p.high || p.high >= 190 ||
            !validFanClamp(p.maximum) || p.maximum < sys.clampMinPercent ||
            !validBoostTime(p.boost) || !validDamperDelay(p.delay) ||
            !validBoostFan(p.boostFan, sys.clampMinPercent, p.maximum))
            return rejectSeasonal(error,
                "Check ranges: damper 0-300 s; boost 0-600 s; boost fan within min/max; tank low < high < 190 F");
    }
    if (profiles[3].start >= profiles[2].start || profiles[2].start >= profiles[1].start)
        return rejectSeasonal(error, "Thresholds must be Extreme < Winter < Spring/Fall");

    // Validate the full candidate before changing any live value or EEPROM.
    uint16_t boostTimes[4];
    uint8_t boostFans[4];
    uint16_t damperDelays[4];
    for (uint8_t i = 0; i < 4; ++i) boostTimes[i] = profiles[i].boost;
    for (uint8_t i = 0; i < 4; ++i) boostFans[i] = profiles[i].boostFan;
    for (uint8_t i = 0; i < 4; ++i) damperDelays[i] = profiles[i].delay;
    if (!eeprom_saveEnvSeasonBoostTimes(boostTimes))
        return rejectSeasonal(error, "Could not save seasonal boost times");
    if (!eeprom_saveEnvBoostFanPercent(boostFans))
        return rejectSeasonal(error, "Could not save seasonal boost speeds");
    if (!eeprom_saveEnvDamperDelays(damperDelays))
        return rejectSeasonal(error, "Could not save seasonal damper delays");
    for (uint8_t i = 0; i < 4; ++i) {
        EnvSeason season = static_cast<EnvSeason>(i);
        const Profile& p = profiles[i];
        *ui_getSeasonStartPtr(season) = p.start;
        *ui_getSeasonBufferPtr(season) = p.hysteresis;
        *ui_getSeasonSetpointPtr(season) = p.exhaust;
        *ui_getSeasonTankLowPtr(season) = p.low;
        *ui_getSeasonTankHighPtr(season) = p.high;
        *ui_getSeasonClampMaxPtr(season) = static_cast<uint8_t>(p.maximum);
        sys.envBoostTimeSeconds[i] = boostTimes[i];
        sys.envBoostFanPercent[i] = boostFans[i];
        sys.envDamperDelaySeconds[i] = damperDelays[i];
    }
    sys.envModeLockoutSec = doc["lockout_hours"].as<uint32_t>() * 3600UL;
    sys.envAutoSeasonEnabled = doc["auto_enabled"].as<bool>();
    eeprom_saveEnvSeasonStarts();
    eeprom_saveEnvSeasonHyst();
    eeprom_saveEnvSeasonSetpoints();
    eeprom_saveEnvSeasonTankValues();
    eeprom_saveEnvSeasonClampValues();
    eeprom_saveEnvLockoutHours(static_cast<uint8_t>(sys.envModeLockoutSec / 3600UL));
    eeprom_saveEnvAutoSeason(sys.envAutoSeasonEnabled);
    sys.remoteChanged = true;
    sys.uiNeedsRefresh = true;
    DBG_PRINTLN("HTTP: seasons saved");
    return true;
}

/* ============================================================
 *  POST /api/set
 * ============================================================ */

HTTP_HANDLER void handleApiSet(WiFiClient& client, String& body) {
    StaticJsonDocument<512> doc;
    const String& input = body;
    DeserializationError err = deserializeJson(doc, input);
    // ArduinoJson owns its strings. Release the HTTP copy before EEPROM's 1 KB page.
    body = String();

    if (err) {
        if (err == DeserializationError::NoMemory) {
            Serial.println("HTTP: no mem");
            queueJson("503 Service Unavailable", "{\"error\":\"insufficient memory\"}");
        } else {
            sendJson(client, "{\"error\":\"invalid JSON\"}");
        }
        return;
    }

    int exhaustSetpoint = sys.exhaustSetpoint;
    int deadband = sys.deadbandF;
    int boostTime = sys.boostTimeSeconds;
    int boostFan = sys.boostFanPercent;
    int damperDelay = sys.damperDelaySeconds;
    int clampMin = sys.clampMinPercent;
    int clampMax = sys.clampMaxPercent;
    int deadzoneFan = sys.deadzoneFanMode;
    int guardianMinutes = sys.emberGuardianTimerMinutes;
    int flueLow = sys.flueLowThreshold;
    int flueRecovery = sys.flueRecoveryThreshold;
    int tankLow = sys.tankLowSetpointF;
    int tankHigh = sys.tankHighSetpointF;
    int envUnits = sys.envUnitsMetric;
    bool selfCleanEnabled = sys.selfCleanEnabled;
    int selfCleanInterval = sys.selfCleanIntervalBurns;
    int selfCleanStartHour = sys.selfCleanStartHour;
    int selfCleanEndHour = sys.selfCleanEndHour;
    int selfCleanUtcOffset = sys.selfCleanUtcOffsetMinutes;
    bool selfCleanDst = sys.selfCleanDstEnabled;
    bool selfCleanRunNow = false;
    bool adaptiveLearning = burnengine_getAdaptiveLearning();
    bool resetAdaptiveLearning = false;

    if (doc.containsKey("exhaust_setpoint")) exhaustSetpoint = doc["exhaust_setpoint"];
    if (doc.containsKey("deadband")) deadband = doc["deadband"];
    if (doc.containsKey("boost_time")) boostTime = doc["boost_time"];
    if (doc.containsKey("damper_delay")) {
        if (!doc["damper_delay"].is<int>() || !validDamperDelay(doc["damper_delay"])) {
            queueJson("400 Bad Request", "{\"error\":\"damper delay must be an integer from 0 to 300 seconds\"}");
            return;
        }
        damperDelay = doc["damper_delay"];
    }
    if (doc.containsKey("boost_fan_pct")) {
        if (!doc["boost_fan_pct"].is<int>()) {
            queueJson("400 Bad Request", "{\"error\":\"boost fan must be an integer\"}");
            return;
        }
        boostFan = doc["boost_fan_pct"];
    }
    if (doc.containsKey("clamp_min")) clampMin = doc["clamp_min"];
    if (doc.containsKey("clamp_max")) clampMax = doc["clamp_max"];
    if (doc.containsKey("deadzone_fan")) deadzoneFan = doc["deadzone_fan"];
    if (doc.containsKey("ember_minutes")) guardianMinutes = doc["ember_minutes"];
    if (doc.containsKey("flue_low")) flueLow = doc["flue_low"];
    if (doc.containsKey("flue_recovery")) flueRecovery = doc["flue_recovery"];
    if (doc.containsKey("tank_low")) tankLow = doc["tank_low"];
    if (doc.containsKey("tank_high")) tankHigh = doc["tank_high"];
    if (doc.containsKey("env_units")) envUnits = doc["env_units"];
    if (doc.containsKey("self_clean_enabled")) selfCleanEnabled = doc["self_clean_enabled"];
    if (doc.containsKey("self_clean_interval")) selfCleanInterval = doc["self_clean_interval"];
    if (doc.containsKey("self_clean_start_hour")) selfCleanStartHour = doc["self_clean_start_hour"];
    if (doc.containsKey("self_clean_end_hour")) selfCleanEndHour = doc["self_clean_end_hour"];
    if (doc.containsKey("self_clean_utc_offset")) selfCleanUtcOffset = doc["self_clean_utc_offset"];
    if (doc.containsKey("self_clean_dst")) selfCleanDst = doc["self_clean_dst"];
    if (doc.containsKey("self_clean_run_now")) selfCleanRunNow = doc["self_clean_run_now"];
    if (doc.containsKey("adaptive_learning")) {
        if (!doc["adaptive_learning"].is<bool>()) {
            queueJson("400 Bad Request", "{\"error\":\"adaptive learning must be enabled or disabled\"}");
            return;
        }
        adaptiveLearning = doc["adaptive_learning"];
    }
    if (doc.containsKey("reset_adaptive_learning")) {
        if (!doc["reset_adaptive_learning"].is<bool>()) {
            queueJson("400 Bad Request", "{\"error\":\"adaptive reset must be true or false\"}");
            return;
        }
        resetAdaptiveLearning = doc["reset_adaptive_learning"];
    }
    if (!validBoostFan(boostFan, clampMin, clampMax)) {
        queueJson("400 Bad Request", "{\"error\":\"boost fan must be within fan minimum/maximum\"}");
        return;
    }
    if (!validExhaustSetpoint(exhaustSetpoint) ||
        !validDeadband(deadband) ||
        !validBoostTime(boostTime) ||
        !validFanClamp(clampMin) ||
        !validFanClamp(clampMax) ||
        clampMin > clampMax ||
        (deadzoneFan != 0 && deadzoneFan != 1) ||
        !validGuardianMinutes(guardianMinutes) ||
        !validFlueThreshold(flueLow) ||
        !validFlueThreshold(flueRecovery) ||
        flueRecovery < flueLow ||
        !validTankSetpoint(tankLow) ||
        !validTankSetpoint(tankHigh) ||
        tankLow >= tankHigh ||
        tankHigh >= 190 ||
        (envUnits != 0 && envUnits != 1) ||
        !validSelfCleanInterval(selfCleanInterval) ||
        !validSelfCleanHour(selfCleanStartHour) ||
        !validSelfCleanHour(selfCleanEndHour) ||
        !validSelfCleanUtcOffset(selfCleanUtcOffset)) {
        sendJson(client, "{\"error\":\"configuration value out of range\"}");
        return;
    }

    watchdog_stage(WD_SAVE);
    if (doc.containsKey("boost_fan_pct") && !eeprom_saveBoostFanPercent(boostFan)) {
        queueJson("500 Internal Server Error", "{\"error\":\"could not save boost fan\"}");
        return;
    }
    if (doc.containsKey("damper_delay") && !eeprom_saveDamperDelay(damperDelay)) {
        queueJson("500 Internal Server Error", "{\"error\":\"could not save damper delay\"}");
        return;
    }

    if (doc.containsKey("boiler_make") || doc.containsKey("boiler_model") ||
        doc.containsKey("share_data")) {
        const char* error = nullptr;
        if (!datashare_set(doc["boiler_make"] | datashare_make(),
                           doc["boiler_model"] | datashare_model(),
                           (doc["share_data"] | (datashare_consent() ? 1 : 0)) != 0, error)) {
            StaticJsonDocument<128> response;
            response["error"] = error;
            String json;
            serializeJson(response, json);
            sendJson(client, json);
            return;
        }
    }

    bool changed = false;

    if (doc.containsKey("exhaust_setpoint")) {
        sys.exhaustSetpoint = exhaustSetpoint;
        eeprom_saveSetpoint(exhaustSetpoint);
        changed = true;
    }
    if (doc.containsKey("deadband")) {
        sys.deadbandF = deadband;
        eeprom_saveDeadband(deadband);
        changed = true;
    }
    if (doc.containsKey("boost_time")) {
        sys.boostTimeSeconds = boostTime;
        eeprom_saveBoostTime(boostTime);
        changed = true;
    }
    if (doc.containsKey("boost_fan_pct")) {
        sys.boostFanPercent = boostFan;
        changed = true;
    }
    if (doc.containsKey("damper_delay")) {
        sys.damperDelaySeconds = damperDelay;
        changed = true;
    }
    if (doc.containsKey("clamp_min")) {
        sys.clampMinPercent = clampMin;
        eeprom_saveClampMin(clampMin);
        changed = true;
    }
    if (doc.containsKey("clamp_max")) {
        sys.clampMaxPercent = clampMax;
        eeprom_saveClampMax(clampMax);
        changed = true;
    }
    if (doc.containsKey("deadzone_fan")) {
        sys.deadzoneFanMode = deadzoneFan;
        eeprom_saveDeadzone(deadzoneFan);
        changed = true;
    }
    if (doc.containsKey("ember_minutes")) {
        sys.emberGuardianTimerMinutes = guardianMinutes;
        eeprom_saveEmberGuardianMinutes(guardianMinutes);
        changed = true;
    }
    if (doc.containsKey("flue_low")) {
        sys.flueLowThreshold = flueLow;
        eeprom_saveFlueLow(flueLow);
        changed = true;
    }
    if (doc.containsKey("flue_recovery")) {
        sys.flueRecoveryThreshold = flueRecovery;
        eeprom_saveFlueRecovery(flueRecovery);
        changed = true;
    }
    if (doc.containsKey("tank_low")) {
        sys.tankLowSetpointF = tankLow;
        eeprom_saveTankLow(tankLow);
        changed = true;
    }
    if (doc.containsKey("tank_high")) {
        sys.tankHighSetpointF = tankHigh;
        eeprom_saveTankHigh(tankHigh);
        changed = true;
    }
    if (doc.containsKey("env_units")) {
        sys.envUnitsMetric = (uint8_t)envUnits;
        eeprom_saveEnvUnits(sys.envUnitsMetric);
        sys.uiNeedsRefresh = true;
        changed = true;
    }
    if (doc.containsKey("self_clean_enabled") ||
        doc.containsKey("self_clean_interval") ||
        doc.containsKey("self_clean_start_hour") ||
        doc.containsKey("self_clean_end_hour") ||
        doc.containsKey("self_clean_utc_offset") ||
        doc.containsKey("self_clean_dst")) {
        sys.selfCleanEnabled = selfCleanEnabled;
        sys.selfCleanIntervalBurns = (uint16_t)selfCleanInterval;
        sys.selfCleanStartHour = (uint8_t)selfCleanStartHour;
        sys.selfCleanEndHour = (uint8_t)selfCleanEndHour;
        sys.selfCleanUtcOffsetMinutes = (int16_t)selfCleanUtcOffset;
        sys.selfCleanDstEnabled = selfCleanDst;
        if (!sys.selfCleanEnabled) {
            sys.selfCleanDue = false;
            sys.selfCleanActive = false;
            sys.selfCleanManualRequested = false;
            sys.selfCleanBurnCount = 0;
        }
        eeprom_saveSelfClean();
        changed = true;
    }
    if (doc.containsKey("adaptive_learning")) {
        burnengine_setAdaptiveLearning(adaptiveLearning);
        changed = true;
    }
    if (resetAdaptiveLearning) {
        burnengine_resetAdaptiveLearning();
        changed = true;
    }
    if (selfCleanRunNow && sys.selfCleanEnabled) {
        sys.selfCleanManualRequested = true;
        changed = true;
    }

    // === PUSH NOTIFICATIONS ===
    bool pushChanged = false;
    if (doc.containsKey("push_enabled")) {
        sys.pushEnabled = (doc["push_enabled"] | 0) != 0;
        pushChanged = true;
    }
    if (doc.containsKey("push_topic")) {
        const char* t = doc["push_topic"] | "";
        char clean[33];
        uint8_t n = 0;
        for (const char* p = t; *p && n < 32; p++) {
            char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_') {
                clean[n++] = c;
            }
        }
        clean[n] = '\0';
        strncpy(sys.pushTopic, clean, sizeof(sys.pushTopic) - 1);
        sys.pushTopic[sizeof(sys.pushTopic) - 1] = '\0';
        pushChanged = true;
    }
    if (pushChanged) {
        if (sys.pushTopic[0] == '\0') sys.pushEnabled = false;
        eeprom_savePushNotify();
        changed = true;
    }

    // Boiler/owner name shown on the boot screen (same rules as Wi-Fi setup).
    if (doc.containsKey("display_name")) {
        const char* d = doc["display_name"] | "";
        while (*d == ' ') d++;
        char clean[21];
        uint8_t n = 0;
        for (const char* p = d; *p && n < 20; p++) {
            if (*p >= 32 && *p <= 126) clean[n++] = (char)toupper((unsigned char)*p);
        }
        while (n > 0 && clean[n - 1] == ' ') n--;
        clean[n] = '\0';
        if (strcmp(clean, runtimeCreds.displayName) != 0) {
            strncpy(runtimeCreds.displayName, clean, sizeof(runtimeCreds.displayName) - 1);
            runtimeCreds.displayName[sizeof(runtimeCreds.displayName) - 1] = '\0';
            eeprom_saveRuntimeCreds();
            changed = true;
        }
    }

    if (changed) {
        sys.remoteChanged = true;
    }

    sendJson(client, "{\"ok\":true}");
}

HTTP_HANDLER void handleApiProbe(WiFiClient& client, const String& body) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, body)) {
        queueJson("400 Bad Request", "{\"error\":\"invalid probe\"}");
        return;
    }

    int index = doc["index"] | -1;
    const char* name = doc["name"] | "";
    bool makeTank = doc["tank"] | false;
    if (index < 0 || index >= (int)sys.waterProbeCount ||
        (name[0] == '\0' && !makeTank)) {
        sendJson(client, "{\"error\":\"invalid probe\"}");
        return;
    }

    if (name[0] != '\0') sensors_setProbeName((uint8_t)index, name);
    if (makeTank) sensors_setTankProbe((uint8_t)index);
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

HTTP_HANDLER void handleApiReset(WiFiClient& client) {
    if (sys.safetyState == SAFETY_HIGHTEMP) {
        sendJson(client, "{\"error\":\"high-temperature lockout requires local reset\"}");
        return;
    }
    burnengine_resetAlarms();
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

/* ============================================================
 *  WiFi Init (provisioning-aware)
 * ============================================================ */

void wifiapi_init() {
    if (wifi_prov_isAPMode()) {
        DBG_PRINTLN("HTTP: skipped in AP");
        return;
    }

    DBG_PRINTLN("HTTP: init");

    const char* ssid = getWifiSSID();
    const char* pass = getWifiPASS();

    if (ssid[0] == 0) {
        DBG_PRINTLN("HTTP: no creds");
        sys.wifiOK = false;
        return;
    }
    if (!sys.wifiOK || !wifi_prov_prepareIo()) return;

    // WiFiProvisioning owns connection setup. Starting WiFi again here can
    // block the control loop even though provisioning already connected.
    bool restarting = httpStartedBefore && !server;
    unsigned long startedMs = millis();
    server.begin();
    wifi_prov_finishIo(startedMs, "HTTP server start");
    if (!wifi_prov_prepareIo()) return;
    if (!server) {
        Serial.println("HTTP: server start failed; recover bridge");
        wifi_prov_requestRecovery("HTTP server start failed");
        return;
    }
    if (restarting) ++httpRestarts;
    httpStartedBefore = true;

    // Bridge 0.4.1+ has accept(). available() only hands over connections
    // that already sent data; idle ones (phone pre-opens, phone asleep) stay
    // in the bridge's 8 slots until it sees them close, which can be hours.
    // Once all 8 are held the dashboard can't connect. accept() hands every
    // connection over so the receive timeout below closes idle ones.
    if (!wifi_prov_prepareIo()) return;
    startedMs = millis();
    const char* fw = WiFi.firmwareVersion();
    wifi_prov_finishIo(startedMs, "HTTP fw version");
    // Parse "major.minor.patch" by hand: sscanf would link ~1.9 KB of scanf code.
    int part[3] = {0, 0, 0};
    uint8_t parts = 0;
    bool digits = false;
    for (const char* p = fw; parts < 3; p++) {
        if (*p >= '0' && *p <= '9') {
            part[parts] = part[parts] * 10 + (*p - '0');
            digits = true;
        } else if (digits && (*p == '.' || parts == 2)) {
            parts++;
            digits = false;
            if (*p != '.') break;
        } else {
            break;
        }
    }
    bool known = parts == 3 && strcmp(fw, "99.99.99") != 0;
    httpUseAccept = known && (part[0] > 0 || part[1] > 4 || (part[1] == 4 && part[2] >= 1));
    Serial.print("HTTP: bridge ");
    Serial.print(fw);
    Serial.println(httpUseAccept ? " accept" : " available");
}

void wifiapi_stop() {
    if (httpPhase != HTTP_IDLE) closeHttp();
    if (wifi_prov_prepareClientIo()) {
        unsigned long startedMs = millis();
        server.end();
        wifi_prov_finishIo(startedMs, "HTTP server close");
    } else {
        // WiFiServer has no local invalidation API; its destructor sends no I/O.
        server = WiFiServer(80);
    }
}

// last=-1 means no request served yet.
int wifiapi_statusText(char* out, size_t size) {
    long last = httpServed ? static_cast<long>((millis() - httpLastServedMs) / 1000UL) : -1L;
    return snprintf(out, size, " http=%d srv=%d ok=%u idle=%u rto=%u sto=%u wf=%u hr=%u last=%ld",
                    static_cast<int>(httpPhase), server ? 1 : 0, httpServed, httpIdleClosed,
                    httpRecvTimeouts, httpSendTimeouts, httpWriteFails, httpRestarts, last);
}

// Freeze clues for the Diagnostic Test report. Kept out of /api/state so the
// 2 s poll does not grow.
HTTP_HANDLER void buildDiagJson() {
    stateDoc.clear();
    stateDoc["reset"] = watchdog_resetText();
    stateDoc["diag"] = wifi_prov_netText(true);
    queueDoc(stateDoc);
}

HTTP_HANDLER void handleSdList(const String& req) {
    char dir[8], before[13];
    if (!queryParam(req, "d", dir, sizeof(dir))) strcpy(dir, "LOGS");
    if (!queryParam(req, "before", before, sizeof(before))) before[0] = '\0';
    datalogger_buildListJson(stateDoc, dir, before);
    queueDoc(stateDoc);
}

// Authenticated save routes share one reply path.
HTTP_HANDLER void handleSave(bool (*save)(const String&, String&)) {
    if (!hasApiToken()) {
        sendUnauthorized(httpClient);
        return;
    }
    String error;
    if (save(httpBody, error)) {
        queueJson("200 OK", "{\"ok\":true}");
        return;
    }
    DynamicJsonDocument response(384);
    response["error"] = error;
    String json;
    serializeJson(response, json);
    queueJson("400 Bad Request", std::move(json));
}

/* ============================================================
 *  WiFi Loop
 * ============================================================ */

HTTP_HANDLER void handleNetwork(bool save, bool reset) {
    if (!hasApiToken()) {
        sendUnauthorized(httpClient);
        return;
    }
    if (ota_isActive() || wifi_prov_networkChangePending()) {
        queueJson("409 Conflict", "{\"error\":\"network change or update already pending\"}");
        return;
    }
    if (save) {
        const char* error = wifi_prov_saveNetwork(httpBody, reset);
        if (error) {
            stateDoc.clear();
            stateDoc["error"] = error;
            String json;
            serializeJson(stateDoc, json);
            queueJson("400 Bad Request", std::move(json));
            return;
        }
        networkChangeQueuedMs = millis();
        queueJson("202 Accepted", "{\"ok\":true,\"reconnecting\":true}");
        return;
    }
    stateDoc.clear();
    stateDoc["ssid"] = runtimeCreds.ssid;
    stateDoc["addressMode"] = networkSettings.fixed ? "fixed" : "dhcp";
    stateDoc["current_ip"] = wifi_prov_cachedIP().toString();
    stateDoc["ip"] = IPAddress(networkSettings.ip).toString();
    stateDoc["gateway"] = IPAddress(networkSettings.gateway).toString();
    stateDoc["subnet"] = IPAddress(networkSettings.subnet).toString();
    stateDoc["dns"] = IPAddress(networkSettings.dns).toString();
    queueDoc(stateDoc);
}

static void dispatchHttp() {
    httpServed++;
    httpLastServedMs = millis();
    const String& req = httpHeaders;
    WiFiClient& client = httpClient;
    const String& body = httpBody;
    if (req.startsWith("GET / HTTP") || req.startsWith("GET /dashboard")) {
        char etag[16], headers[96];
        dashboardEtag(etag, sizeof(etag));
        snprintf(headers, sizeof(headers),
                 "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\nETag: %s\r\n", etag);
        queueResponse(httpDashboardNotModified ? "304 Not Modified" : "200 OK",
                      "text/html; charset=utf-8",
                      reinterpret_cast<const char*>(DASHBOARD_HTML_GZ),
                      httpDashboardNotModified ? 0 : DASHBOARD_HTML_GZ_LENGTH,
                      headers, "private, no-cache");
    }
    else if (req.startsWith("GET /api/network HTTP")) {
        handleNetwork(false, false);
    }
    else if (req.startsWith("POST /api/network HTTP")) {
        handleNetwork(true, false);
    }
    else if (req.startsWith("POST /api/network/reset HTTP")) {
        handleNetwork(true, true);
    }
    else if (req.startsWith("GET /api/sd/list")) {
        handleSdList(req);
    }
    else if (req.startsWith("GET /api/sd/file")) {
        startSdDownload(req);
    }
    else if (req.startsWith("GET /api/sd")) {
        datalogger_buildStatusJson(stateDoc);
        queueDoc(stateDoc);
    }
    else if (req.startsWith("GET /api/burn_performance")) {
        datalogger_buildHistoryJson(stateDoc, true);
        queueDoc(stateDoc);
    }
    else if (req.startsWith("GET /api/water_calibration HTTP")) {
        watercal_buildJson(stateDoc);
        queueDoc(stateDoc);
    }
    else if (req.startsWith("POST /api/water_calibration HTTP")) {
        handleSave(watercal_save);
    }
    else if (req.startsWith("GET /api/seasonal HTTP")) {
        buildSeasonalJson();
    }
    else if (req.startsWith("POST /api/seasonal HTTP")) {
        handleSave(saveSeasonal);
    }
    else if (req.startsWith("GET /api/state")) {
        buildStateJson();
    }
    else if (req.startsWith("GET /api/diag")) {
        buildDiagJson();
    }
    else if (req.startsWith("POST /api/update/install HTTP")) {
        queueJson("403 Forbidden", "{\"error\":\"firmware updates are keypad-only\"}");
    }
    else if (req.startsWith("GET /api/history")) {
        datalogger_buildHistoryJson(stateDoc);
        queueDoc(stateDoc);
    }
    else if (req.startsWith("GET /api/settings")) {
        buildSettingsJson();
    }
    else if (req.startsWith("POST /api/set")) {
        if (hasApiToken()) {
            handleApiSet(client, httpBody);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/probe")) {
        if (hasApiToken()) {
            handleApiProbe(client, body);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/reset")) {
        if (hasApiToken()) {
            handleApiReset(client);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/push_test")) {
        if (hasApiToken()) {
#if BOILER_NTFY_ENABLED
            bool ok = pushnotify_sendTest();
            sendJson(client, ok ? "{\"ok\":true,\"queued\":true}"
                                : "{\"error\":\"push not queued - check topic, WiFi and queue status\"}");
#else
            queueJson("503 Service Unavailable",
                      "{\"error\":\"ntfy disabled for isolation test\"}");
#endif
        } else {
            sendUnauthorized(client);
        }
    }
    else {
        sendNotFound(client);
    }

    resetHttpRequest();
}

void wifiapi_loop() {
    if (wifi_prov_networkChangePending() && !ota_isActive() &&
        millis() - networkChangeQueuedMs >= 1000UL &&
        (httpPhase == HTTP_IDLE || millis() - networkChangeQueuedMs >= HTTP_SEND_TIMEOUT_MS)) {
        if (httpPhase != HTTP_IDLE) closeHttp();
        wifi_prov_applySavedNetwork();
        return;
    }
    if (wifi_prov_isAPMode()) {
        sys.wifiOK = false;
        if (httpPhase != HTTP_IDLE) closeHttp();
        return;
    }

    unsigned long now = millis();
    if (!sys.wifiOK) {
        if (httpPhase != HTTP_IDLE) closeHttp();
        return;
    }

    if (httpPhase == HTTP_IDLE) {
        static unsigned long lastAcceptMs = 0;
        if (now - lastAcceptMs < 20UL) return;
        lastAcceptMs = now;
        watchdog_stage(WD_HTTP_ACCEPT);
        unsigned long startedMs = millis();
        httpClient = httpUseAccept ? server.accept() : server.available();
        wifi_prov_finishIo(startedMs, "HTTP accept");
        if (!wifi_prov_prepareIo()) {
            httpClient.stop();
            return;
        }
        if (!httpClient) return;
        now = millis();
        resetHttpRequest();
        httpHeaders.reserve(96);
        httpStartedMs = now;
        httpLastReadMs = now - 20UL;
        httpPhase = HTTP_RECEIVING;
    }

    wifi_prov_sampleHeap();
    if (httpPhase == HTTP_SENDING) {
        watchdog_stage(WD_HTTP_SEND);
        if (httpStreaming ? (now - httpLastProgressMs >= HTTP_STREAM_STALL_MS)
                          : (now - httpStartedMs >= HTTP_SEND_TIMEOUT_MS)) {
            httpSendTimeouts++;
            Serial.println("HTTP: send timeout; close");
            closeHttp();
            return;
        }
        if (now - httpLastWriteMs < 10UL) return;
        httpLastWriteMs = now;
        bool sendingHeaders = httpHeaderOffset < httpResponseHeaderLength;
        if (!sendingHeaders && httpStreaming) {
            if (httpStreamPos == httpStreamFill) {
                if (httpStreamRemaining == 0) {
                    closeHttp();
                    return;
                }
                uint16_t want = static_cast<uint16_t>(
                    min(static_cast<uint32_t>(HTTP_WRITE_CHUNK), httpStreamRemaining));
                int got = datalogger_readDownload(httpIoStorage, want);
                if (got == -2) return;
                if (got <= 0) {
                    DBG_PRINTLN("HTTP: SD read failed; closed");
                    closeHttp();
                    return;
                }
                httpStreamFill = static_cast<size_t>(got);
                httpStreamPos = 0;
                httpStreamRemaining -= static_cast<uint32_t>(got);
                return;  // Let control run between the SD read and network send.
            }
            size_t written = httpClient.write(httpIoStorage + httpStreamPos,
                                              httpStreamFill - httpStreamPos);
            if (written == 0) {
                httpWriteFails++;
                DBG_PRINTLN("HTTP: dl write fail");
                closeHttp();
                return;
            }
            httpStreamPos += written;
            httpLastProgressMs = now;
            if (httpStreamRemaining == 0 && httpStreamPos == httpStreamFill) closeHttp();
            return;
        }
        const char* data = sendingHeaders ? responseHeaderBuffer() : httpResponseData;
        size_t& offset = sendingHeaders ? httpHeaderOffset : httpResponseOffset;
        size_t length = sendingHeaders ? httpResponseHeaderLength : httpResponseLength;
        size_t count = min(HTTP_WRITE_CHUNK, length - offset);
        const uint8_t* chunk = reinterpret_cast<const uint8_t*>(data) + offset;
        if (!sendingHeaders && httpResponseDoc && count > 0) {
            count = min(HTTP_READ_CHUNK, count);
            JsonChunkWriter writer(httpIoBuffer, offset, count);
            serializeJson(*httpResponseDoc, writer);
            if (writer.count() != count) {
                Serial.println("HTTP: reply changed; close");
                closeHttp();
                return;
            }
            chunk = httpIoBuffer;
        }
        if (count > 0) {
            size_t written = httpClient.write(chunk, count);
            if (written == 0) {
                httpWriteFails++;
                Serial.println("HTTP: write failed; close");
                closeHttp();
                return;
            }
            offset += written;
            if (httpStreaming) httpLastProgressMs = now;
        }
        if (httpHeaderOffset == httpResponseHeaderLength &&
            httpResponseOffset == httpResponseLength) closeHttp();
        return;
    }

    if (now - httpStartedMs >= HTTP_RECEIVE_TIMEOUT_MS) {
        if (httpHeaderBytes == 0) {
            // Idle connection (never sent a request): just close it; the
            // browser opens a new one if it needs it.
            httpIdleClosed++;
            DBG_PRINTLN("HTTP: idle connection closed");
            closeHttp();
            return;
        }
        httpRecvTimeouts++;
        Serial.println("HTTP: request timeout");
        queueJson("408 Request Timeout", "{\"error\":\"request timed out\"}");
        return;
    }

    if (now - httpLastReadMs < 20UL) return;
    httpLastReadMs = now;
    watchdog_stage(WD_HTTP_RECEIVE);
    int available = httpClient.available();
    if (available <= 0) return;
    size_t count = min(HTTP_READ_CHUNK, static_cast<size_t>(available));
    watchdog_stage(WD_HTTP_READ);
    int received = httpClient.readRequest(httpIoBuffer, count);
    if (received < 0) {
        DBG_PRINTLN("HTTP: request read failed; close");
        closeHttp();
        return;
    }
    watchdog_stage(WD_HTTP_PARSE);
    for (int i = 0; i < received; ++i) {
        if (!httpHeadersComplete) {
            if (!receiveHeaderByte(static_cast<char>(httpIoBuffer[i]))) return;
        } else if (httpBody.length() < static_cast<unsigned int>(httpContentLength)) {
            if (!httpBody.concat(static_cast<char>(httpIoBuffer[i]))) {
                Serial.println("HTTP: body memory exhausted");
                queueJson("503 Service Unavailable", "{\"error\":\"insufficient memory\"}");
                return;
            }
        }
    }
    if (httpHeadersComplete &&
        httpBody.length() == static_cast<unsigned int>(httpContentLength)) {
        watchdog_stage(WD_HTTP);
        dispatchHttp();
    }
}
