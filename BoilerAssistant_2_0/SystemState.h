/*
 * ============================================================
 *  Boiler Assistant – System State Definitions (v2.0)
 *  ------------------------------------------------------------
 *  File: SystemState.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Core burn‑phase state definitions for the Boiler Assistant.
 *      Defines the BurnState enum used by:
 *        • BurnEngine (phase logic + timers)
 *        • FanControl (BOOST/SAFETY overrides)
 *        • UI (phase display + stability timers)
 *        • SystemState (startup/reset behavior)
 *
 *  v2.0 Notes:
 *      - BOOST phase formalized as a first‑class state
 *      - SAFETY state integrated across all modules
 *      - Enum kept minimal and deterministic for embedded use
 *
 * ============================================================
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

// ============================================================
//  Burn Phases
// ============================================================
enum BurnState {
    BURN_RAMP = 0,
    BURN_HOLD,
    BURN_IDLE,
    BURN_COALBED,
    BURN_BOOST,     // BOOST PHASE
    BURN_SAFETY
};

#endif
