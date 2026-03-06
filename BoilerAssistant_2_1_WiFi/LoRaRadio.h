/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry API (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: LoRaRadio.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the LoRa telemetry + command module.
 *      Provides:
 *        • lora_init() — initialize LoRa radio hardware
 *        • lora_loop() — non‑blocking RX/TX handler
 *
 *      Responsibilities:
 *        - Broadcast compact 16‑byte telemetry packets
 *        - Receive CRC‑validated command packets
 *        - Update SystemData fields from remote commands
 *        - Maintain strict real‑time safety (no blocking calls)
 *
 *      Notes:
 *        - All packet encoding/decoding implemented in LoRaRadio.cpp
 *        - Telemetry interval is fixed at 2 seconds
 *        - Compatible with v2.1 Ember Guardian architecture
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef LORA_RADIO_H
#define LORA_RADIO_H

// Initialize LoRa radio hardware
void lora_init();

// Non‑blocking RX/TX loop (called from main loop)
void lora_loop();

#endif
