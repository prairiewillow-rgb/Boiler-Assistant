/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry Module (v2.0)
 *  ------------------------------------------------------------
 *  File: LoRaRadio.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      LoRa interface for long‑range telemetry and remote
 *      control. Implements:
 *        • 12‑byte telemetry packet encoder
 *        • Command packet decoder
 *        • Two‑way sync with SystemData
 *
 * ============================================================
 */

#include "LoRaRadio.h"
#include "SystemData.h"
#include <LoRa.h>

extern SystemData sys;

static void lora_sendTelemetry();
static void lora_handleCommand(uint8_t* pkt, uint8_t len);

void lora_init() {
    LoRa.begin(915E6);
}

void lora_loop() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        uint8_t buf[32];
        int len = LoRa.readBytes(buf, packetSize);
        lora_handleCommand(buf, len);
    }

    lora_sendTelemetry();
}

static void lora_sendTelemetry() {
    uint8_t pkt[12];

    uint16_t ex = sys.exhaustSmoothF * 10;
    pkt[0] = ex >> 8;
    pkt[1] = ex & 0xFF;

    pkt[2] = sys.fanFinal;
    pkt[3] = sys.burnState;

    uint16_t t = sys.envTempF * 10;
    pkt[4] = t >> 8;
    pkt[5] = t & 0xFF;

    LoRa.beginPacket();
    LoRa.write(pkt, 12);
    LoRa.endPacket();
}

static void lora_handleCommand(uint8_t* pkt, uint8_t len) {
    uint8_t cmd = pkt[0];
    uint16_t value = (pkt[1] << 8) | pkt[2];

    if (cmd == 0x01) { // set exhaust setpoint
        sys.exhaustSetpoint = value;
        sys.remoteChanged = true;
    }
}
