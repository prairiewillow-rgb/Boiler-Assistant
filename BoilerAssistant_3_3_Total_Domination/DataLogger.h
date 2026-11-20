/*
 * Boiler Assistant - Optional SD data logger (v3.4.0 "Total Domination")
 * One CSV file per day (30-second rows with alarms and events mixed in) and one row per completed burn period,
 * written in RAM-bounded batches (up to five minutes, sooner at capacity)
 * to a FAT or exFAT micro-SD card (512-byte sectors; MBR or unpartitioned).
 * Resets can lose pending data. The controller runs normally
 * without a card; logging never feeds back into control.
 * HISTORY stores 15-second/damper-change snapshots and completed Burn Times.
 * Watchdog resets queue restored WD_SETTINGS events into the probe DATA CSV.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include "BurnPerformance.h"
#include "JsonConfig.h"

void datalogger_init();
void datalogger_loop(unsigned long now);
void datalogger_recordBurn(const BurnRecord& record);
// Queues only the just-completed period; SD I/O runs later in datalogger_loop.
void datalogger_recordHistoryBurn(uint32_t durationSec, uint32_t intervalSec);
String datalogger_historyJson(bool performance = false);
void datalogger_buildHistoryJson(JsonDocument& doc, bool performance = false);
void datalogger_buildStatusJson(JsonDocument& doc);
void datalogger_buildListJson(JsonDocument& doc, const char* dir, const char* before);

String datalogger_statusJson();
// True while the card is mounted and rows are being written.
bool datalogger_ready();
// dir: DATA, BURNS or HISTORY. before: optional name bound for paging.
String datalogger_listJson(const char* dir, const char* before);

// Download streaming (one file at a time). path such as "DATA/20261016.CSV".
// Downloads contain synchronized rows only; pending RAM batches are not forced.
// size returns remaining bytes after start; start=0 preserves full downloads.
bool datalogger_validPath(const char* path);
bool datalogger_openDownload(const char* path, uint32_t& size, uint32_t start = 0);
int datalogger_readDownload(uint8_t* buffer, uint16_t length);  // -2: yield; -1: error
bool datalogger_downloadBusy();
void datalogger_closeDownload();

#endif
