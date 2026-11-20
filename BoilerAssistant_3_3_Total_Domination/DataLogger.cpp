/*
 * Boiler Assistant - DataLogger
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#include "DataLogger.h"
#include "DataShare.h"
#include "DebugLog.h"
#include "Pinout.h"
#include "SdCard.h"
#include "SystemData.h"
#include "Sensors.h"
#include "WiFiAPI.h"
#include "WiFiProvisioning.h"
#include "Version.h"
#include "Watchdog.h"
#include "WaterCalibration.h"
#include "src/fatfs/ff.h"
#include "src/fatfs/diskio.h"
#include "JsonConfig.h"
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

namespace {

/* ---------------- Time ---------------- */

constexpr uint32_t CLOCK_HOLD_SEC = 30UL * 86400UL;
uint32_t anchorUtc = 0;
unsigned long anchorMs = 0;

struct Civil {
    int16_t year;
    uint8_t month, day, hour, minute, second;
};

// Keeps the last network time running from millis() for up to 30 days,
// so daily files stay dated through WiFi outages longer than 24 h.
uint32_t nowUtc() {
    unsigned long utc = wifi_prov_cachedUTC();
    unsigned long ms = millis();
    if (utc) {
        anchorUtc = utc;
        anchorMs = ms;
        return utc;
    }
    if (!anchorUtc) return 0;
    uint32_t elapsed = (ms - anchorMs) / 1000UL;
    if (elapsed > CLOCK_HOLD_SEC) return 0;
    return anchorUtc + elapsed;
}

Civil toCivil(int64_t seconds) {
    int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    int32_t rem = static_cast<int32_t>(seconds - days * 86400);
    Civil c;
    c.hour = rem / 3600;
    c.minute = (rem / 60) % 60;
    c.second = rem % 60;
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp < 10 ? mp + 3 : mp - 9;
    c.year = static_cast<int16_t>(y + (c.month <= 2));
    return c;
}

Civil localCivil(uint32_t utc) {
    int64_t offset = static_cast<int64_t>(sys.selfCleanUtcOffsetMinutes) * 60 +
                     (sys.selfCleanDstEnabled ? 3600 : 0);
    return toCivil(static_cast<int64_t>(utc) + offset);
}

/* ---------------- Text output ---------------- */

struct Out {
    char* p;
    size_t cap;
    size_t len;
};

void put(Out& o, const char* s) {
    while (*s && o.len + 1 < o.cap) o.p[o.len++] = *s++;
    o.p[o.len] = '\0';
}

void putChar(Out& o, char c) {
    if (o.len + 1 < o.cap) o.p[o.len++] = c;
    o.p[o.len] = '\0';
}

// Digits by hand: snprintf here added ~340 B of stack to every SD write.
// width = minimum digits, zero-padded.
void putDigits(Out& o, unsigned long v, uint8_t width) {
    char b[11];
    uint8_t n = 0;
    do {
        b[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v || n < width);
    while (n) putChar(o, b[--n]);
}

void putInt(Out& o, long v) {
    if (v < 0) putChar(o, '-');
    putDigits(o, v < 0 ? 0UL - static_cast<unsigned long>(v) : static_cast<unsigned long>(v), 1);
}

void putUInt(Out& o, unsigned long v) { putDigits(o, v, 1); }

// Fixed-point text without relying on printf float support; NaN = empty field.
void putFixed(Out& o, float v, uint8_t decimals) {
    if (!isfinite(v) || fabsf(v) > 1.0e8f) return;
    long scale = 1;
    for (uint8_t i = 0; i < decimals; ++i) scale *= 10;
    long n = lroundf(v * scale);
    if (n < 0) {
        putChar(o, '-');
        n = -n;
    }
    putUInt(o, static_cast<unsigned long>(n / scale));
    if (!decimals) return;
    putChar(o, '.');
    long frac = n % scale;
    for (long d = scale / 10; d > 0; d /= 10) {
        putChar(o, static_cast<char>('0' + (frac / d) % 10));
    }
}

// Text for inside a quoted CSV field.
void putEscaped(Out& o, const char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c == '\r' || c == '\n') c = ' ';
        if (c == '"') putChar(o, '"');
        putChar(o, c);
    }
}

void putLocal(Out& o, uint32_t utc) {
    if (!utc) return;
    Civil c = localCivil(utc);
    const unsigned long parts[6] = {static_cast<unsigned long>(c.year), c.month, c.day,
                                    c.hour, c.minute, c.second};
    static const char seps[6] = {'-', '-', ' ', ':', ':', '\0'};
    for (uint8_t i = 0; i < 6; ++i) {
        putDigits(o, parts[i], i ? 2 : 4);
        if (seps[i]) putChar(o, seps[i]);
    }
}

// "utc,local,uptime_s," - the first columns of every DATA line.
void putStamp(Out& o, uint32_t utc, unsigned long now) {
    if (utc) putUInt(o, utc);
    putChar(o, ',');
    putLocal(o, utc);
    putChar(o, ',');
    putUInt(o, now / 1000UL);
    putChar(o, ',');
}

}  // namespace

// FatFs file timestamps: local time when known, otherwise 2025-01-01.
extern "C" DWORD get_fattime(void) {
    uint32_t utc = nowUtc();
    if (!utc) return (static_cast<DWORD>(2025 - 1980) << 25) | (1UL << 21) | (1UL << 16);
    Civil c = localCivil(utc);
    if (c.year < 1980 || c.year > 2107) c.year = 2025;
    return (static_cast<DWORD>(c.year - 1980) << 25) | (static_cast<DWORD>(c.month) << 21) |
           (static_cast<DWORD>(c.day) << 16) | (static_cast<DWORD>(c.hour) << 11) |
           (static_cast<DWORD>(c.minute) << 5) | (c.second / 2);
}

#if SD_LOGGING_ENABLED

namespace {

/* ---------------- State ---------------- */

enum State : uint8_t { ST_ABSENT, ST_NOFS, ST_READY, ST_ERROR, ST_FULL };
const char* const STATE_TEXT[] = {"No card", "Filesystem not recognized", "Logging", "Card error - retrying",
                                  "Card full"};

constexpr unsigned long ROW_INTERVAL_MS = 30000UL;
constexpr unsigned long HISTORY_INTERVAL_MS = 30000UL;
constexpr unsigned long FLUSH_INTERVAL_MS = 300000UL;
constexpr unsigned long MOUNT_RETRY_MS = 30000UL;
constexpr unsigned long RETENTION_CHECK_MS = 60000UL;
constexpr unsigned long EVENT_POLL_MS = 1000UL;
constexpr unsigned long PROBE_POLL_MS = 10000UL;
constexpr uint16_t ROW_BUF_SIZE = 1056;
constexpr uint16_t HISTORY_BUF_SIZE = 992;
constexpr uint16_t SD_IO_CHUNK = 512;
constexpr unsigned long SD_STEP_LIMIT_MS = 1000UL;
constexpr uint16_t ROW_MAX = 420;  // longest line: an event with a full detail
constexpr uint8_t EVENTS_PER_MINUTE = 30;
constexpr uint8_t RESTORE_COUNT = 8;
constexpr uint32_t RESTORE_MAX_AGE_SEC = 30UL * 86400UL;
constexpr uint16_t RESTORE_TAIL = 1600;
constexpr uint8_t LIST_MAX = 20;

const char DATA_HEADER[] =
    "D,utc,local,uptime_s,event,state,safety,faults,fan_pct,demand_pct,exh_f,exh_raw_f,exh_sp_f,"
    "exh_ok,tank_f,tank_lo_f,tank_hi_f,clamp_min_pct,clamp_max_pct,mode,"
    "season,out_f,out_rh,out_hpa,w1_f,w2_f,w3_f,w4_f,w5_f,w6_f,w7_f,w8_f,store_btuh,"
    "line1_btuh,line2_btuh,line3_btuh,idle_loss_btuh,flags\r\n";
constexpr uint32_t HISTORY_KEEP_SEC = 36UL * 3600UL;
const char BURN_HEADER[] =
    "S,"
    "end_utc,end_local,duration_s,start_tank_f,end_tank_f,rise_f,tank_lo_f,tank_hi_f,"
    "clamp_min_pct,clamp_max_pct,mode,fan_mode,end_state,settings_changed,complete,"
    "probes_valid,avg_fan_pct,avg_exh_f,peak_exh_f,avg_out_f,boost_s,ramp_s,hold_s\r\n";

State state = ST_ABSENT;
FATFS* fs = nullptr;
char* rowBuf = nullptr;
char* historyBuf = nullptr;
// The logger formats one record at a time and copies it into its batch (or
// writes it) before returning. Both formatters can use the same scratch space.
constexpr uint16_t LINE_SIZE = 400;
constexpr uint16_t DETAIL_SIZE = 320;
static_assert(DETAIL_SIZE <= LINE_SIZE, "SD event scratch must fit the shared buffer");
static_assert(ROW_BUF_SIZE >= 2 * ROW_MAX && HISTORY_BUF_SIZE >= 2 * LINE_SIZE,
              "SD batches must fit at least two maximum-sized records");
char* lineBuf = nullptr;
char* detailBuf = nullptr;
uint16_t rowLen = 0;
uint16_t rowsInBuf = 0;
char rowName[9] = "";
FIL dataFile;
bool dataOpen = false;
char dataName[9] = "";
FIL dlFile;
bool dlOpen = false;
bool dlSeekPending = false;
uint32_t dlStart = 0;
uint16_t historyLen = 0, historyPending = 0;
char historyName[9] = "";
char pendingBurnLine[LINE_SIZE], pendingBurnDay[9] = "";
uint16_t pendingBurnLength = 0;
char nextHistoryName[9] = "", nextRowName[9] = "";
uint16_t historySplit = 0, rowSplit = 0, rowSplitRows = 0;
uint16_t historySplitRows = 0;
unsigned long historyBufferedMs = 0, dataBufferedMs = 0;
bool historyFlushSoon = false, sdWorkedThisPass = false, loggingSuspended = false;
enum WritePhase : uint8_t {
    WRITE_IDLE, WRITE_OPEN, WRITE_END_SEEK, WRITE_END_READ, WRITE_START_SEEK,
    WRITE_HEADER_READ, WRITE_APPEND_SEEK, WRITE_NEWLINE, WRITE_HEADER, WRITE_BURN_HEADER,
    WRITE_INFO, WRITE_PROBES, WRITE_BYTES, WRITE_SYNC
};
WritePhase writePhase = WRITE_IDLE;
bool writingHistory = false, writingBurn = false, preferHistory = false;
FSIZE_t dataOriginalSize = 0;
uint16_t headerOffset = 0;
bool dataHeaderSame = false, dataEndsWithNewline = true;
uint16_t batchLength = 0, batchOffset = 0, batchRows = 0;
uint16_t eventsInBuf = 0, rowSplitEvents = 0, batchEvents = 0;
uint32_t lastSdStepMs = 0, maxSdStepMs = 0, maxDownloadReadMs = 0;

unsigned long lastMountMs = 0, lastRowMs = 0, lastRetentionMs = 0;
uint32_t mountAttempts = 0, mountInitFails = 0;
uint8_t bootMountResult = UINT8_MAX;
unsigned long lastEventPollMs = 0, lastProbePollMs = 0, eventWindowMs = 0;
bool everMounted = false, bootLogged = false, restoreDone = false, snapshotReady = false;
bool clockWasValid = false, flushSoon = false, retentionDue = false;
uint8_t watchdogSettingsStep = 0;
char lastAlert[48] = "";
uint8_t eventsThisMinute = 0;
uint8_t restoredBurns = 0;
uint32_t rowsLogged = 0, rowsLost = 0, eventsLogged = 0, eventsLost = 0;
uint32_t burnsLost = 0, filesDeleted = 0;
uint32_t lastWriteMs = 0, maxWriteMs = 0;
uint32_t lostAtError = 0;
uint64_t logBytes = 0;
char lastError[48] = "";
char lastDropError[sizeof(lastError)] = "";
unsigned long lastHistoryMs = 0;
int8_t lastHistoryDamper = -1;
uint32_t historyRowsLost = 0;
struct PendingHistoryBurn {
    uint32_t utc, duration, interval;
    float tank;
    bool pending;
};
PendingHistoryBurn historyBurn = {};

/* ---------------- Helpers ---------------- */

int nameCmp(const char* a, const char* b) {
    // Undated (N...) files sort before dated ones: treated as oldest.
    bool na = a[0] == 'N', nb = b[0] == 'N';
    if (na != nb) return na ? -1 : 1;
    return strcmp(a, b);
}

void historyDayName(uint32_t utc, char* name);

void setError(const char* where, FRESULT r) {
    snprintf(lastError, sizeof(lastError), "%s (FatFs %d)", where, static_cast<int>(r));
    DBG_PRINT("SD: ");
    DBG_PRINTLN(lastError);
}

bool freeKnown() {
    return fs && fs->free_clst <= fs->n_fatent - 2;
}

uint64_t volumeBytes() {
    return fs ? static_cast<uint64_t>(fs->n_fatent - 2) * fs->csize * 512ULL : 0;
}

uint64_t freeBytes() {
    return freeKnown() ? static_cast<uint64_t>(fs->free_clst) * fs->csize * 512ULL : 0;
}

void dropCard(const char* where, FRESULT r) {
    setError(where, r);
    memcpy(lastDropError, lastError, sizeof(lastDropError));
    const uint8_t readError = r == FR_DISK_ERR ? sdcard_readError() : 0;
    if (readError) {
        const size_t len = strlen(lastDropError);
        snprintf(lastDropError + len, sizeof(lastDropError) - len, "; SdFat 0x%02X",
                 static_cast<unsigned>(readError));
    }
    dataOpen = false;
    dlOpen = false;
    writePhase = WRITE_IDLE;
    rowsLost += rowsInBuf;
    eventsLost += eventsInBuf;
    historyRowsLost += historyPending;
    if (pendingBurnLength) ++burnsLost;
    pendingBurnLength = 0;
    rowLen = rowsInBuf = historyLen = historyPending = 0;
    eventsInBuf = rowSplitEvents = 0;
    rowSplit = rowSplitRows = historySplit = historySplitRows = 0;
    nextRowName[0] = nextHistoryName[0] = '\0';
    loggingSuspended = true;
    if (r == FR_DENIED) retentionDue = true;
    f_mount(nullptr, "", 0);
    sdcard_invalidate();
    state = ST_ERROR;
    lostAtError = rowsLost;
    lastMountMs = millis();
}

bool finishSdStep(unsigned long started, const char* operation, bool enforceLimit = true) {
    lastSdStepMs = millis() - started;
    if (lastSdStepMs > maxSdStepMs) maxSdStepMs = lastSdStepMs;
    sdWorkedThisPass = true;
    if (enforceLimit && (state == ST_READY || state == ST_FULL) && lastSdStepMs > SD_STEP_LIMIT_MS) {
        dropCard(operation, FR_TIMEOUT);
        return false;
    }
    return state == ST_READY || state == ST_FULL;
}

uint64_t dirBytes(const char* path) {
    DIR dir;
    FILINFO info;
    uint64_t total = 0;
    if (f_opendir(&dir, path) != FR_OK) return 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        if (!(info.fattrib & AM_DIR)) total += info.fsize;
    }
    f_closedir(&dir);
    return total;
}

void runRetention(bool force);

// Room in rowBuf for one more line of the day file for utc. A new day
// flushes (or drops) the previous day's lines first.
bool rowSpace(uint32_t utc) {
    char name[9] = "";
    if (utc) historyDayName(utc, name);
    if (rowLen && !rowName[0] && utc) {
        strcpy(rowName, name);
        flushSoon = true;
    }
    if (rowLen && !utc) strcpy(name, rowSplit ? nextRowName : rowName);
    if (rowLen && strcmp(name, rowSplit ? nextRowName : rowName) != 0) {
        if (!rowSplit) {
            rowSplit = rowLen;
            rowSplitRows = rowsInBuf;
            rowSplitEvents = eventsInBuf;
            strcpy(nextRowName, name);
        } else if (strcmp(name, nextRowName) != 0) {
            flushSoon = true;
            return false;
        }
        flushSoon = true;
    }
    if (!rowLen) {
        strcpy(rowName, name);
        dataBufferedMs = millis();
    }
    if (rowLen + ROW_MAX > ROW_BUF_SIZE) flushSoon = true;
    return rowLen + ROW_MAX <= ROW_BUF_SIZE;
}

/* ---------------- Events ---------------- */

// Events share the DATA file with the rows: utc,local,uptime_s,"TYPE detail".
bool writeEvent(const char* type, const char* detail) {
    if (state != ST_READY || !rowBuf) {
        ++eventsLost;
        return false;
    }
    unsigned long now = millis();
    if (now - eventWindowMs >= 60000UL) {
        eventWindowMs = now;
        eventsThisMinute = 0;
    }
    if (eventsThisMinute >= EVENTS_PER_MINUTE) {
        ++eventsLost;
        return false;
    }
    ++eventsThisMinute;
    uint32_t utc = nowUtc();
    if (!rowSpace(utc) || state != ST_READY) {
        ++eventsLost;
        return false;
    }
    Out o = {rowBuf + rowLen, static_cast<size_t>(ROW_BUF_SIZE - rowLen), 0};
    put(o, "D,");
    putStamp(o, utc, now);
    putChar(o, '"');
    put(o, type);
    putChar(o, ' ');
    putEscaped(o, detail);
    put(o, "\"\r\n");
    rowLen += o.len;
    ++eventsInBuf;
    return true;
}

void eventf(const char* type, const char* format, ...) {
    if (state != ST_READY || !detailBuf) {
        ++eventsLost;
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(detailBuf, DETAIL_SIZE, format, args);
    va_end(args);
    writeEvent(type, detailBuf);
}

// escape: names go straight into a quoted field (event lines escape later).
void putProbes(Out& o, bool escape) {
    put(o, "count=");
    putUInt(o, sys.waterProbeCount);
    put(o, ";control_tank=w");
    putUInt(o, static_cast<unsigned long>(sys.probeRoleMap[PROBE_TANK]) + 1UL);
    for (uint8_t i = 0; i < sys.waterProbeCount && i < MAX_WATER_PROBES; ++i) {
        char id[17];
        if (!sensors_waterProbeId(i, id)) continue;
        put(o, ";w");
        putUInt(o, i + 1UL);
        putChar(o, '=');
        put(o, id);
        putChar(o, ':');
        if (escape) putEscaped(o, sys.waterProbeNames[i]);
        else put(o, sys.waterProbeNames[i]);
    }
}

void writeProbesEvent() {
    if (state != ST_READY || !detailBuf) {
        ++eventsLost;
        return;
    }
    Out o = {detailBuf, DETAIL_SIZE, 0};
    putProbes(o, false);
    writeEvent("PROBES", detailBuf);
}

const char* stateName(uint8_t s) {
    static const char* const names[] = {"IDLE", "RAMP", "HOLD", "BOOST", "EMBER_GUARD"};
    return s < 5 ? names[s] : "UNKNOWN";
}

enum Kind : uint8_t { K_BURN, K_STATUS, K_SETTING };
struct Track {
    const char* name;
    Kind kind;
};
const Track TRACKS[] = {
    {"burn_state", K_BURN},        {"safety", K_STATUS},
    {"sensor_faults", K_STATUS},   {"season", K_STATUS},
    {"boost", K_STATUS},           {"ember_guard_active", K_STATUS},
    {"damper_delay", K_STATUS},
    {"ember_guard_latched", K_STATUS}, {"exhaust_fallback", K_STATUS},
    {"self_clean_active", K_STATUS}, {"wifi", K_STATUS},
    {"exhaust_sensor_ok", K_STATUS},
    {"exhaust_setpoint_f", K_SETTING}, {"clamp_min_pct", K_SETTING},
    {"clamp_max_pct", K_SETTING},  {"deadband_f", K_SETTING},
    {"fan_always_on", K_SETTING},  {"tank_low_f", K_SETTING},
    {"tank_high_f", K_SETTING},    {"run_mode", K_SETTING},
    {"boost_time_s", K_SETTING},   {"ember_timer_min", K_SETTING},
    {"boost_fan_pct", K_SETTING},
    {"damper_delay_s", K_SETTING},
    {"flue_low_f", K_SETTING},     {"flue_recovery_f", K_SETTING},
    {"season_mode", K_SETTING},
};
constexpr uint8_t TRACK_COUNT = sizeof(TRACKS) / sizeof(TRACKS[0]);
int32_t snapshot[TRACK_COUNT];
uint32_t probeSignature = 0;

void readTracked(int32_t* v) {
    uint8_t i = 0;
    v[i++] = sys.burnState;
    v[i++] = sys.safetyState;
    v[i++] = sys.sensorFaultMask;
    v[i++] = sys.envActiveSeason == ENV_SEASON_NONE ? -1 : sys.envActiveSeason;
    v[i++] = sys.boostActive;
    v[i++] = sys.emberGuardianActive;
    v[i++] = sys.burnState == BURN_BOOST && sys.boostActive && sys.damperDelayActive;
    v[i++] = sys.emberGuardianLatched;
    v[i++] = sys.exhaustFallbackActive;
    v[i++] = sys.selfCleanActive;
    v[i++] = sys.wifiOK;
    v[i++] = sys.exhaustSensorOK;
    v[i++] = sys.exhaustSetpoint;
    v[i++] = sys.clampMinPercent;
    v[i++] = sys.clampMaxPercent;
    v[i++] = sys.deadbandF;
    v[i++] = sys.deadzoneFanMode;
    v[i++] = sys.tankLowSetpointF;
    v[i++] = sys.tankHighSetpointF;
    v[i++] = sys.controlMode;
    v[i++] = sys.boostTimeSeconds;
    v[i++] = sys.emberGuardianTimerMinutes;
    v[i++] = sys.boostFanPercent;
    v[i++] = sys.damperDelaySeconds;
    v[i++] = sys.flueLowThreshold;
    v[i++] = sys.flueRecoveryThreshold;
    v[i++] = sys.envSeasonMode;
}

void logWatchdogSettings() {
    if (!watchdogSettingsStep || state != ST_READY || !detailBuf) return;
    if (millis() - eventWindowMs < 60000UL && eventsThisMinute >= EVENTS_PER_MINUTE) return;
    const uint8_t step = watchdogSettingsStep;
    const char* type = "WD_SETTINGS";
    if (step == 1) {
        snprintf(detailBuf, DETAIL_SIZE, "BEGIN restored settings reset=%s", watchdog_resetText());
    } else if (step == 2) {
        int32_t values[TRACK_COUNT];
        readTracked(values);
        Out o = {detailBuf, DETAIL_SIZE, 0};
        put(o, "general");
        for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
            if (TRACKS[i].kind != K_SETTING) continue;
            putChar(o, ' '); put(o, TRACKS[i].name);
            putChar(o, '='); putInt(o, values[i]);
        }
    } else if (step == 3) {
        snprintf(detailBuf, DETAIL_SIZE,
                 "self_clean on=%u every=%u start_h=%u end_h=%u utc_min=%d dst=%u push=%u share=%u",
                 sys.selfCleanEnabled, sys.selfCleanIntervalBurns, sys.selfCleanStartHour,
                 sys.selfCleanEndHour, sys.selfCleanUtcOffsetMinutes, sys.selfCleanDstEnabled,
                 sys.pushEnabled, datashare_consent());
    } else if (step == 4) {
        snprintf(detailBuf, DETAIL_SIZE, "seasonal auto=%u lock_s=%lu mode=%d active=%d metric=%u",
                 sys.envAutoSeasonEnabled, static_cast<unsigned long>(sys.envModeLockoutSec),
                 sys.envSeasonMode, sys.envActiveSeason, sys.envUnitsMetric);
    } else if (step <= 8) {
        const EnvSeason season = static_cast<EnvSeason>(step - 5);
        snprintf(detailBuf, DETAIL_SIZE,
                 "season=%u start_f=%d buffer_f=%d exhaust_f=%d tank_low_f=%d tank_high_f=%d fan_max_pct=%u",
                 step - 5, *ui_getSeasonStartPtr(season), *ui_getSeasonBufferPtr(season),
                 *ui_getSeasonSetpointPtr(season), *ui_getSeasonTankLowPtr(season),
                 *ui_getSeasonTankHighPtr(season), *ui_getSeasonClampMaxPtr(season));
    } else if (step <= 8 + MAX_WATER_PROBES) {
        const uint8_t probe = step - 9;
        if (probe >= sys.waterProbeCount) { ++watchdogSettingsStep; return; }
        char id[17];
        if (!sensors_waterProbeId(probe, id)) {
            snprintf(detailBuf, DETAIL_SIZE, "probe=%u no ROM ID", probe + 1);
        } else {
            Out o = {detailBuf, DETAIL_SIZE, 0};
            put(o, "probe="); putUInt(o, probe + 1);
            put(o, " id="); put(o, id);
            put(o, " controls_tank="); putUInt(o, sys.probeRoleMap[PROBE_TANK] == probe);
            float offset;
            put(o, " offset_f=");
            if (watercal_probeOffset(probe, offset)) putFixed(o, offset, 3);
            else put(o, "unavailable");
        }
    } else {
        strcpy(detailBuf, "END restored settings (F, US gal)");
    }
    if (!writeEvent(type, detailBuf)) return;
    watchdogSettingsStep = step == 9 + MAX_WATER_PROBES ? 0 : step + 1;
}

uint32_t readProbeSignature() {
    uint32_t h = 2166136261UL;
    auto mix = [&h](uint8_t b) { h = (h ^ b) * 16777619UL; };
    mix(sys.waterProbeCount);
    mix(sys.probeRoleMap[PROBE_TANK]);
    for (uint8_t i = 0; i < sys.waterProbeCount && i < MAX_WATER_PROBES; ++i) {
        char id[17];
        if (sensors_waterProbeId(i, id)) {
            for (uint8_t c = 0; id[c]; ++c) mix(id[c]);
        }
        for (uint8_t c = 0; c < PROBE_NAME_LENGTH && sys.waterProbeNames[i][c]; ++c)
            mix(sys.waterProbeNames[i][c]);
    }
    return h;
}

void pollEvents(unsigned long now) {
    if (now - lastEventPollMs < EVENT_POLL_MS) return;
    lastEventPollMs = now;
    int32_t current[TRACK_COUNT];
    readTracked(current);
    if (!snapshotReady) {
        memcpy(snapshot, current, sizeof(snapshot));
        probeSignature = readProbeSignature();
        lastProbePollMs = now;
        clockWasValid = nowUtc() != 0;
        snapshotReady = true;
        return;
    }
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        if (current[i] == snapshot[i]) continue;
        if (TRACKS[i].kind == K_BURN) {
            eventf("BURN_STATE", "%s->%s", stateName(snapshot[i]), stateName(current[i]));
        } else {
            eventf(TRACKS[i].kind == K_SETTING ? "SETTING" : "STATUS", "%s %ld->%ld",
                   TRACKS[i].name, static_cast<long>(snapshot[i]), static_cast<long>(current[i]));
        }
        snapshot[i] = current[i];
    }
    bool clockValid = nowUtc() != 0;
    if (clockValid != clockWasValid) {
        clockWasValid = clockValid;
        eventf("CLOCK", "%s", clockValid ? "network time available" : "network time lost");
    }
    // Same wording as the dashboard alert.
    const char* alert = wifiapi_alertText();
    if (strcmp(alert, lastAlert) != 0) {
        if (alert[0]) eventf("ALARM", "%s", alert);
        else eventf("ALARM", "cleared: %s", lastAlert);
        snprintf(lastAlert, sizeof(lastAlert), "%s", alert);
    }
    if (now - lastProbePollMs >= PROBE_POLL_MS) {
        lastProbePollMs = now;
        uint32_t signature = readProbeSignature();
        if (signature != probeSignature) {
            probeSignature = signature;
            writeProbesEvent();
        }
    }
}

/* ---------------- Data rows ---------------- */

// INFO and PROBES describe a new daily file. Later changes are logged as events.
FRESULT writeFileInfo(uint8_t part) {
    uint32_t utc = nowUtc();
    unsigned long now = millis();
    Out o = {lineBuf, LINE_SIZE, 0};
    put(o, "D,");
    putStamp(o, utc, now);
    if (part == 0) {
        put(o, "\"INFO firmware=" FW_VERSION " boiler_make=");
        putEscaped(o, datashare_make());
        put(o, " boiler_model=");
        putEscaped(o, datashare_model());
        put(o, " row_seconds=");
        putUInt(o, ROW_INTERVAL_MS / 1000UL);
    } else {
        put(o, "\"PROBES ");
        putProbes(o, true);
    }
    put(o, "\"\r\n");
    UINT bw = 0;
    FRESULT r = f_write(&dataFile, lineBuf, o.len, &bw);
    if (r == FR_OK && bw != o.len) r = FR_DENIED;
    if (r == FR_OK) logBytes += o.len;
    return r;
}

void serviceWrites(unsigned long now) {
    if (state != ST_READY || dlOpen || sdWorkedThisPass) return;
    if (writePhase == WRITE_IDLE) {
        if (rowLen && !rowName[0]) {
            uint32_t utc = nowUtc();
            if (utc) {
                historyDayName(utc, rowName);
                flushSoon = true;
            }
        }
        if (pendingBurnLength && !pendingBurnDay[0]) {
            uint32_t utc = nowUtc();
            if (utc) historyDayName(utc, pendingBurnDay);
        }
        bool dataDue = rowLen && rowName[0] && (rowSplit || flushSoon || rowLen + ROW_MAX > ROW_BUF_SIZE ||
                                  now - dataBufferedMs >= FLUSH_INTERVAL_MS);
        bool historyDue = historyLen && (historySplit || historyFlushSoon || historyLen + LINE_SIZE > HISTORY_BUF_SIZE ||
                                        now - historyBufferedMs >= FLUSH_INTERVAL_MS);
        writingBurn = pendingBurnLength && pendingBurnDay[0];
        if (!dataDue && !historyDue && !writingBurn) return;
        writingHistory = !writingBurn && historyDue && (!dataDue || preferHistory);
        preferHistory = !writingHistory;
        batchLength = writingHistory ? (historySplit ? historySplit : historyLen)
                                     : (rowSplit ? rowSplit : rowLen);
        batchRows = writingHistory ? (historySplit ? historySplitRows : historyPending)
                                   : (rowSplit ? rowSplitRows : rowsInBuf);
        batchEvents = writingHistory ? 0 : (rowSplit ? rowSplitEvents : eventsInBuf);
        if (writingBurn) batchLength = pendingBurnLength;
        batchOffset = 0;
        writePhase = WRITE_OPEN;
    }
    unsigned long started = millis();
    FIL* file = &dataFile;
    FRESULT r = FR_OK;
    const char* operation = writingBurn ? "burn batch" : writingHistory ? "history batch" : "data batch";
    if (writePhase == WRITE_OPEN) {
        const char* day = writingBurn ? pendingBurnDay : writingHistory ? historyName : rowName;
        if (dataOpen && strcmp(dataName, day) != 0) {
            r = f_close(file);
            dataOpen = false;
        } else if (dataOpen) {
            writePhase = WRITE_BYTES;
        } else {
            char path[24];
            snprintf(path, sizeof(path), "/LOGS/%s.CSV", day);
            r = f_open(file, path, FA_READ | FA_WRITE | FA_OPEN_APPEND);
            if (r == FR_OK) {
                dataOpen = true;
                strcpy(dataName, day);
                dataOriginalSize = f_size(file);
                dataHeaderSame = dataOriginalSize >= sizeof(DATA_HEADER) - 1;
                dataEndsWithNewline = true;
                headerOffset = 0;
                writePhase = dataOriginalSize ? WRITE_END_SEEK : WRITE_HEADER;
            }
        }
    } else if (writePhase == WRITE_END_SEEK) {
        r = f_lseek(file, dataOriginalSize - 1);
        writePhase = WRITE_END_READ;
    } else if (writePhase == WRITE_END_READ) {
        char last = '\0';
        UINT read = 0;
        r = f_read(file, &last, 1, &read);
        if (r == FR_OK && read != 1) r = FR_DISK_ERR;
        dataEndsWithNewline = last == '\n';
        writePhase = dataHeaderSame ? WRITE_START_SEEK : WRITE_APPEND_SEEK;
    } else if (writePhase == WRITE_START_SEEK) {
        r = f_lseek(file, 0);
        writePhase = WRITE_HEADER_READ;
    } else if (writePhase == WRITE_HEADER_READ) {
        char header[64];
        UINT read = 0, want = sizeof(DATA_HEADER) - 1 - headerOffset;
        if (want > sizeof(header)) want = sizeof(header);
        r = f_read(file, header, want, &read);
        if (r == FR_OK && read != want) r = FR_DISK_ERR;
        if (r == FR_OK) {
            dataHeaderSame = memcmp(header, DATA_HEADER + headerOffset, want) == 0;
            headerOffset += want;
            if (!dataHeaderSame || headerOffset == sizeof(DATA_HEADER) - 1)
                writePhase = WRITE_APPEND_SEEK;
        }
    } else if (writePhase == WRITE_APPEND_SEEK) {
        r = f_lseek(file, dataOriginalSize);
        writePhase = !dataEndsWithNewline ? WRITE_NEWLINE : dataHeaderSame ? WRITE_BYTES : WRITE_HEADER;
    } else if (writePhase == WRITE_NEWLINE) {
        UINT written = 0;
        r = f_write(file, "\r\n", 2, &written);
        if (r == FR_OK && written != 2) r = FR_DENIED;
        writePhase = dataHeaderSame ? WRITE_BYTES : WRITE_HEADER;
    } else if (writePhase == WRITE_HEADER || writePhase == WRITE_BURN_HEADER) {
        UINT written = 0;
        const char* header = writePhase == WRITE_HEADER ? DATA_HEADER : BURN_HEADER;
        UINT length = strlen(header);
        r = f_write(file, header, length, &written);
        if (r == FR_OK && written != length) r = FR_DENIED;
        writePhase = writePhase == WRITE_HEADER ? WRITE_BURN_HEADER : WRITE_INFO;
    } else if (writePhase == WRITE_INFO || writePhase == WRITE_PROBES) {
        r = writeFileInfo(writePhase == WRITE_INFO ? 0 : 1);
        writePhase = writePhase == WRITE_INFO ? WRITE_PROBES : WRITE_BYTES;
    } else if (writePhase == WRITE_BYTES) {
        UINT written = 0;
        UINT count = batchLength - batchOffset;
        if (count > SD_IO_CHUNK) count = SD_IO_CHUNK;
        const char* buffer = writingBurn ? pendingBurnLine : writingHistory ? historyBuf : rowBuf;
        r = f_write(file, buffer + batchOffset, count, &written);
        if (r == FR_OK && written != count) r = FR_DENIED;
        if (r == FR_OK) {
            batchOffset += written;
            if (batchOffset == batchLength) writePhase = WRITE_SYNC;
        }
    } else if (writePhase == WRITE_SYNC) {
        r = f_sync(file);
        if (r == FR_OK && writingBurn) {
            pendingBurnLength = 0;
            logBytes += batchLength;
            writePhase = WRITE_IDLE;
        } else if (r == FR_OK) {
            char* buffer = writingHistory ? historyBuf : rowBuf;
            uint16_t& length = writingHistory ? historyLen : rowLen;
            uint16_t& pending = writingHistory ? historyPending : rowsInBuf;
            uint16_t& split = writingHistory ? historySplit : rowSplit;
            uint16_t& splitRows = writingHistory ? historySplitRows : rowSplitRows;
            char* name = writingHistory ? historyName : rowName;
            char* nextName = writingHistory ? nextHistoryName : nextRowName;
            memmove(buffer, buffer + batchLength, length - batchLength);
            length -= batchLength;
            pending -= batchRows;
            if (split == batchLength) {
                strcpy(name, nextName);
                nextName[0] = '\0';
                split = splitRows = 0;
                if (!writingHistory) rowSplitEvents = 0;
            } else if (split) {
                split -= batchLength;
                splitRows -= batchRows;
                if (!writingHistory) rowSplitEvents -= batchEvents;
            }
            if (writingHistory) {
                historyFlushSoon = false;
                if (!length) historyBufferedMs = now;
            } else {
                rowsLogged += batchRows;
                eventsLogged += batchEvents;
                eventsInBuf -= batchEvents;
                flushSoon = false;
                if (!length) dataBufferedMs = now;
            }
            logBytes += batchLength;
            writePhase = WRITE_IDLE;
        }
    }
    if (r != FR_OK) dropCard(operation, r);
    lastWriteMs = millis() - started;
    if (lastWriteMs > maxWriteMs) maxWriteMs = lastWriteMs;
    finishSdStep(started, operation);
}

bool freshWater(uint8_t i, unsigned long now) {
    return i < sys.waterProbeCount && i < MAX_WATER_PROBES && sys.waterTempLastGoodMs[i] != 0 &&
           now - sys.waterTempLastGoodMs[i] <= 3000UL && isfinite(sys.waterTempF[i]);
}

void historyDayName(uint32_t utc, char* name) {
    Civil c = toCivil(utc);
    snprintf(name, 9, "%04d%02u%02u", static_cast<int>(c.year),
             static_cast<unsigned>(c.month), static_cast<unsigned>(c.day));
}

void writeHistory(uint32_t utc, char type, uint32_t duration, uint32_t interval, float tank) {
    if (state != ST_READY || !utc) {
        ++historyRowsLost;
        return;
    }
    char name[9];
    historyDayName(utc, name);
    Out o = {lineBuf, LINE_SIZE, 0};
    putUInt(o, utc);
    putChar(o, ',');
    putChar(o, type);
    putChar(o, ',');
    if (type == 'T') {
        unsigned long now = millis();
        bool first = true;
        for (uint8_t i = 0; i < sys.waterProbeCount && i < MAX_WATER_PROBES; ++i) {
            char id[17];
            if (!sensors_waterProbeId(i, id)) continue;
            if (!first) putChar(o, ',');
            first = false;
            put(o, id);
            putChar(o, ',');
            if (freshWater(i, now)) putFixed(o, sys.waterTempF[i], 1);
        }
        if (first) return;
    } else if (type == 'Q') {
        unsigned long now = millis();
        putFixed(o, tank, 1);
        putChar(o, ',');
        if (sys.exhaustSensorOK && now - sys.exhaustLastGoodMs <= 1000UL)
            putFixed(o, sys.exhaustSmoothF, 1);
        putChar(o, ',');
        putInt(o, sys.fanFinal);
        putChar(o, ',');
        putInt(o, sys.burnState);
        put(o, ",,");  // Retain legacy heat columns for mixed-version history.
        const int values[] = {sys.tankLowSetpointF, sys.tankHighSetpointF,
                             sys.clampMinPercent, sys.clampMaxPercent,
                             sys.controlMode, sys.deadzoneFanMode};
        for (int value : values) { putChar(o, ','); putInt(o, value); }
        put(o, ",,,");
        putChar(o, ','); putInt(o, digitalRead(PIN_DAMPER) == LOW ? 1 : 0);
    } else {
        if (type == 'B') putUInt(o, duration);
        putChar(o, ',');
        if (type == 'B' && interval) putUInt(o, interval);
        putChar(o, ',');
        putFixed(o, tank, 1);
    }
    put(o, "\r\n");
    if (!historyLen) {
        strcpy(historyName, name);
        historyBufferedMs = millis();
    } else if (strcmp(name, historySplit ? nextHistoryName : historyName) != 0) {
        if (!historySplit) {
            historySplit = historyLen;
            historySplitRows = historyPending;
            strcpy(nextHistoryName, name);
        } else if (strcmp(name, nextHistoryName) != 0) {
            ++historyRowsLost;
            historyFlushSoon = true;
            return;
        }
        historyFlushSoon = true;
    }
    if (historyLen + o.len > HISTORY_BUF_SIZE) {
        ++historyRowsLost;
        historyFlushSoon = true;
        return;
    }
    memcpy(historyBuf + historyLen, lineBuf, o.len);
    historyLen += o.len;
    ++historyPending;
}

void pollHistory(unsigned long now) {
    if (historyBurn.pending) {
        historyBurn.pending = false;
        writeHistory(historyBurn.utc, 'B', historyBurn.duration, historyBurn.interval,
                     historyBurn.tank);
    }
    int8_t damper = digitalRead(PIN_DAMPER) == LOW ? 1 : 0;
    if (lastHistoryDamper >= 0 && damper == lastHistoryDamper &&
        now - lastHistoryMs < HISTORY_INTERVAL_MS) return;
    lastHistoryMs = now;
    lastHistoryDamper = damper;
    uint8_t tank = sys.probeRoleMap[PROBE_TANK];
    uint32_t utc = nowUtc();
    float temp = freshWater(tank, now) ? sys.waterTempF[tank] : NAN;
    writeHistory(utc, 'Q', 0, 0, temp);
    writeHistory(utc, 'T', 0, 0, NAN);
}

void appendRow(unsigned long now) {
    if (state != ST_READY || !rowBuf) {
        ++rowsLost;
        return;
    }
    uint32_t utc = nowUtc();
    if (!rowSpace(utc)) {
        ++rowsLost;
        return;
    }
    Out o = {rowBuf + rowLen, static_cast<size_t>(ROW_BUF_SIZE - rowLen), 0};
    bool exhaustFresh = sys.exhaustSensorOK && now - sys.exhaustLastGoodMs <= 1000UL;
    put(o, "D,");
    putStamp(o, utc, now);
    putChar(o, ',');  // empty event column
    putInt(o, sys.burnState);
    putChar(o, ',');
    putInt(o, sys.safetyState);
    putChar(o, ',');
    putInt(o, sys.sensorFaultMask);
    putChar(o, ',');
    putInt(o, sys.fanFinal);
    putChar(o, ',');
    putInt(o, sys.fanDemand);
    putChar(o, ',');
    if (exhaustFresh) putFixed(o, sys.exhaustSmoothF, 1);
    putChar(o, ',');
    if (exhaustFresh) putFixed(o, sys.exhaustRawF, 1);
    putChar(o, ',');
    putInt(o, sys.exhaustSetpoint);
    putChar(o, ',');
    putInt(o, sys.exhaustSensorOK ? 1 : 0);
    putChar(o, ',');
    uint8_t tank = sys.probeRoleMap[PROBE_TANK];
    if (freshWater(tank, now)) putFixed(o, sys.waterTempF[tank], 1);
    putChar(o, ',');
    putInt(o, sys.tankLowSetpointF);
    putChar(o, ',');
    putInt(o, sys.tankHighSetpointF);
    putChar(o, ',');
    putInt(o, sys.clampMinPercent);
    putChar(o, ',');
    putInt(o, sys.clampMaxPercent);
    putChar(o, ',');
    putInt(o, sys.controlMode);
    putChar(o, ',');
    if (sys.envActiveSeason != ENV_SEASON_NONE) putInt(o, sys.envActiveSeason);
    putChar(o, ',');
    if (sys.envSensorOK) putFixed(o, sys.envTempF, 1);
    putChar(o, ',');
    if (sys.envSensorOK) putFixed(o, sys.envHumidity, 1);
    putChar(o, ',');
    if (sys.envSensorOK) putFixed(o, sys.envPressure, 1);
    for (uint8_t i = 0; i < 8; ++i) {
        putChar(o, ',');
        if (freshWater(i, now)) putFixed(o, sys.waterTempF[i], 1);
    }
    put(o, ",,,,,");  // Preserve the five archived estimate columns in DATA CSV.
    putChar(o, ',');
    unsigned flags = 0;
    if (sys.boostActive) flags |= 1;
    if (sys.emberGuardianLatched) flags |= 2;
    if (sys.exhaustFallbackActive) flags |= 4;
    if (sys.wifiOK) flags |= 8;
    if (sys.selfCleanActive) flags |= 16;
    if (sys.deadzoneFanMode) flags |= 32;
    if (utc) flags |= 64;
    if (sys.emberGuardianActive) flags |= 128;
    putUInt(o, flags);
    put(o, "\r\n");
    rowLen += o.len;
    ++rowsInBuf;
}

/* ---------------- Retention ---------------- */

// Only rotate new unified logs. Do not remove legacy files.
bool oldestDataFile(char* oldest, uint16_t& count) {
    DIR dir;
    FILINFO info;
    count = 0;
    oldest[0] = '\0';
    if (f_opendir(&dir, "/LOGS") != FR_OK) return false;
    char keepFrom[9];
    uint32_t utc = nowUtc();
    historyDayName(utc > HISTORY_KEEP_SEC ? utc - HISTORY_KEEP_SEC : 0, keepFrom);
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        if (info.fattrib & AM_DIR || strlen(info.fname) != 12 ||
            strspn(info.fname, "0123456789") != 8 || strcmp(info.fname + 8, ".CSV")) continue;
        ++count;
        if (!utc || strncmp(info.fname, keepFrom, 8) >= 0 ||
            !strncmp(info.fname, dataName, 8) || !strncmp(info.fname, rowName, 8) ||
            !strncmp(info.fname, historyName, 8) || !strncmp(info.fname, pendingBurnDay, 8)) continue;
        if (!oldest[0] || nameCmp(info.fname, oldest) < 0) strcpy(oldest, info.fname);
    }
    f_closedir(&dir);
    return oldest[0] != '\0';
}

bool deleteLog(const char* dir, const char* name) {
    char path[26];
    snprintf(path, sizeof(path), "/%s/%s", dir, name);
    FILINFO info;
    if (f_stat(path, &info) != FR_OK) return false;
    if (f_unlink(path) == FR_OK) {
        logBytes = logBytes > info.fsize ? logBytes - info.fsize : 0;
        return true;
    }
    return false;
}

void runRetention(bool force) {
    if ((state != ST_READY && state != ST_FULL) || dlOpen) return;
    uint64_t volume = volumeBytes();
    uint64_t reserve = volume / 10;
    bool low = freeKnown() ? freeBytes() < reserve : logBytes + reserve > volume;
    if (!low && !force) {
        if (state == ST_FULL) state = ST_READY;
        return;
    }
    char oldest[13];
    uint16_t count = 0;
    if (!oldestDataFile(oldest, count) || count <= 2) {
        state = ST_FULL;
        snprintf(lastError, sizeof(lastError), "card full; no old logs to remove");
        return;
    }
    if (!deleteLog("LOGS", oldest)) {
        state = ST_FULL;
        snprintf(lastError, sizeof(lastError), "could not delete old log");
        return;
    }
    ++filesDeleted;
    state = ST_READY;
    eventf("RETENTION", "deleted day %s (low free space)", oldest);
}

/* ---------------- Burns ---------------- */

// Counted, then logged once after the restore (no SD write deep inside it).
uint8_t restoreInvalid = 0;

float fieldFloat(const char* s) {
    if (!s || !*s) return NAN;
    // Burn CSV temperatures are written by putFixed(..., 1), never scientific/hex.
    char* end;
    long whole = strtol(s, &end, 10);
    if (end != s && whole >= -100000000L && whole <= 100000000L) {
        float value = static_cast<float>(whole);
        if (*end == '.' && end[1] >= '0' && end[1] <= '9') {
            float fraction = static_cast<float>(end[1] - '0') / 10.0f;
            value += s[0] == '-' ? -fraction : fraction;
            end += 2;
        }
        if (*end == '\0') return value;
    }
    if (restoreInvalid < 255) ++restoreInvalid;
    return NAN;
}

bool parseBurnLine(char* line, uint32_t nowUtcSec, BurnRecord& r, uint32_t& age) {
    char* fields[23];
    uint8_t count = 0;
    char* cr = strchr(line, '\r');
    if (cr) *cr = '\0';
    char* s = line;
    while (count < 23) {
        fields[count++] = s;
        char* comma = strchr(s, ',');
        if (!comma) break;
        *comma = '\0';
        s = comma + 1;
    }
    if (count < 16 || fields[0][0] < '0' || fields[0][0] > '9') return false;
    uint32_t endUtc = strtoul(fields[0], nullptr, 10);
    if (endUtc > nowUtcSec || nowUtcSec - endUtc > RESTORE_MAX_AGE_SEC) return false;
    age = nowUtcSec - endUtc;
    r = {};
    r.durationSec = strtoul(fields[2], nullptr, 10);
    r.startF = fieldFloat(fields[3]);
    r.endF = fieldFloat(fields[4]);
    r.low = static_cast<int16_t>(atoi(fields[6]));
    r.high = static_cast<int16_t>(atoi(fields[7]));
    r.minimum = static_cast<int16_t>(atoi(fields[8]));
    r.maximum = static_cast<int16_t>(atoi(fields[9]));
    r.mode = static_cast<uint8_t>(atoi(fields[10]));
    r.fanMode = static_cast<uint8_t>(atoi(fields[11]));
    r.endState = static_cast<uint8_t>(atoi(fields[12]));
    r.changed = atoi(fields[13]) != 0;
    r.complete = atoi(fields[14]) != 0;
    r.probesValid = atoi(fields[15]) != 0;
    return true;
}

void readBurnTail(int year, char* buf, BurnRecord* recs, uint32_t* ages, uint8_t& n,
                  uint32_t utc) {
    char path[20];
    snprintf(path, sizeof(path), "/BURNS/%04d.CSV", year);
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return;
    FSIZE_t size = f_size(&f);
    FSIZE_t start = size > RESTORE_TAIL ? size - RESTORE_TAIL : 0;
    UINT br = 0;
    FRESULT r = f_lseek(&f, start);
    if (r == FR_OK) r = f_read(&f, buf, RESTORE_TAIL, &br);
    f_close(&f);
    if (r != FR_OK) return;
    buf[br] = '\0';
    char* p = buf;
    if (start > 0) {
        p = strchr(p, '\n');
        if (!p) return;
        ++p;
    }
    while (*p) {
        char* end = strchr(p, '\n');
        if (!end) break;  // Incomplete final line.
        *end = '\0';
        BurnRecord rec;
        uint32_t age;
        if (parseBurnLine(p, utc, rec, age)) {
            if (n == RESTORE_COUNT) {
                memmove(recs, recs + 1, sizeof(BurnRecord) * (RESTORE_COUNT - 1));
                memmove(ages, ages + 1, sizeof(uint32_t) * (RESTORE_COUNT - 1));
                --n;
            }
            recs[n] = rec;
            ages[n] = age;
            ++n;
        }
        p = end + 1;
    }
}

void tryRestore() {
    if (restoreDone) return;
    if (!burnperformance_canRestore()) {
        restoreDone = true;
        return;
    }
    uint32_t utc = nowUtc();
    if (!utc) return;  // Wait for network time; ages need a real clock.
    restoreDone = true;
    BurnRecord* recs = static_cast<BurnRecord*>(malloc(sizeof(BurnRecord) * RESTORE_COUNT));
    char* buf = static_cast<char*>(malloc(RESTORE_TAIL + 1));
    if (!recs || !buf) {
        free(recs);
        free(buf);
        return;
    }
    uint32_t ages[RESTORE_COUNT];
    uint8_t n = 0;
    Civil c = localCivil(utc);
    readBurnTail(c.year - 1, buf, recs, ages, n, utc);
    readBurnTail(c.year, buf, recs, ages, n, utc);
    if (n) burnperformance_restore(recs, ages, n);
    restoredBurns = n;
    free(recs);
    free(buf);
    if (restoreInvalid) eventf("RESTORE_INVALID", "%u invalid burn temperatures", restoreInvalid);
    if (n) eventf("RESTORE", "loaded %u burn periods into Performance", n);
}

/* ---------------- Mount ---------------- */

void releaseMountWorkspace() {
    // FatFs can register the object even if the filesystem check fails.
    if (fs) f_mount(nullptr, "", 0);
    free(fs);
    free(rowBuf);
    fs = nullptr;
    rowBuf = lineBuf = detailBuf = historyBuf = nullptr;
    sdcard_invalidate();
}

bool tryMount(unsigned long now) {
    lastMountMs = now;
    ++mountAttempts;
    bootMountResult = UINT8_MAX;
    sdcard_beginBootTrace();
    if (disk_initialize(0) & STA_NOINIT) {
        sdcard_endBootTrace();
        ++mountInitFails;
        // An earlier card error stays visible while retries continue.
        if (state != ST_ERROR) {
            state = ST_ABSENT;
            setError("card init", FR_NOT_READY);
        }
        if (fs) releaseMountWorkspace();
        return false;
    }
    if (!fs) {
        fs = static_cast<FATFS*>(calloc(1, sizeof(FATFS)));
        rowBuf = static_cast<char*>(malloc(ROW_BUF_SIZE + LINE_SIZE + HISTORY_BUF_SIZE));
        if (!fs || !rowBuf) {
            sdcard_endBootTrace();
            snprintf(lastError, sizeof(lastError), "not enough RAM for SD buffers");
            state = ST_ERROR;
            releaseMountWorkspace();
            return false;
        }
        lineBuf = rowBuf + ROW_BUF_SIZE;
        detailBuf = lineBuf;
        historyBuf = lineBuf + LINE_SIZE;
    }
    FRESULT r = f_mount(fs, "", 1);
    sdcard_endBootTrace();
    bootMountResult = r;
    if (r == FR_NO_FILESYSTEM) {
        state = ST_NOFS;
        setError("no supported filesystem found", r);
        releaseMountWorkspace();
        return false;
    }
    if (r != FR_OK) {
        state = r == FR_NOT_READY ? ST_ABSENT : ST_ERROR;
        setError("mount", r);
        releaseMountWorkspace();
        return false;
    }
    const char* const dirs[] = {"/LOGS"};
    for (const char* d : dirs) {
        r = f_mkdir(d);
        if (r != FR_OK && r != FR_EXIST) {
            state = ST_ERROR;
            setError("create folders", r);
            releaseMountWorkspace();
            return false;
        }
    }
    logBytes = dirBytes("/LOGS");
    dataOpen = false;
    dlOpen = false;
    state = ST_READY;
    loggingSuspended = false;
    everMounted = true;
    DBG_PRINTLN("SD: mounted");
    return true;
}

void onMounted() {
    unsigned long cardMb = static_cast<unsigned long>(volumeBytes() / 1048576ULL);
    if (!bootLogged) {
        bootLogged = true;
        eventf("BOOT", "firmware=%s reset=%s", FW_VERSION, watchdog_resetText());
    } else {
        eventf("SD_RECOVERED", "rows_lost_total=%lu last_error=%s",
               static_cast<unsigned long>(rowsLost), lastError);
    }
    if (freeKnown()) {
        eventf("SD_CARD", "card_mb=%lu free_mb=%lu log_mb=%lu", cardMb,
               static_cast<unsigned long>(freeBytes() / 1048576ULL),
               static_cast<unsigned long>(logBytes / 1048576ULL));
    } else {
        eventf("SD_CARD", "card_mb=%lu free_mb=unknown log_mb=%lu", cardMb,
               static_cast<unsigned long>(logBytes / 1048576ULL));
    }
}

}  // namespace

/* ---------------- Public API ---------------- */

void datalogger_recordHistoryBurn(uint32_t durationSec, uint32_t intervalSec) {
    if (historyBurn.pending) {
        ++historyRowsLost;
        DBG_PRINTLN("SD: burn hist overwritten");
    }
    uint8_t tank = sys.probeRoleMap[PROBE_TANK];
    historyBurn.utc = nowUtc();
    historyBurn.duration = durationSec;
    historyBurn.interval = intervalSec;
    historyBurn.tank = freshWater(tank, millis()) ? sys.waterTempF[tank] : NAN;
    historyBurn.pending = true;
}

String datalogger_historyJson(bool performance) {
    DynamicJsonDocument doc(768);
    datalogger_buildHistoryJson(doc, performance);
    return jsonToString(doc);
}

void datalogger_buildHistoryJson(JsonDocument& doc, bool performance) {
    doc.clear();
    uint32_t utc = nowUtc();
    bool mounted = state == ST_READY || state == ST_FULL;
    if (writePhase != WRITE_IDLE) mounted = false;
    doc["available"] = mounted && utc != 0;
    doc["now_utc"] = utc;
    const uint32_t window = 86400UL;
    doc["window_seconds"] = window;
    doc["sample_seconds"] = HISTORY_INTERVAL_MS / 1000UL;
    doc["utc_offset_minutes"] = sys.selfCleanUtcOffsetMinutes +
                                (sys.selfCleanDstEnabled ? 60 : 0);
    if (!mounted) doc["reason"] = writePhase != WRITE_IDLE ? "SD batch in progress; retry shortly" : STATE_TEXT[state];
    else if (!utc) doc["reason"] = "Waiting for network time";
    JsonArray files = doc.createNestedArray("files");
    if (mounted && utc > window) {
        unsigned long started = millis();
        // Keep the legacy chart files readable alongside the new daily files.
        const uint32_t back[2] = {window, 0};
        const char* const folders[] = {"HISTORY", "LOGS"};
        // Static to keep this off the 1 KB stack during web requests.
        static char previous[9], name[9], path[30];
        static FILINFO info;
        previous[0] = '\0';
        for (uint8_t i = 0; i < 2; ++i) {
            historyDayName(utc - back[i], name);
            if (!strcmp(name, previous)) continue;
            strcpy(previous, name);
            for (const char* folder : folders) {
                snprintf(path, sizeof(path), "/%s/%s.CSV", folder, name);
                FRESULT r = f_stat(path, &info);
                if (!finishSdStep(started, "slow history list")) {
                    doc["available"] = false;
                    doc["reason"] = "History card read suspended";
                    files.clear();
                    break;
                }
                if (r == FR_NO_FILE || r == FR_NO_PATH) continue;
                if (r != FR_OK) {
                    dropCard("history list", r);
                    doc["available"] = false;
                    doc["reason"] = "History card read failed";
                    files.clear();
                    break;
                }
                JsonArray file = files.createNestedArray();
                file.add(String(path + 1));
                file.add(static_cast<uint32_t>(info.fsize));
            }
            if (!doc["available"].as<bool>()) break;
        }
    }
}

void datalogger_init() {
    unsigned long now = millis();
    lastRowMs = lastRetentionMs = lastHistoryMs = now;
    lastHistoryDamper = -1;
    watchdogSettingsStep = watchdog_wasReset() ? 1 : 0;
    if (tryMount(now)) onMounted();
    else {
        loggingSuspended = true;
        DBG_PRINTLN("SD: no card");
    }
    // Mount/maintenance contain multiple calls: measure their total, but do not
    // turn successful aggregate work into a card failure. Existing I/O error handling stays.
    finishSdStep(now, "mount", false);
}

void datalogger_loop(unsigned long now) {
    sdWorkedThisPass = false;
    if (state != ST_READY && state != ST_FULL && now - lastMountMs >= MOUNT_RETRY_MS) {
        unsigned long started = millis();
        if (tryMount(now)) onMounted();
        else loggingSuspended = true;
        finishSdStep(started, "mount", false);
        return;
    }
    pollEvents(now);
    logWatchdogSettings();
    pollHistory(now);
    if (now - lastRowMs >= ROW_INTERVAL_MS) {
        lastRowMs = now;
        if (everMounted) appendRow(now);
    }
    if (retentionDue && state == ST_READY && !dlOpen && writePhase == WRITE_IDLE) {
        retentionDue = false;
        lastRetentionMs = now;
        unsigned long started = millis();
        runRetention(true);
        finishSdStep(started, "retention", false);
        return;
    }
    serviceWrites(now);
    if (sdWorkedThisPass || dlOpen || writePhase != WRITE_IDLE) return;
    if (now - lastRetentionMs >= RETENTION_CHECK_MS) {
        lastRetentionMs = now;
        unsigned long started = millis();
        runRetention(false);
        finishSdStep(started, "retention", false);
        return;
    }
    if (state == ST_READY && !restoreDone) {
        unsigned long started = millis();
        tryRestore();
        finishSdStep(started, "restore", false);
    }
}

void datalogger_recordBurn(const BurnRecord& b) {
    if (state != ST_READY || !lineBuf || pendingBurnLength) {
        ++burnsLost;
        return;
    }
    uint32_t utc = nowUtc();
    Out o = {lineBuf, LINE_SIZE, 0};
    put(o, "S,");
    if (utc) putUInt(o, utc);
    putChar(o, ',');
    putLocal(o, utc);
    putChar(o, ',');
    putUInt(o, b.durationSec);
    putChar(o, ',');
    putFixed(o, b.startF, 1);
    putChar(o, ',');
    putFixed(o, b.endF, 1);
    putChar(o, ',');
    putFixed(o, b.endF - b.startF, 1);
    putChar(o, ',');
    putInt(o, b.low);
    putChar(o, ',');
    putInt(o, b.high);
    putChar(o, ',');
    putInt(o, b.minimum);
    putChar(o, ',');
    putInt(o, b.maximum);
    putChar(o, ',');
    putUInt(o, b.mode);
    putChar(o, ',');
    putUInt(o, b.fanMode);
    putChar(o, ',');
    putUInt(o, b.endState);
    putChar(o, ',');
    putUInt(o, b.changed);
    putChar(o, ',');
    putUInt(o, b.complete);
    putChar(o, ',');
    putUInt(o, b.probesValid);
    putChar(o, ',');
    putFixed(o, b.avgFan, 1);
    putChar(o, ',');
    putFixed(o, b.avgExhaustF, 1);
    putChar(o, ',');
    putFixed(o, b.peakExhaustF, 0);
    putChar(o, ',');
    putFixed(o, b.avgOutdoorF, 1);
    putChar(o, ',');
    putUInt(o, b.boostSec);
    putChar(o, ',');
    putUInt(o, b.rampSec);
    putChar(o, ',');
    putUInt(o, b.holdSec);
    put(o, "\r\n");
    memcpy(pendingBurnLine, lineBuf, o.len);
    pendingBurnDay[0] = '\0';
    if (utc) historyDayName(utc, pendingBurnDay);
    pendingBurnLength = o.len;
}

bool datalogger_ready() { return state == ST_READY; }

String datalogger_statusJson() {
    DynamicJsonDocument doc(1024);
    datalogger_buildStatusJson(doc);
    return jsonToString(doc);
}

void addBootRead(JsonDocument& doc, const char* key, const SdBootRead& probe) {
    if (!probe.attempted) return;
    char text[48];
    if (probe.ok) {
        snprintf(text, sizeof(text), "LBA %lu read OK, signature %04X",
                 static_cast<unsigned long>(probe.sector), static_cast<unsigned>(probe.signature));
    } else {
        snprintf(text, sizeof(text), "LBA %lu read FAILED, SdFat 0x%02X",
                 static_cast<unsigned long>(probe.sector), static_cast<unsigned>(probe.error));
    }
    doc[key] = text;
}

void datalogger_buildStatusJson(JsonDocument& doc) {
    doc.clear();
    jsonSetBool(doc, "enabled", true);
    doc["state"] = STATE_TEXT[state];
    jsonSetBool(doc, "clock_valid", nowUtc() != 0);
    if (fs && (state == ST_READY || state == ST_FULL)) {
        doc["card_mb"] = static_cast<unsigned long>(volumeBytes() / 1048576ULL);
        if (freeKnown()) doc["free_mb"] = static_cast<unsigned long>(freeBytes() / 1048576ULL);
        else doc["free_mb"] = nullptr;
        doc["log_mb"] = static_cast<float>(logBytes / 1048576.0);
    }
    if (dataOpen) {
        char file[16];
        snprintf(file, sizeof(file), "%s.CSV", dataName);
        doc["file"] = file;
    }
    jsonSetUnsigned(doc, "rows_logged", rowsLogged);
    jsonSetUnsigned(doc, "rows_pending", rowsInBuf);
    jsonSetUnsigned(doc, "events_pending", eventsInBuf);
    jsonSetUnsigned(doc, "history_rows_pending", historyPending);
    jsonSetUnsigned(doc, "buffer_bytes", ROW_BUF_SIZE + HISTORY_BUF_SIZE);
    jsonSetUnsigned(doc, "batch_interval_seconds", FLUSH_INTERVAL_MS / 1000UL);
    jsonSetBool(doc, "logging_suspended", state != ST_READY);
    jsonSetUnsigned(doc, "last_sd_step_ms", lastSdStepMs);
    jsonSetUnsigned(doc, "max_sd_step_ms", maxSdStepMs);
    jsonSetUnsigned(doc, "max_download_read_ms", maxDownloadReadMs);
    jsonSetUnsigned(doc, "sd_step_limit_ms", SD_STEP_LIMIT_MS);
    jsonSetUnsigned(doc, "retry_seconds", state == ST_FULL ? RETENTION_CHECK_MS / 1000UL :
                                        loggingSuspended ? MOUNT_RETRY_MS / 1000UL : 0);
    jsonSetUnsigned(doc, "rows_lost", rowsLost);
    jsonSetUnsigned(doc, "events_logged", eventsLogged);
    jsonSetUnsigned(doc, "events_lost", eventsLost);
    jsonSetUnsigned(doc, "burns_lost", burnsLost);
    jsonSetUnsigned(doc, "burns_restored", restoredBurns);
    jsonSetUnsigned(doc, "history_rows_lost", historyRowsLost);
    jsonSetUnsigned(doc, "files_deleted", filesDeleted);
    jsonSetUnsigned(doc, "last_write_ms", lastWriteMs);
    jsonSetUnsigned(doc, "max_write_ms", maxWriteMs);
    if (lastError[0]) doc["last_error"] = lastError;
    if (lastDropError[0]) doc["last_drop_error"] = lastDropError;
    jsonSetUnsigned(doc, "mount_attempts", mountAttempts);
    jsonSetUnsigned(doc, "mount_init_fails", mountInitFails);
    jsonSetUnsigned(doc, "card_init_error", sdcard_initError());
    if (bootMountResult != UINT8_MAX) jsonSetUnsigned(doc, "boot_mount_result", bootMountResult);
    addBootRead(doc, "boot_sector_zero", sdcard_bootRead(false));
    addBootRead(doc, "boot_partition", sdcard_bootRead(true));
}

String datalogger_listJson(const char* dirName, const char* before) {
    DynamicJsonDocument doc(1536);
    datalogger_buildListJson(doc, dirName, before);
    return jsonToString(doc);
}

void datalogger_buildListJson(JsonDocument& doc, const char* dirName, const char* before) {
    doc.clear();
    if (strcmp(dirName, "LOGS") && strcmp(dirName, "DATA") &&
        strcmp(dirName, "BURNS") && strcmp(dirName, "HISTORY"))
        { doc["error"] = "unknown folder"; return; }
    if (state != ST_READY && state != ST_FULL) { doc["error"] = "no card"; return; }
    if (dlOpen || datalogger_downloadBusy()) { doc["error"] = "SD busy; retry shortly"; return; }
    struct Listing {
        char names[LIST_MAX][13];
        uint32_t sizes[LIST_MAX];
    };
    Listing* list = static_cast<Listing*>(malloc(sizeof(Listing)));
    if (!list) { doc["error"] = "insufficient memory"; return; }
    char (*names)[13] = list->names;
    uint32_t* sizes = list->sizes;
    uint8_t count = 0;
    bool more = false;
    char path[10];
    snprintf(path, sizeof(path), "/%s", dirName);
    DIR dir;
    FILINFO info;
    unsigned long started = millis();
    FRESULT r = f_opendir(&dir, path);
    if (r != FR_OK) {
        free(list);
        if (r != FR_NO_PATH) dropCard("list", r);
        finishSdStep(started, "slow folder open");
        doc["error"] = "folder unavailable";
        return;
    }
    bool scanTimedOut = false;
    while ((r = f_readdir(&dir, &info)) == FR_OK && info.fname[0]) {
        if (millis() - started > SD_STEP_LIMIT_MS) { scanTimedOut = true; break; }
        if (info.fattrib & AM_DIR || info.fname[0] == '?') continue;
        if (before && before[0] && nameCmp(info.fname, before) >= 0) continue;
        // Keep the LIST_MAX newest names, sorted newest first.
        uint8_t pos = count;
        while (pos > 0 && nameCmp(info.fname, names[pos - 1]) > 0) --pos;
        if (pos >= LIST_MAX) {
            more = true;
            continue;
        }
        if (count == LIST_MAX) more = true;
        uint8_t last = count < LIST_MAX ? count : LIST_MAX - 1;
        for (uint8_t i = last; i > pos; --i) {
            strcpy(names[i], names[i - 1]);
            sizes[i] = sizes[i - 1];
        }
        strncpy(names[pos], info.fname, 12);
        names[pos][12] = '\0';
        sizes[pos] = info.fsize;
        if (count < LIST_MAX) ++count;
    }
    FRESULT closed = f_closedir(&dir);
    if (r == FR_OK) r = closed;
    if (r != FR_OK) {
        free(list);
        dropCard("list", r);
        finishSdStep(started, "slow folder list");
        doc["error"] = "card read failed";
        return;
    }
    // A folder-wide scan budget is not a failed card transaction.
    if (!finishSdStep(started, "slow folder list", false)) {
        free(list);
        doc["error"] = "card read suspended";
        return;
    }
    if (scanTimedOut) {
        free(list);
        doc["error"] = "folder scan timed out; card remains mounted";
        return;
    }
    // The HTTP handler's query buffer goes out of scope before streamed sending.
    doc["dir"] = String(dirName);
    doc["more"] = more;
    JsonArray files = doc.createNestedArray("files");
    for (uint8_t i = 0; i < count; ++i) {
        JsonArray f = files.createNestedArray();
        f.add(names[i]);  // Copied: names is a char array, stored by value.
        f.add(sizes[i]);
    }
    free(list);
}

bool datalogger_validPath(const char* p) {
    const char* rest = nullptr;
    if (!strncmp(p, "LOGS/", 5) || !strncmp(p, "DATA/", 5)) rest = p + 5;
    else if (!strncmp(p, "BURNS/", 6)) rest = p + 6;
    else if (!strncmp(p, "HISTORY/", 8)) rest = p + 8;
    if (!rest) return false;
    uint8_t n = 0;
    while ((rest[n] >= 'A' && rest[n] <= 'Z') || (rest[n] >= '0' && rest[n] <= '9')) {
        if (++n > 8) return false;
    }
    return n > 0 && strcmp(rest + n, ".CSV") == 0;
}

bool datalogger_downloadBusy() {
    return writePhase != WRITE_IDLE || sdWorkedThisPass;
}

bool datalogger_openDownload(const char* path, uint32_t& size, uint32_t start) {
    if ((state != ST_READY && state != ST_FULL) || dlOpen || !datalogger_validPath(path))
        return false;
    if (datalogger_downloadBusy()) return false;
    unsigned long started = millis();
    // FatFs does not allow a second handle to a file opened for writing.
    if (dataOpen && !strncmp(path, "LOGS/", 5) && !strncmp(path + 5, dataName, 8)) {
        FRESULT closed = f_close(&dataFile);
        dataOpen = false;
        if (closed != FR_OK) {
            dropCard("close log for download", closed);
            finishSdStep(started, "slow close log for download");
            return false;
        }
    }
    char full[28];
    snprintf(full, sizeof(full), "/%s", path);
    FRESULT opened = f_open(&dlFile, full, FA_READ);
    if (opened != FR_OK) {
        if (opened != FR_NO_FILE && opened != FR_NO_PATH) dropCard("download open", opened);
        finishSdStep(started, "slow download open");
        return false;
    }
    if (start > f_size(&dlFile)) {
        FRESULT closed = f_close(&dlFile);
        if (closed != FR_OK) dropCard("download close", closed);
        finishSdStep(started, "slow download open");
        return false;
    }
    dlStart = start;
    dlSeekPending = start != 0;
    dlOpen = true;
    size = f_size(&dlFile) - start;
    finishSdStep(started, "slow download open");
    return dlOpen;
}

int datalogger_readDownload(uint8_t* buffer, uint16_t length) {
    if (!dlOpen) return -1;
    if (sdWorkedThisPass) return -2;
    if (length > SD_IO_CHUNK) length = SD_IO_CHUNK;
    unsigned long started = millis();
    if (dlSeekPending) {
        FRESULT r = f_lseek(&dlFile, dlStart);
        dlSeekPending = false;
        if (r != FR_OK) dropCard("download seek", r);
        return finishSdStep(started, "slow download seek") ? -2 : -1;
    }
    UINT br = 0;
    FRESULT r = f_read(&dlFile, buffer, length, &br);
    unsigned long elapsed = millis() - started;
    if (elapsed > maxDownloadReadMs) maxDownloadReadMs = elapsed;
    if (r != FR_OK) {
        dropCard("download read", r);
        finishSdStep(started, "slow download read");
        return -1;
    }
    if (!finishSdStep(started, "slow download read")) return -1;
    return static_cast<int>(br);
}

void datalogger_closeDownload() {
    if (!dlOpen) return;
    unsigned long started = millis();
    FRESULT r = f_close(&dlFile);
    dlOpen = false;
    if (r != FR_OK) dropCard("download close", r);
    finishSdStep(started, "slow download close");
}

#else  // SD_LOGGING_ENABLED

void datalogger_init() {}
void datalogger_loop(unsigned long) {}
void datalogger_recordBurn(const BurnRecord&) {}
void datalogger_recordHistoryBurn(uint32_t, uint32_t) {}
void datalogger_buildHistoryJson(JsonDocument& doc, bool) {
    doc.clear();
    doc["available"] = false;
    doc["reason"] = "SD logging disabled in firmware";
    doc.createNestedArray("files");
}
void datalogger_buildStatusJson(JsonDocument& doc) {
    doc.clear();
    doc["enabled"] = false;
    doc["state"] = "Disabled in firmware";
}
void datalogger_buildListJson(JsonDocument& doc, const char*, const char*) {
    doc.clear();
    doc["error"] = "disabled";
}
String datalogger_historyJson(bool) {
    return String("{\"available\":false,\"reason\":\"SD logging disabled in firmware\",\"files\":[]}");
}
String datalogger_statusJson() { return String("{\"enabled\":false,\"state\":\"Disabled in firmware\"}"); }
bool datalogger_ready() { return false; }
String datalogger_listJson(const char*, const char*) { return String("{\"error\":\"disabled\"}"); }
bool datalogger_validPath(const char*) { return false; }
bool datalogger_openDownload(const char*, uint32_t&, uint32_t) { return false; }
int datalogger_readDownload(uint8_t*, uint16_t) { return -1; }
bool datalogger_downloadBusy() { return false; }
void datalogger_closeDownload() {}

#endif  // SD_LOGGING_ENABLED
