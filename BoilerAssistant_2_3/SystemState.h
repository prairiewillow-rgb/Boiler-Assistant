/*
 * ============================================================
 *  Boiler Assistant – System State Definitions (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: SystemState.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized enumerations and shared state definitions
 *      used across the entire Boiler Assistant firmware.
 *
 *      This module defines:
 *        • Burn Engine state machine
 *        • UI navigation state machine (v2.3 unified seasonal workflow)
 *        • Water probe role mapping
 *        • Environmental season enums (AUTO logic)
 *
 *      These enums form the backbone of inter‑module communication
 *      and must remain stable across firmware versions.
 *
 *  v2.3‑Environmental Notes:
 *      - UI state machine expanded for unified seasonal editing.
 *      - EnvSeason and EnvSeasonMode updated for AUTO logic.
 *      - No behavioral logic lives here — only definitions.
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
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
 *  UI STATES (v2.3 – Unified Seasonal Workflow)
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
    UI_EMBER_GUARD_TIMER,
    UI_FLUE_LOW,
    UI_FLUE_REC,

    UI_WATER_MENU,
    UI_WATER_PROBE_MENU,
    UI_BME_SCREEN,
    UI_NETWORK_INFO,

    // --- ENVIRONMENT ROOT ---
    UI_ENV_MENU,

    // --- UNIFIED SEASON UI ---
    UI_SEASONS_MENU,         // List of seasons (Summer / SPF / Winter / Extreme)
    UI_SEASON_DETAIL_MENU,   // Unified menu: Start / Buffer / Setpoint
    UI_SEASON_EDIT_START,    // Edit season start temperature
    UI_SEASON_EDIT_BUFFER,   // Edit season buffer (formerly hysteresis)
    UI_SEASON_EDIT_SETPOINT, // Edit season setpoint

    // --- LOCKOUT / MODE / AUTO ---
    UI_ENV_LOCKOUT,
    UI_ENV_MODE,
    UI_ENV_AUTOSEASON,
    UI_ENV_LOCKOUT_HOURS
};

/* ============================================================
 *  WATER PROBE ROLES
 * ============================================================ */

enum WaterProbeRole {
    PROBE_TANK = 0,
    PROBE_L1_SUPPLY,
    PROBE_L1_RETURN,
    PROBE_L2_SUPPLY,
    PROBE_L2_RETURN,
    PROBE_L3_SUPPLY,
    PROBE_L3_RETURN,
    PROBE_SPARE,
    PROBE_ROLE_COUNT
};

#define MAX_WATER_PROBES 8

/* ============================================================
 *  ENVIRONMENTAL LOGIC ENUMS (v2.3)
 * ============================================================ */

enum EnvSeason {
    ENV_SEASON_SUMMER = 0,
    ENV_SEASON_SPRINGFALL,
    ENV_SEASON_WINTER,
    ENV_SEASON_EXTREME,
    ENV_SEASON_NONE
};

enum EnvSeasonMode {
    ENV_MODE_OFF  = 0,
    ENV_MODE_USER = 1,
    ENV_MODE_AUTO = 2
};

#endif // SYSTEM_STATE_H
