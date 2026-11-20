/*
 * Boiler Assistant - UI
 * Author: The Architect Collective
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
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
 * @param key        The key pressed ('A', 'B', 'C', 'D', '0'-'9', '*', '#')
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
