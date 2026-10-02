#include "PushNotify.h"

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include <WDT.h>

#include "SystemState.h"
#include "SystemData.h"
#include "WiFiProvisioning.h"

extern SystemData sys;

static const unsigned long NTFY_HTTP_TIMEOUT_MS = 8000UL;
static const unsigned long NTFY_MIN_INTERVAL_MS = 5000UL;

static unsigned long lastSendMs = 0;

static bool prevHighTemp        = false;
static bool prevTankFault       = false;
static bool prevExhaustFallback = false;
static bool prevGuardian        = false;

static bool pushConfigured() {
    return sys.pushEnabled &&
           sys.pushTopic[0] != '\0' &&
           !wifi_prov_isAPMode() &&
           WiFi.status() == WL_CONNECTED;
}

static bool pushSend(const char* title, const char* message, bool urgent) {
    if (!pushConfigured()) return false;

    unsigned long now = millis();
    if (now - lastSendMs < NTFY_MIN_INTERVAL_MS) return false;

    WiFiSSLClient client;
    WDT.refresh();
    if (!client.connect("ntfy.sh", 443)) {
        return false;
    }

    client.print("POST /");
    client.print(sys.pushTopic);
    client.print(" HTTP/1.0\r\nHost: ntfy.sh\r\n");
    client.print("User-Agent: BoilerAssistant\r\n");
    client.print("Title: ");
    client.print(title);
    client.print("\r\n");
    if (urgent) client.print("Priority: urgent\r\n");
    client.print("Content-Type: text/plain\r\nConnection: close\r\n\r\n");
    client.print(message);

    bool got200 = false;
    bool statusDone = false;
    char statusLine[24];
    uint8_t statusLen = 0;
    bool gotData = false;

    unsigned long start = millis();
    while (millis() - start < NTFY_HTTP_TIMEOUT_MS) {
        WDT.refresh();
        if (!client.available()) {
            if (gotData && !client.connected()) break;
            delay(5);
            continue;
        }
        int value = client.read();
        if (value < 0) continue;
        char c = (char)value;
        gotData = true;
        if (!statusDone) {
            if (c == '\n') statusDone = true;
            else if (c != '\r' && statusLen < sizeof(statusLine) - 1) statusLine[statusLen++] = c;
        } else {
            break;
        }
    }
    client.stop();
    statusLine[statusLen] = '\0';

    got200 = (strstr(statusLine, " 200") != nullptr);
    lastSendMs = now;
    return got200;
}

void pushnotify_loop() {
    if (!pushConfigured()) {
        prevHighTemp        = (sys.safetyState == SAFETY_HIGHTEMP);
        prevTankFault       = (sys.safetyState == SAFETY_SENSOR_FAULT &&
                               (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0);
        prevExhaustFallback = sys.exhaustFallbackActive;
        prevGuardian        = (sys.emberGuardianLatched ||
                               sys.burnState == BURN_EMBER_GUARD);
        return;
    }

    bool highTemp = (sys.safetyState == SAFETY_HIGHTEMP);
    bool tankFault = (sys.safetyState == SAFETY_SENSOR_FAULT &&
                      (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0);
    bool exhaustFallback = sys.exhaustFallbackActive;
    bool guardian = (sys.emberGuardianLatched ||
                     sys.burnState == BURN_EMBER_GUARD);

    if (highTemp != prevHighTemp) {
        pushSend(highTemp ? "Boiler: HIGH TEMP LOCKOUT" : "Boiler: High temp cleared",
                 highTemp ? "Tank over-temperature - system stopped" : "High temperature cleared",
                 highTemp);
        prevHighTemp = highTemp;
    }
    if (tankFault != prevTankFault) {
        pushSend(tankFault ? "Boiler: TANK PROBE FAULT" : "Boiler: Tank fault cleared",
                 tankFault ? "Tank probe fault - system stopped" : "Tank probe fault cleared",
                 tankFault);
        prevTankFault = tankFault;
    }
    if (exhaustFallback != prevExhaustFallback) {
        pushSend(exhaustFallback ? "Boiler: EXHAUST PROBE FAULT" : "Boiler: Exhaust probe cleared",
                 exhaustFallback ? "Exhaust probe needs cleaning - fan at max"
                                   : "Exhaust probe fault cleared",
                 exhaustFallback);
        prevExhaustFallback = exhaustFallback;
    }
    if (guardian != prevGuardian) {
        pushSend(guardian ? "Boiler: EMBER GUARDIAN" : "Boiler: Ember Guardian cleared",
                 guardian ? "Ember Guardian active - reset required" : "Ember Guardian cleared",
                 guardian);
        prevGuardian = guardian;
    }
}

bool pushnotify_sendTest() {
    return pushSend("Boiler: TEST", "Test notification from Boiler Assistant", false);
}
