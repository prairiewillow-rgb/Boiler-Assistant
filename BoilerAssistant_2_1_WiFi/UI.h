/*
 * ============================================================
 *  Boiler Assistant – UI Module (v2.1 – Ember Guardian)
 *  ------------------------------------------------------------
 *  File: UI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the LCD‑based operator UI.
 *      Responsibilities:
 *        • Initialize LCD hardware and boot screen
 *        • Render screens based on UIState
 *        • Process keypad input for all UI modes
 *
 *      Notes:
 *        - UIState enum is defined in SystemState.h
 *        - Rendering is stateless; all data comes from SystemData
 *        - Keypad input is fully non‑blocking
 *
 *  Version:
 *      Boiler Assistant v2.1 – Ember Guardian Edition
 * ============================================================
 */

#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "SystemState.h"

/* ============================================================
 *  PUBLIC UI API
 * ============================================================ */

// Initialize LCD, boot screen, and initial UI state
void ui_init();

// Draw the correct screen for the given UI state
void ui_showScreen(UIState st, double exhaustF, int fanPercent);

// Handle keypad input for all UI states
void ui_handleKey(char k, double exhaustF, int fanPercent);

#endif

