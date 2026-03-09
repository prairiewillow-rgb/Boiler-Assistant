/*
 * ============================================================
 *  Boiler Assistant – Shared System Data (v2.2)
 *  ------------------------------------------------------------
 *  File: SystemData.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Defines the global SystemData instance used across the
 *      entire firmware. This structure holds:
 *        • Live sensor values
 *        • BurnEngine state variables
 *        • Fan control outputs
 *        • Water probe temperatures + roles
 *        • Remote‑change flags for MQTT/LoRa
 *        • All operator‑adjustable settings
 *
 *      Notes:
 *        - SystemData is a pure data container (no logic).
 *        - All modules read/write fields directly.
 *        - EEPROMStorage handles persistence.
 *
 *  Version:
 *      Boiler Assistant v2.2
 * ============================================================
 */

#include "SystemData.h"

// Global instance
SystemData sys;
