/*
 * Boiler Assistant - Optional SD card driver (v3.4.0 "Total Domination")
 * Software SPI on dedicated pins (Pinout.h); never shares the MAX31855 bus.
 * Provides the FatFs disk interface. Every card wait is time-limited.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#ifndef SD_CARD_H
#define SD_CARD_H

#include <Arduino.h>

// Card is SDHC/SDXC (block addressed) or SDSC (byte addressed); 0 = not initialized.
uint8_t sdcard_type();
// Next mount retries the SPI reset handshake; it cannot power-cycle the module.
void sdcard_invalidate();
// SdFat error code from the most recent failed card init (0 = last init succeeded).
uint8_t sdcard_initError();
// Unrecovered sector-read error for the current mount attempt.
uint8_t sdcard_readError();

struct SdBootRead {
    uint32_t sector;
    uint16_t signature;
    bool attempted;
    bool ok;
    uint8_t error;
};

void sdcard_beginBootTrace();
void sdcard_endBootTrace();
const SdBootRead& sdcard_bootRead(bool partition);

#endif
