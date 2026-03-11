/*
 * ============================================================
 *  Boiler Assistant – UI Module (v2.3‑Environmental)
 *  ------------------------------------------------------------
 *  File: UI.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *
 *  Description:
 *      Public interface for the 20×4 LCD‑based operator UI.
 *      Responsibilities:
 *        • Initialize LCD hardware and display boot sequence
 *        • Render screens based on the active UIState
 *        • Process keypad input for all UI modes
 *
 *      Notes:
 *        - UIState enum is defined in SystemState.h
 *        - Rendering is stateless; all data is supplied externally
 *        - Keypad input is fully non‑blocking
 *
 *  Version:
 *      Boiler Assistant v2.3‑Environmental
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

// Render the correct screen for the given UI state
void ui_showScreen(UIState st, double exhaustF, int fanPercent);

// Handle keypad input for all UI states
void ui_handleKey(char k, double exhaustF, int fanPercent);

#endif

