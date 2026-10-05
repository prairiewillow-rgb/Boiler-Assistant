/*
 * ============================================================
 *  Boiler Assistant - Keypad I2C Driver (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Keypad_I2C.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Checked, rate-limited I2C scanning of the 4x4 PCF8574 keypad.
 *    Failed transfers suspend input until recovery and a stable
 *    release, preventing phantom key events after a bus reset.
 *
 *    Features:
 *      - 4x4 matrix scan (rows driven low, columns read)
 *      - Debounce filtering (15 ms stable requirement)
 *      - Stable key reporting (no repeats until release)
 *      - Zero blocking delays (only us-level settling)
 *      - Five-second device retry after transfer failure
 *
 *    Notes:
 *      - scanMatrix() performs raw hardware scanning
 *      - keypad_read() applies debounce + stable reporting
 *      - No dynamic allocation or Strings
 *      - Scan/debounce/retry cadence uses millis(); individual Wire
 *        transactions are synchronous with the shared-bus timeout
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#include "Keypad_I2C.h"
#include "I2CBus.h"

#define KEYPAD_ADDR 0x20

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static TwoWire *kb = nullptr;

static const char keymap[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static char lastStableKey     = 0;
static char lastReportedKey   = 0;
static unsigned long lastChangeTime = 0;
static const unsigned long DEBOUNCE_MS = 15UL;
static const unsigned long SCAN_INTERVAL_MS = 5UL;
static const unsigned long RETRY_MS = 5000UL;
static unsigned long lastScanMs = 0;
static unsigned long lastFailureMs = 0;
static bool scanFailed = false;
static bool waitingForRelease = true;
static uint32_t busGeneration = 0;

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void keypad_init(TwoWire &bus) {
    kb = &bus;
    lastStableKey = lastReportedKey = 0;
    lastChangeTime = millis();
    lastScanMs = millis() - SCAN_INTERVAL_MS;
    scanFailed = false;
    waitingForRelease = true;
    busGeneration = i2cbus_generation();
}

/* ============================================================
 *  RAW MATRIX SCAN (no debounce)
 * ============================================================
 *  Returns:
 *      - true on a complete scan; key holds the pressed character or 0
 *      - false on any failed row write, column read, or idle restore
 *
 *  Behavior:
 *      - Drives one row LOW at a time
 *      - Reads column bits from expander
 *      - 300 us settling delay ensures stable read
 * ============================================================ */

static bool writeRows(uint8_t mask) {
    kb->beginTransmission(KEYPAD_ADDR);
    kb->write(mask);
    return kb->endTransmission() == 0;
}

static bool scanMatrix(char &key) {
    key = 0;
    if (!kb) return false;

    for (int row = 0; row < 4; row++) {

        uint8_t rowMask = ~(1 << row);  // active-low row drive

        if (!writeRows(rowMask)) return false;

        delayMicroseconds(300);  // settling time

        if (kb->requestFrom(KEYPAD_ADDR, 1) != 1 || !kb->available()) return false;

        uint8_t colData = kb->read();

        for (int col = 0; col < 4; col++) {
            if (!(colData & (1 << (col + 4)))) {
                key = keymap[row][col];
                return writeRows(0xFF);
            }
        }
    }

    return writeRows(0xFF);
}

/* ============================================================
 *  DEBOUNCED KEYPAD READ
 * ============================================================
 *  Returns:
 *      - A single keypress event (char)
 *      - 0 when no new key is ready
 *
 *  Behavior:
 *      - Requires 15 ms of stable key state
 *      - Prevents repeats until key is released
 *      - Tracks last stable and last reported keys
 * ============================================================ */

char keypad_read() {
    unsigned long now = millis();
    if (!kb || !i2cbus_ready()) return 0;
    if (busGeneration != i2cbus_generation()) {
        busGeneration = i2cbus_generation();
        lastStableKey = lastReportedKey = 0;
        lastChangeTime = now;
        waitingForRelease = true;
    }
    if (scanFailed && now - lastFailureMs < RETRY_MS) return 0;
    if (now - lastScanMs < SCAN_INTERVAL_MS) return 0;
    lastScanMs = now;

    char rawKey = 0;
    if (!scanMatrix(rawKey)) {
        if (!scanFailed) Serial.println("Keypad: I2C scan failed; input suspended");
        scanFailed = true;
        lastFailureMs = now;
        lastStableKey = lastReportedKey = 0;
        lastChangeTime = now;
        waitingForRelease = true;
        i2cbus_requestRecovery();
        return 0;
    }
    if (scanFailed) {
        Serial.println("Keypad: I2C scan restored; release keys to resume");
        scanFailed = false;
    }

    // Detect change in raw key state
    if (rawKey != lastStableKey) {
        lastStableKey = rawKey;
        lastChangeTime = now;
    }

    // Key pressed and stable
    // Never turn an interrupted scan or a held key during recovery into an action.
    if (waitingForRelease) {
        if (rawKey == 0 && now - lastChangeTime > DEBOUNCE_MS) {
            waitingForRelease = false;
        }
        return 0;
    }

    if (rawKey != 0 && (now - lastChangeTime) > DEBOUNCE_MS) {
        if (rawKey != lastReportedKey) {
            lastReportedKey = rawKey;
            return rawKey;
        }
    }

    // Key released and stable
    if (rawKey == 0 && lastReportedKey != 0 && (now - lastChangeTime) > DEBOUNCE_MS) {
        lastReportedKey = 0;
    }

    return 0;
}
