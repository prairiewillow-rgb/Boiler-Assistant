/*
 * ============================================================
 *  Boiler Assistant â€“ UI API (v3.3.4 "Total Domination")
 *  ------------------------------------------------------------
 *  File: UI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the keypadâ€‘driven LCD UI subsystem.
 *    This module exposes deterministic entry points for:
 *
 *      â€¢ ui_init()       â€” initialize LCD + boot screen
 *      â€¢ ui_handleKey()  â€” process keypad input and update UI state
 *      â€¢ ui_showScreen() â€” render the active UI state
 *
 *    Architectural Notes:
 *      - All UI state definitions live in SystemState.h
 *      - No sensor, burn engine, or MQTT logic belongs here
 *      - Rendering is strictly operatorâ€‘facing and deterministic
 *
 *  Version:
 *      Boiler Assistant v3.3.4 "Total Domination"
 * ============================================================
 */

#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "SystemState.h"   // UIState enum lives here

/* ============================================================
 *  PUBLIC UI FUNCTIONS
 * ============================================================ */

/**
 * Initialize LCD, boot screen, and UI state.
 */
void ui_init();

/**
 * Handle keypad input and update UI state.
 *
 * @param key        The key pressed ('A', 'B', 'C', 'D', '0'â€“'9', '*', '#')
 * @param exhaustF   Current exhaust temperature
 * @param fanPercent Current fan percentage
 */
void ui_handleKey(char key, double exhaustF, int fanPercent);

/**
 * Render the screen for the given UI state.
 *
 * @param st         UI state to render
 * @param exhaustF   Current exhaust temperature
 * @param fanPercent Current fan percentage
 */
void ui_showScreen(UIState st, double exhaustF, int fanPercent);

#endif // UI_H
