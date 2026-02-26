/*
 * ============================================================
 *  Boiler Assistant – Keypad Interface Module (v1.3.1)
 *  ------------------------------------------------------------
 *  File: Keypad.cpp
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Implements the 4×4 matrix keypad scanner for the Boiler
 *      Assistant firmware. Provides stable, debounced ASCII key
 *      input for the UI state machine.
 *
 *  Responsibilities:
 *      • Configure keypad GPIO pins
 *      • Perform UNO R4–safe matrix scanning with precharge timing
 *      • Debounce keypresses
 *      • Return a single stable ASCII key per press
 *
 *  Architecture Notes:
 *      • scanKeypad() performs raw matrix scanning (no debounce)
 *      • getKey() provides debounced, non‑blocking key retrieval
 *      • Returns 0 when no key is pressed
 *      • Keys are normalized to uppercase
 *
 * ============================================================
 */

#include "Keypad.h"
#include "Pinout.h"
#include <Arduino.h>


// ============================================================
//  SECTION: Key Mapping Table
// ============================================================

/**
 * KEYS
 * ------------------------------------------------------------
 * Lookup table mapping row/column positions to ASCII characters.
 *
 * Layout:
 *      1 2 3 A
 *      4 5 6 B
 *      7 8 9 C
 *      * 0 # D
 */
static const char KEYS[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};


// ============================================================
//  keypad_init()
// ============================================================

/**
 * keypad_init()
 * ------------------------------------------------------------
 * Configures all keypad row and column pins as inputs with
 * internal pull‑ups. Actual scanning is performed by scanKeypad().
 *
/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v1.3)
 *  ------------------------------------------------------------
 *  File: FanControl.h
 *  Author: The Architect Collective
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface for the fan shaping and damper relay
 *      control module. FanControl applies:
 *
 *          • BOOST override
 *          • Clamp min/max
 *          • Deadzone mode
 *          • PWM output
 *          • Damper relay logic
 *
 *      BurnLogic computes the *raw* fan %, and FanControl
 *      applies all shaping and hardware output.
 *
 * ============================================================
 */

#pragma once
#include <Arduino.h>

// ============================================================
//  Initialization
// ============================================================
void fancontrol_init();

// ============================================================
//  BOOST Mode Handler
// ============================================================
void fancontrol_updateBoost();

// ============================================================
//  Apply shaping + PWM + damper relay
//  Returns final applied fan percentage (0–100)
// ============================================================
int fancontrol_apply(int rawFanPercent);

