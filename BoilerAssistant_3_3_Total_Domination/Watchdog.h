/*
 * Boiler Assistant - Watchdog
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

static const uint32_t WATCHDOG_LOOP_LIMIT_MS = 30000UL;
// OTA steps use the modem's long (10-60 s) waits.
static const uint32_t WATCHDOG_SLOW_LIMIT_MS = 120000UL;

// setup()/loop() stack (in .bss, so the heap is this much smaller).
// Interrupts use the core's 1 KB stack.
static const uint16_t WATCHDOG_STACK_BYTES = 1536;

void watchdog_bootReport();     // after Serial.begin()
// After watchdog_begin(), only SysTick may feed the hardware WDT; use watchdog_kick().
void watchdog_begin();          // end of setup()
void watchdog_kick(uint32_t allowanceMs = WATCHDOG_LOOP_LIMIT_MS);
uint16_t watchdog_stackPeak();  // deepest setup()/loop() stack use since boot, bytes

// Loop step being run; reported after a watchdog reset (see WD_STAGE_NAMES).
enum : uint8_t {
    WD_SENSORS = 1, WD_KEYPAD, WD_PROBES, WD_BURN, WD_SDLOG, WD_LINK,
    WD_OTA, WD_HTTP, WD_MQTT, WD_PUSH, WD_SCREEN, WD_PROV, WD_SAVE,
    WD_HTTP_ACCEPT, WD_HTTP_RECEIVE, WD_HTTP_SEND, WD_HTTP_READ, WD_HTTP_PARSE
};
extern volatile uint8_t watchdogStage;
// Also times the step just finished, for the slowest-step report.
void watchdog_stage(uint8_t stage);
const char* watchdog_resetText();       // why the board last restarted
bool watchdog_wasReset();              // this boot, not the cumulative reset count
uint16_t watchdog_resetCount();         // watchdog restarts since power-on
const char* watchdog_slowStage(uint32_t& ms);  // slowest step since boot (OTA excluded)

#endif
