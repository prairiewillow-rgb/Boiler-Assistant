/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry API (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: LoRaRadio.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the LoRa telemetry + command module.
 *      Provides:
 *          • lora_init() — initialize LoRa radio hardware
 *          • lora_loop() — non‑blocking RX/TX handler
 *
 *      Responsibilities:
 *          - Broadcast compact 16‑byte telemetry packets
 *          - Receive CRC‑validated command packets
 *          - Update SystemData fields from remote commands
 *          - Maintain strict real‑time safety (no blocking calls)
 *
 *      Notes:
 *          - All packet encoding/decoding implemented in LoRaRadio.cpp
 *          - Telemetry interval is fixed at 2 seconds
 *          - Fully compatible with SystemData v2.3‑Environmental
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
 * ============================================================
 */

#ifndef LORA_RADIO_H
#define LORA_RADIO_H

// Initialize LoRa radio hardware
void lora_init();

// Non‑blocking RX/TX loop (called from main loop)
void lora_loop();

#endif
