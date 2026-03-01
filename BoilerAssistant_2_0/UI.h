/*
 * ============================================================
 *  Boiler Assistant – User Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: UI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the LCD UI and keypad‑driven
 *      navigation system. Defines:
 *        • UI state machine (menus, editors, system screens)
 *        • Rendering entry point for all screens
 *        • Key handling for all editable parameters
 *
 *  v2.0 Notes:
 *      - Supports BOOST, RAMP, HOLD, COALBED, SAFETY displays
 *      - Integrated stability timer readouts
 *      - Non‑blocking boot animation support
 *      - RAM‑safe rendering via lcd4()
 *
 * ============================================================
 */

#ifndef UI_H
#define UI_H

#include <Arduino.h>

enum UIState {
    UI_HOME = 0,
    UI_SETPOINT,
    UI_BOOSTTIME,
    UI_SYSTEM,
    UI_DEADBAND,
    UI_CLAMP_MENU,
    UI_CLAMP_MIN,
    UI_CLAMP_MAX,
    UI_COALBED_TIMER,
    UI_FLUE_LOW,
    UI_FLUE_REC
};

void ui_init();
void ui_showScreen(int state, double exhaustF, int fanPercent);
void ui_handleKey(char k, double exhaustF, int fanPercent);

#endif
