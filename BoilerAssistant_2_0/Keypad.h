/*
 * ============================================================
 *  Boiler Assistant – Keypad Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: Keypad.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the 4×4 matrix keypad scanner.
 *      Provides:
 *        • Hardware initialization (row/col pullups)
 *        • Debounced ASCII key retrieval
 *        • Boot‑safe lockout to protect UI startup
 *
 *  v2.0 Notes:
 *      - Fully compatible with non‑blocking UI boot sequence
 *      - Ensures keypad cannot interfere during boot animation
 *      - Stable, noise‑resistant key detection for v2.0 UI flow
 *
 * ============================================================
 */

#ifndef KEYPAD_H
#define KEYPAD_H

#include <Arduino.h>

// Initialize keypad hardware (row/col pins, pullups)
void keypad_init();

// Return a single ASCII key:
//   '0'–'9', 'A'–'D', '*', '#'
// Debounced, boot‑safe, returns 0 when no key is pressed.
char getKey();

#endif
