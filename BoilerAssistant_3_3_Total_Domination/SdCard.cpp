/*
 * Boiler Assistant - Optional SD card driver (SdFat block I/O for FatFs)
 * Uses the UNO R4 software-SPI pins while replacing the previous custom
 * SD command/block protocol with SdFat v2.3.1.
 * SdFat is bundled in src/SdFat so its options are the same in every file;
 * do not include the installed <SdFat.h> anywhere in this sketch.
 */
#include "src/SdFat/SdFat.h"

#if !defined(BA_BUNDLED_SDFAT) || SPI_DRIVER_SELECT != 2 || ENABLE_DEDICATED_SPI != 0
#error "SdCard.cpp must use the bundled src/SdFat configuration"
#endif

#include "SdCard.h"
#include "Pinout.h"
#include "src/fatfs/ff.h"
#include "src/fatfs/diskio.h"

using SdSoftCard = SoftSpiDriver<PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_SCK>;

static SdSoftCard sdSpiPort;
static SdSpiCard sdCard;
static volatile DSTATUS diskStat = STA_NOINIT;
static uint8_t cardType = 0;
static bool cardBegun = false;
static uint8_t initError = 0;
static uint8_t readError = 0;
static bool traceBootReads = false;
static SdBootRead sectorZero = {}, partitionBoot = {};

void sdcard_beginBootTrace() {
    sectorZero = {};
    partitionBoot = {};
    traceBootReads = true;
}

void sdcard_endBootTrace() {
    traceBootReads = false;
}

const SdBootRead& sdcard_bootRead(bool partition) {
    return partition ? partitionBoot : sectorZero;
}

extern "C" void ba_sd_boot_read(LBA_t sector, const BYTE* bytes) {
    if (!traceBootReads) return;
    SdBootRead& probe = sector ? partitionBoot : sectorZero;
    probe.sector = sector;
    probe.attempted = true;
    probe.ok = bytes != nullptr;
    probe.signature = bytes ? (bytes[510] << 8) | bytes[511] : 0;
    probe.error = bytes ? 0 : sdcard_readError();
}

static bool initializeCard() {
    readError = 0;
    if (cardBegun) {
        sdCard.end();
        cardBegun = false;
    }

    SdSpiConfig config(PIN_SD_CS, SHARED_SPI, SD_SCK_MHZ(1),
                       &sdSpiPort);
    cardBegun = sdCard.begin(config);
    initError = cardBegun ? 0 : sdCard.errorCode();
    if (!cardBegun) {
        cardType = 0;
        diskStat = STA_NOINIT;
        return false;
    }

    cardType = sdCard.type();
    diskStat = 0;
    return true;
}

uint8_t sdcard_type() {
    return cardType;
}

uint8_t sdcard_initError() {
    return initError;
}

uint8_t sdcard_readError() {
    return readError;
}

void sdcard_invalidate() {
    diskStat = STA_NOINIT;
    cardType = 0;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return initializeCard() ? diskStat : STA_NOINIT;
}

extern "C" DSTATUS disk_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return diskStat;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (diskStat & STA_NOINIT) return RES_NOTRDY;
    if (count == 1) {
        const unsigned long started = millis();
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            // Skip the extra retry if the first two attempts already spent 400 ms.
            if (attempt == 2 && millis() - started >= 400UL) break;
            if (sdCard.readSector(sector, buff)) return RES_OK;
            const uint8_t error = sdCard.errorCode();
            // A rejected read command usually means the card is still programming an earlier write.
            if (error != SD_CARD_ERROR_READ_TIMEOUT && error != SD_CARD_ERROR_READ_TOKEN &&
                error != SD_CARD_ERROR_CMD17) break;
        }
    } else if (sdCard.readSectors(sector, buff, count)) {
        return RES_OK;
    }
    readError = sdCard.errorCode();
    sdcard_invalidate();
    return RES_ERROR;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (diskStat & STA_NOINIT) return RES_NOTRDY;
    if (!sdCard.writeSectors(sector, buff, count)) {
        sdcard_invalidate();
        return RES_ERROR;
    }
    return RES_OK;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)buff;
    if (pdrv) return RES_PARERR;
    if (diskStat & STA_NOINIT) return RES_NOTRDY;

    if (cmd == CTRL_SYNC) {
        if (!sdCard.syncDevice()) {
            sdcard_invalidate();
            return RES_ERROR;
        }
        return RES_OK;
    }
    if (cmd == GET_SECTOR_COUNT) {
        *(LBA_t*)buff = sdCard.sectorCount();
        return *(LBA_t*)buff ? RES_OK : RES_ERROR;
    }
    if (cmd == GET_SECTOR_SIZE) {
        *(WORD*)buff = 512;
        return RES_OK;
    }
    if (cmd == GET_BLOCK_SIZE) {
        *(DWORD*)buff = 1;
        return RES_OK;
    }
    return RES_PARERR;
}
