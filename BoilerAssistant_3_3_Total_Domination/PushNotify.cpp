/*
 * Boiler Assistant - PushNotify
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#include "PushNotify.h"

#if !BOILER_NTFY_ENABLED

void pushnotify_loop() {}
bool pushnotify_sendTest() { return false; }
const char* pushnotify_status() { return "Disabled for isolation test"; }
void pushnotify_networkLost() {}

#else

#include <Arduino.h>
#include <new>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>

#include "SystemState.h"
#include "SystemData.h"
#include "WiFiProvisioning.h"
#include "RuntimeWiFiClient.h"
#include "Sensors.h"
#include "DebugLog.h"

extern SystemData sys;

static const unsigned long NTFY_HTTP_TIMEOUT_MS = 8000UL;
static const unsigned long NTFY_MIN_INTERVAL_MS = 5000UL;

static unsigned long lastSendMs = 0;
static bool sendAttempted = false;
struct PushMessage {
    const char* title;
    const char* message;
    bool urgent;
};
static const uint8_t PUSH_QUEUE_SIZE = 8;
static PushMessage pushQueue[PUSH_QUEUE_SIZE];
static uint8_t queueHead = 0;
static uint8_t queueCount = 0;
static bool queueFullLogged = false;
enum PushPhase { PUSH_IDLE, PUSH_WRITING, PUSH_WAITING };
static PushPhase pushPhase = PUSH_IDLE;
// A WiFiClient owns a 1,024-byte heap receive buffer, so the TLS client exists only while a
// notification is being sent.
static RuntimeWiFiClient<WiFiSSLClient>* pushClient = nullptr;
static void releasePushClient() {
    if (!pushClient) return;
    pushClient->stop();
    delete pushClient;
    pushClient = nullptr;
}
static unsigned long responseStartMs = 0;
static unsigned long lastPushIoMs = 0;
static char statusLine[64];
static uint8_t statusLen = 0;
static const char* deliveryStatus = "Idle";

static bool prevHighTemp        = false;
static bool prevTankFault       = false;
static bool prevExhaustFallback = false;
static bool prevGuardian        = false;
static bool prevProbeMissing    = false;
static char probeMissingMsg[48];

static bool pushConfigured() {
    return sys.pushEnabled &&
           sys.pushTopic[0] != '\0' &&
           !wifi_prov_isAPMode();
}

static bool queuePush(const char* title, const char* message, bool urgent) {
    if (!pushConfigured()) return false;
    if (queueCount == PUSH_QUEUE_SIZE) {
        if (!queueFullLogged) DBG_PRINTLN("Push: queue full");
        queueFullLogged = true;
        deliveryStatus = "Queue full";
        return false;
    }
    pushQueue[(queueHead + queueCount) % PUSH_QUEUE_SIZE] = {title, message, urgent};
    ++queueCount;
    deliveryStatus = "Queued";
    return true;
}

static void finishPush(const char* status) {
    releasePushClient();
    pushPhase = PUSH_IDLE;
    deliveryStatus = status;
    DBG_PRINT("Push: ");
    DBG_PRINTLN(status);
    queueHead = (queueHead + 1) % PUSH_QUEUE_SIZE;
    --queueCount;
    queueFullLogged = false;
    lastSendMs = millis();
}

static void servicePush() {
    if (!sys.wifiOK || !wifi_prov_prepareIo()) return;
    unsigned long now = millis();
    if (pushPhase == PUSH_IDLE) {
        if (queueCount == 0 ||
            (sendAttempted && now - lastSendMs < NTFY_MIN_INTERVAL_MS)) return;
        sendAttempted = true;
        if (!pushClient) pushClient = new (std::nothrow) RuntimeWiFiClient<WiFiSSLClient>;
        if (!pushClient) {
            finishPush("Out of memory");
            return;
        }
        // TLS connect remains synchronous in WiFiS3, but has an explicit bound.
        pushClient->setConnectionTimeout(2000);
        if (!pushClient->connect("ntfy.sh", 443)) {
            finishPush("Connection failed");
            return;
        }
        pushPhase = PUSH_WRITING;
        deliveryStatus = "Sending";
        return;
    }

    if (!pushClient) {
        finishPush("Interrupted");
        return;
    }

    if (pushPhase == PUSH_WRITING) {
        const PushMessage& entry = pushQueue[queueHead];
        static const char REQUEST_FORMAT[] =
            "POST /%s HTTP/1.1\r\nHost: ntfy.sh\r\nUser-Agent: BoilerAssistant\r\n"
            "Title: %s\r\n%sContent-Type: text/plain\r\nConnection: close\r\n"
            "Content-Length: %u\r\n\r\n%s";
        const char* priority = entry.urgent ? "Priority: urgent\r\n" : "";
        unsigned int bodyLength = static_cast<unsigned int>(strlen(entry.message));
        int length = snprintf(nullptr, 0, REQUEST_FORMAT,
            sys.pushTopic, entry.title, priority, bodyLength, entry.message);
        // Heap, not stack: the main stack is only 1 KB.
        char* request = length > 0 ? static_cast<char*>(malloc(length + 1)) : nullptr;
        if (request == nullptr) {
            finishPush("Out of memory");
            return;
        }
        snprintf(request, length + 1, REQUEST_FORMAT,
            sys.pushTopic, entry.title, priority, bodyLength, entry.message);
        size_t written = pushClient->write(reinterpret_cast<const uint8_t*>(request), length);
        free(request);
        if (written != static_cast<size_t>(length)) {
            finishPush("Request write failed");
            return;
        }
        responseStartMs = millis();
        statusLen = 0;
        pushPhase = PUSH_WAITING;
        return;
    }

    if (now - responseStartMs >= NTFY_HTTP_TIMEOUT_MS) {
        finishPush("Response timed out");
        return;
    }
    int available = pushClient->available();
    if (available <= 0) return;
    uint8_t buffer[64];
    int received = pushClient->read(buffer, min(available, 64));
    for (int i = 0; i < received; ++i) {
        char c = static_cast<char>(buffer[i]);
        if (c == '\n') {
            statusLine[statusLen] = '\0';
            finishPush(strstr(statusLine, " 200") ? "Delivered" : "Server rejected notification");
            return;
        }
        if (c != '\r') {
            if (statusLen >= sizeof(statusLine) - 1) {
                finishPush("Invalid server response");
                return;
            }
            statusLine[statusLen++] = c;
        }
    }
}

void pushnotify_loop() {
    if (!pushConfigured()) {
        if ((!sys.pushEnabled || sys.pushTopic[0] == '\0') && queueCount != 0) {
            releasePushClient();
            pushPhase = PUSH_IDLE;
            queueCount = 0;
            deliveryStatus = "Cancelled: push disabled";
            DBG_PRINTLN("Push: off; queue cleared");
        }
        prevHighTemp        = (sys.safetyState == SAFETY_HIGHTEMP);
        prevTankFault       = (sys.safetyState == SAFETY_SENSOR_FAULT &&
                               (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0);
        prevExhaustFallback = sys.exhaustFallbackActive;
        prevGuardian        = (sys.emberGuardianLatched ||
                               sys.burnState == BURN_EMBER_GUARD);
        prevProbeMissing    = sensors_missingProbe() != nullptr;
        return;
    }

    bool highTemp = (sys.safetyState == SAFETY_HIGHTEMP);
    bool tankFault = (sys.safetyState == SAFETY_SENSOR_FAULT &&
                      (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0);
    bool exhaustFallback = sys.exhaustFallbackActive;
    bool guardian = (sys.emberGuardianLatched ||
                     sys.burnState == BURN_EMBER_GUARD);

    if (highTemp != prevHighTemp) {
        if (queuePush(highTemp ? "Boiler: HIGH TEMP LOCKOUT" : "Boiler: High temp cleared",
                 highTemp ? "Tank over-temperature - system stopped" : "High temperature cleared",
                 highTemp)) prevHighTemp = highTemp;
    }
    if (tankFault != prevTankFault) {
        if (queuePush(tankFault ? "Boiler: TANK PROBE FAULT" : "Boiler: Tank fault cleared",
                 tankFault ? "Tank probe fault - system stopped" : "Tank probe fault cleared",
                 tankFault)) prevTankFault = tankFault;
    }
    if (exhaustFallback != prevExhaustFallback) {
        if (queuePush(exhaustFallback ? "Boiler: EXHAUST PROBE FAULT" : "Boiler: Exhaust probe cleared",
                 exhaustFallback ? "Exhaust probe needs cleaning - fan at max"
                                   : "Exhaust probe fault cleared",
                 exhaustFallback)) prevExhaustFallback = exhaustFallback;
    }
    if (guardian != prevGuardian) {
        if (queuePush(guardian ? "Boiler: EMBER GUARDIAN" : "Boiler: Ember Guardian cleared",
                 guardian ? "Ember Guardian active - reset required" : "Ember Guardian cleared",
                 guardian)) prevGuardian = guardian;
    }
    const char* missing = sensors_missingProbe();
    if ((missing != nullptr) != prevProbeMissing) {
        if (missing) snprintf(probeMissingMsg, sizeof(probeMissingMsg),
                              "%s stopped reading", missing);
        if (queuePush(missing ? "Boiler: PROBE MISSING" : "Boiler: Probes OK",
                 missing ? probeMissingMsg : "All water probes reading again",
                 false)) prevProbeMissing = missing != nullptr;
    }
    if (millis() - lastPushIoMs >= 100UL) {
        lastPushIoMs = millis();
        servicePush();
    }
}

bool pushnotify_sendTest() {
    if (!sys.wifiOK) return false;
    return queuePush("Boiler: TEST", "Test notification from Boiler Assistant", false);
}

const char* pushnotify_status() {
    return deliveryStatus;
}

void pushnotify_networkLost() {
    if (pushPhase != PUSH_IDLE) {
        releasePushClient();
        pushPhase = PUSH_IDLE;
        if (queueCount != 0) {
            queueHead = (queueHead + 1) % PUSH_QUEUE_SIZE;
            --queueCount;
        }

        deliveryStatus = "Interrupted: WiFi lost";
        DBG_PRINTLN("Push: WiFi lost mid-send");
    }
}

#endif // BOILER_NTFY_ENABLED
