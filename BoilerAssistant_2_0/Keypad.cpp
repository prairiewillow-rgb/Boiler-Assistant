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

static const char KEYS[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

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
//  Non‑blocking scan timer
// ============================================================
static unsigned long lastScanMs = 0;
static char lastRaw = 0;

// ============================================================
//  Raw matrix scan with UNO R4 D3 stabilization
// ============================================================
static char scanMatrix() {
  const int rows[4] = {PIN_KEYPAD_ROW1, PIN_KEYPAD_ROW2, PIN_KEYPAD_ROW3, PIN_KEYPAD_ROW4};
  const int cols[4] = {PIN_KEYPAD_COL1, PIN_KEYPAD_COL2, PIN_KEYPAD_COL3, PIN_KEYPAD_COL4};

  char found = 0;

  for (int r = 0; r < 4; r++) {

    // Reset all rows
    for (int i = 0; i < 4; i++) {
      pinMode(rows[i], INPUT_PULLUP);
      digitalWrite(rows[i], HIGH);
    }

    // Drive selected row LOW
    digitalWrite(rows[r], HIGH);
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], LOW);

    // ============================================================
    // UNO R4 WiFi FIX:
    // D3 (ROW2) requires longer settle time due to RA4M1 GPIO behavior.
    // ============================================================
    if (rows[r] == PIN_KEYPAD_ROW2) {
      delayMicroseconds(600);   // extended settle time for D3
    } else {
      delayMicroseconds(300);   // normal settle time
    }

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
//  getKey() – non‑blocking, debounced
// ============================================================
char getKey() {
  static char stableKey = 0;
  static char lastReturned = 0;
  static unsigned long lastChange = 0;

  unsigned long now = millis();

  // Boot‑safe: ignore keypad for first 4 seconds
  if (now < 4000) {
    stableKey = 0;
    lastReturned = 0;
    lastRaw = 0;
    return 0;
  }

  // Only scan every 5 ms
  if (now - lastScanMs >= 5) {
    lastScanMs = now;
    lastRaw = scanMatrix();
  }

  char k = lastRaw;

  if (k == 0) {
    stableKey = 0;
    lastReturned = 0;
    return 0;
  }

  if (k != stableKey) {
    stableKey = k;
    lastChange = now;
    return 0;
  }

  if (now - lastChange < 40)
    return 0;

  if (k == lastReturned)
    return 0;

  lastReturned = k;
  return toupper(k);
}
