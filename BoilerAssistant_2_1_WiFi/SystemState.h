/*
 * ============================================================
 *  Boiler Assistant – System State Definitions (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: SystemState.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Core enumerations and constants defining:
 *        • BurnEngine state machine
 *        • UI navigation states
 *        • Water probe role assignments
 *        • Maximum supported DS18B20 probes
 *
 *      These enums form the backbone of the system’s logic,
 *      ensuring consistent state transitions across UI,
 *      BurnEngine, telemetry, and remote control interfaces.
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

/* ============================================================
 *  BURN ENGINE STATES
 * ============================================================ */

enum BurnState {
    BURN_IDLE = 0,
    BURN_RAMP,
    BURN_HOLD,
    BURN_BOOST,
    BURN_SAFETY,
    BURN_EMBER_GUARD
};

/* ============================================================
 *  UI STATES
 * ============================================================ */

enum UIState {
    UI_HOME = 0,
    UI_SETPOINT,
    UI_BOOSTTIME,
    UI_SYSTEM,
    UI_DEADBAND,
    UI_CLAMP_MENU,
    UI_CLAMP_MIN,
    UI_CLAMP_MAX,
    UI_COAL_SAVER_TIMER,
    UI_FLUE_LOW,
    UI_FLUE_REC,
    UI_WATER_MENU,
    UI_WATER_PROBE_MENU,
    UI_BME_SCREEN,
    UI_NETWORK_INFO
};

/* ============================================================
 *  WATER PROBE ROLES (8 roles, assignable)
 * ============================================================ */

enum WaterProbeRole {
    PROBE_TANK = 0,     // Main tank
    PROBE_L1_SUPPLY,
    PROBE_L1_RETURN,
    PROBE_L2_SUPPLY,
    PROBE_L2_RETURN,
    PROBE_L3_SUPPLY,
    PROBE_L3_RETURN,
    PROBE_SPARE,
    PROBE_ROLE_COUNT
};

/* ============================================================
 *  MAX WATER PROBES
 * ============================================================ */

#define MAX_WATER_PROBES 8

#endif
