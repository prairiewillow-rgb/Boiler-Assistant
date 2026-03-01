/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry Module (v2.0)
 *  ------------------------------------------------------------
 *  File: LoRaRadio.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      LoRa telemetry + command interface. Provides:
 *        • Compact binary telemetry packets
 *        • Command packet decoder
 *        • Two‑way sync with SystemData
 *
 * ============================================================
 */

#ifndef LORA_RADIO_H
#define LORA_RADIO_H

void lora_init();
void lora_loop();

#endif
