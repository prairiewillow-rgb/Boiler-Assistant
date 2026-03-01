/*
 * ============================================================
 *  Boiler Assistant – Keypad Interface Module (v2.0)
 *  ------------------------------------------------------------
 *  File: Keypad.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Direct‑wired 4×4 matrix keypad scanner with:
 *          • UNO‑safe precharge timing
 *          • Debounced ASCII key output
 *          • Boot‑safe state reset (prevents keypad lockup)
 *          • Stable, noise‑resistant scanning for v2.0 UI
 *
 *  v2.0 Notes:
 *      - Integrated with non‑blocking boot screen timing
 *      - Ensures keypad cannot interfere with UI boot animation
 *      - Debounce logic preserved and tuned for v2.0 responsiveness
 *      - Fully compatible with expanded UI state machine
 *
 * ============================================================
 */

#include "Keypad.h"
#include "Pinout.h"
#include <Arduino.h>

// ============================================================
//  Key Mapping Table
// ============================================================
static const char KEYS[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// ============================================================
//  keypad_init()
// ============================================================
void keypad_init() {
  pinMode(PIN_KEYPAD_ROW1, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_ROW2, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_ROW3, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_ROW4, INPUT_PULLUP);

  pinMode(PIN_KEYPAD_COL1, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_COL2, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_COL3, INPUT_PULLUP);
  pinMode(PIN_KEYPAD_COL4, INPUT_PULLUP);
}

// ============================================================
//  scanKeypad() – raw matrix scan
// ============================================================
static char scanKeypad() {
  const int rows[4] = {PIN_KEYPAD_ROW1, PIN_KEYPAD_ROW2, PIN_KEYPAD_ROW3, PIN_KEYPAD_ROW4};
  const int cols[4] = {PIN_KEYPAD_COL1, PIN_KEYPAD_COL2, PIN_KEYPAD_COL3, PIN_KEYPAD_COL4};

  char found = 0;

  for (int r = 0; r < 4; r++) {

    // Reset all rows
    for (int i = 0; i < 4; i++) {
      pinMode(rows[i], INPUT_PULLUP);
      digitalWrite(rows[i], HIGH);
    }

    // Precharge + drive row LOW
    digitalWrite(rows[r], HIGH);
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], LOW);

    delayMicroseconds(300);

    // Scan columns
    for (int c = 0; c < 4; c++) {
      if (digitalRead(cols[c]) == LOW) {
        delayMicroseconds(80);
        if (digitalRead(cols[c]) == LOW)
          found = KEYS[r][c];
      }
    }
  }

  return found;
}

// ============================================================
//  getKey() – debounced, boot‑safe
// ============================================================
char getKey() {
  static char lastKey = 0;
  static char stableKey = 0;
  static unsigned long lastChange = 0;

  unsigned long now = millis();

  // ------------------------------------------------------------
  // BOOT‑SAFE PROTECTION
  // Ignore keypad for first 4 seconds of boot.
  // Prevents state machine lockup during UI boot animation.
  // ------------------------------------------------------------
  if (now < 4000) {
    lastKey = 0;
    stableKey = 0;
    return 0;
  }

  char k = scanKeypad();

  // No key pressed
  if (k == 0) {
    stableKey = 0;
    lastKey = 0;
    return 0;
  }

  // Debounce: new key detected
  if (k != stableKey) {
    stableKey = k;
    lastChange = now;
    return 0;
  }

  // Debounce timing
  if (now - lastChange < 40)
    return 0;

  // Already returned this key
  if (k == lastKey)
    return 0;

  lastKey = k;
  return toupper(k);
}
