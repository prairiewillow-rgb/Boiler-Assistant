/*
 * ============================================================
 *  Boiler Assistant - Watchdog and crash report (v3.4.0)
 *  File: Watchdog.cpp
 * ============================================================
 */

#include "Watchdog.h"
#include <WDT.h>
#include <stdio.h>
#include <string.h>

extern "C" char __StackTop, __StackLimit;

static const uint8_t STACK_PAINT = 0xA5;
static const uint32_t CRASH_MAGIC = 0xC0A5FA17UL;

static volatile uint32_t lastKickMs = 0;
static volatile uint32_t allowanceMs = WATCHDOG_LOOP_LIMIT_MS;
static bool started = false;

static const uint32_t HANG_MAGIC = 0x4A6E6721UL;
static const char* const WD_STAGE_NAMES[] = {
    "?", "sensors", "keypad", "probes", "burn/fan", "SD log", "WiFi link",
    "OTA", "HTTP", "MQTT", "push", "screen", "WiFi setup", "settings save",
    "HTTP accept", "HTTP receive", "HTTP send", "HTTP socket read", "HTTP request parse"};

volatile uint8_t watchdogStage = 0;

// The bootloader runs on every reset and reuses RAM 0x20000000-0x2000496C, so
// .noinit records were wiped before boot could read them. They now live in the
// VBATT backup registers, which survive a reset. Bytes 0-3 hold the
// bootloader's double-tap flag.
struct CrashRecord { uint32_t magic, pc, lr; };
struct HangRecord { uint32_t magic, pc, lr, upSec; uint8_t stage; };
static const uint32_t COUNT_MAGIC = 0x57444354UL;
struct ResetCount { uint32_t magic; uint16_t watchdog; };
static const uint16_t BK_HANG = 256, BK_CRASH = 288, BK_COUNT = 320;
static ResetCount resets;
static volatile bool hangSaved = false;

static void bkWrite(uint16_t at, const void* src, uint8_t len) {
    uint16_t prcr = R_SYSTEM->PRCR & 0x0FU;
    R_SYSTEM->PRCR = 0xA502U;  // unlock PRC1 (backup registers)
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint8_t i = 0; i < len; ++i) R_SYSTEM->VBTBKR[at + i] = s[i];
    R_SYSTEM->PRCR = static_cast<uint16_t>(0xA500U | prcr);
}

static void bkRead(uint16_t at, void* dst, uint8_t len) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (uint8_t i = 0; i < len; ++i) d[i] = R_SYSTEM->VBTBKR[at + i];
}

static char resetText[112];  // zero-filled: no flash copy of an initial value
static uint32_t stageStartMs = 0, slowMs = 0;
static uint8_t slowStage = 0;

static const char* stageName(uint8_t stage) {
    return WD_STAGE_NAMES[stage < sizeof(WD_STAGE_NAMES) / sizeof(WD_STAGE_NAMES[0]) ? stage : 0];
}

void watchdog_stage(uint8_t stage) {
    uint32_t now = millis();
    // Stage 0 is setup(); OTA's daily check is expected to take ~10 s.
    if (watchdogStage && watchdogStage != WD_OTA && now - stageStartMs > slowMs) {
        slowMs = now - stageStartMs;
        slowStage = watchdogStage;
    }
    watchdogStage = stage;
    stageStartMs = now;
}

const char* watchdog_resetText() { return resetText[0] ? resetText : "unknown"; }
uint16_t watchdog_resetCount() { return resets.watchdog; }
const char* watchdog_slowStage(uint32_t& ms) {
    ms = slowMs;
    return stageName(slowStage);
}

// setup() and loop() run on their own stack. The core's 1 KB stack sits
// right above the heap; start-up alone reached stk=992 and the loop peaked at
// ~1.4 KB, so the two collided (crash PC=0x223A2272 was JSON text).
// Interrupts keep the core stack.
static uint8_t mainStack[WATCHDOG_STACK_BYTES] __attribute__((aligned(8)));

uint16_t watchdog_stackPeak() {
    const uint8_t* p = mainStack;
    while (p < mainStack + sizeof(mainStack) && *p == STACK_PAINT) ++p;
    return static_cast<uint16_t>(mainStack + sizeof(mainStack) - p);
}

static void stackMonitor(uint8_t which, uint32_t start, uint32_t end) {
    R_MPU_SPMON->SP[which].CTL = 0;
    R_MPU_SPMON->SP[which].SA = start;
    R_MPU_SPMON->SP[which].EA = end;
    R_MPU_SPMON->SP[which].OAD = 0xA501U;  // key + reset on detection
    R_MPU_SPMON->SP[which].CTL = 1;
}

static void __attribute__((noreturn, noinline)) runApp() {
    setup();
    for (;;) loop();
}

// The core calls this weak hook just before setup(). It never returns, so the
// core's own setup()/loop() calls are not reached; both run from runApp().
extern "C" void early_start_freertos_on_header_inclusion() {
    memset(mainStack, STACK_PAINT, sizeof(mainStack));
    uint32_t top = reinterpret_cast<uint32_t>(mainStack + sizeof(mainStack));
    __set_PSP(top - 8);  // strictly inside the monitored range
    // An overflow of either stack resets the board (reported at boot).
    stackMonitor(1, reinterpret_cast<uint32_t>(mainStack), top);
    stackMonitor(0, reinterpret_cast<uint32_t>(&__StackLimit),
                 reinterpret_cast<uint32_t>(&__StackTop));
    __asm volatile(
        "msr control, %0 \n"  // thread mode uses PSP
        "isb             \n"
        "bx %1           \n"
        :: "r"(2U), "r"(runApp) : "memory");
    __builtin_unreachable();
}

// Only the core's crash printer calls printf (setup-time faults, before
// watchdog_begin() replaces it); this stub keeps ~650 B of formatter out.
extern "C" int printf(const char*, ...) { return 0; }

static bool watchdogReset = false;
bool watchdog_wasReset() { return watchdogReset; }

void watchdog_bootReport() {
    uint16_t flags = R_SYSTEM->RSTSR1;
    bool powerOn = R_SYSTEM->RSTSR0 & R_SYSTEM_RSTSR0_PORF_Msk;
    watchdogReset = (flags & R_SYSTEM_RSTSR1_WDTRF_Msk) != 0;
    HangRecord hang;
    CrashRecord crash;
    bkRead(BK_HANG, &hang, sizeof(hang));
    bkRead(BK_CRASH, &crash, sizeof(crash));
    bkRead(BK_COUNT, &resets, sizeof(resets));
    if (powerOn || resets.magic != COUNT_MAGIC) {
        resets.magic = COUNT_MAGIC;
        resets.watchdog = 0;
    }
    if (flags & R_SYSTEM_RSTSR1_WDTRF_Msk) {
        ++resets.watchdog;
        strcpy(resetText, "watchdog reset (board had frozen), ");
        if (hang.magic == HANG_MAGIC)
            snprintf(resetText + 35, sizeof(resetText) - 35, "stuck in %s up=%lu PC=0x%lX LR=0x%lX",
                     stageName(hang.stage), static_cast<unsigned long>(hang.upSec),
                     static_cast<unsigned long>(hang.pc), static_cast<unsigned long>(hang.lr));
        else
            strcat(resetText, "location not recorded");
    } else if (flags & R_SYSTEM_RSTSR1_SPERF_Msk) {
        strcpy(resetText, "stack overflow reset");
    } else if (crash.magic == CRASH_MAGIC) {
        snprintf(resetText, sizeof(resetText), "crash reset PC=0x%lX LR=0x%lX",
                 static_cast<unsigned long>(crash.pc), static_cast<unsigned long>(crash.lr));
    } else if (flags & R_SYSTEM_RSTSR1_SWRF_Msk) {
        strcpy(resetText, "software restart");
    } else if (powerOn) {
        strcpy(resetText, "power on");
    } else {
        strcpy(resetText, "reset button or other");
    }
    Serial.print("BOOT: ");
    Serial.println(resetText);
    uint32_t zero = 0;
    bkWrite(BK_HANG, &zero, sizeof(zero));
    bkWrite(BK_CRASH, &zero, sizeof(zero));
    bkWrite(BK_COUNT, &resets, sizeof(resets));
    // Flags clear by writing 0 after being read as 1.
    R_SYSTEM->RSTSR0 = 0;
    R_SYSTEM->RSTSR1 = 0;
}

void watchdog_kick(uint32_t allowance) {
    allowanceMs = allowance;
    lastKickMs = millis();
    if (hangSaved) {  // loop recovered; forget the overdue record
        hangSaved = false;
        uint32_t zero = 0;
        bkWrite(BK_HANG, &zero, sizeof(zero));
    }
}

extern "C" void HardFault_Handler(void);

void watchdog_begin() {
    // With BACKTRACE_SUPPORT (UNO R4 WiFi) the core points the RAM vector
    // table's HardFault entry at its own handler, which prints and loops
    // forever. Point it back at ours so a crash is recorded and resets.
    reinterpret_cast<volatile uint32_t*>(SCB->VTOR)[3] =
        reinterpret_cast<uint32_t>(&HardFault_Handler);
    watchdog_kick();
    if (!WDT.begin(5000)) {
        Serial.println("WDT: start failed");
        return;
    }
    started = true;
    SysTick_Config(SystemCoreClock / 10UL);
}

extern "C" __attribute__((used)) void watchdog_tick(const uint32_t* frame) {
    if (!started) return;
    uint32_t now = millis();
    if (now - lastKickMs < allowanceMs) {
        WDT.refresh();
        return;
    }
    // Loop is overdue: remember where it is stuck. The last write before the
    // hardware reset (about 5 s later) is the one reported at boot.
    HangRecord hang = {HANG_MAGIC, frame[6], frame[5], now / 1000UL, watchdogStage};
    bkWrite(BK_HANG, &hang, sizeof(hang));
    hangSaved = true;
}

extern "C" __attribute__((naked)) void SysTick_Handler(void) {
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b watchdog_tick \n");
}

extern "C" __attribute__((used)) void watchdog_hardFault(const uint32_t* frame) {
    uint32_t addr = reinterpret_cast<uint32_t>(frame);
    bool valid = addr >= 0x20000000UL && addr <= reinterpret_cast<uint32_t>(&__StackTop) - 32;
    CrashRecord crash = {CRASH_MAGIC, valid ? frame[6] : 0, valid ? frame[5] : 0};
    bkWrite(BK_CRASH, &crash, sizeof(crash));
    NVIC_SystemReset();
}

// Replaces the core's default handler, which loops forever and froze the board.
extern "C" __attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b watchdog_hardFault \n");
}
