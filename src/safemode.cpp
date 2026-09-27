#include "safemode.h"
#include "config.h"
#include <Arduino.h>
#include <user_interface.h>

// Crash counter in RTC memory: it survives resets but is cleared by a power cut,
// so unplugging the clock always gives the normal firmware another try.
struct BootRecord {
    uint32_t magic;
    uint32_t crashes;
    uint32_t intentional;   // set by restart() just before a deliberate ESP.restart()
    uint32_t installUpdate; // install the GitHub update straight after this restart
};
static const uint32_t MAGIC = 0x5AFE0C03;
// User RTC blocks 0-31 (the first 128 bytes) hold eboot's "install the new firmware"
// command after an update -- writing there would silently cancel the update.
static const uint32_t RTC_SLOT = 64;
static BootRecord rec;

static void store() {
    ESP.rtcUserMemoryWrite(RTC_SLOT, (uint32_t*)&rec, sizeof(rec));
}

// On this core a panic / abort / out-of-memory reboot reports the same reason as
// ESP.restart() (REASON_SOFT_RESTART), so deliberate restarts are marked beforehand
// and any unmarked software restart counts as a crash.
static bool wasCrash(uint32_t reason) {
    switch (reason) {
        case REASON_WDT_RST:
        case REASON_EXCEPTION_RST:
        case REASON_SOFT_WDT_RST:  return true;
        case REASON_SOFT_RESTART:  return !rec.intentional;
        default:                   return reason >= 253;   // core's user stack-smash / sw-exception codes
    }
}

bool safeModeCheck() {
    ESP.rtcUserMemoryRead(RTC_SLOT, (uint32_t*)&rec, sizeof(rec));
    if (rec.magic != MAGIC) rec = {MAGIC, 0, 0, 0};
    uint32_t reason = ESP.getResetInfoPtr()->reason;
    rec.crashes = wasCrash(reason) ? rec.crashes + 1 : 0;   // power-on, reset button, deliberate restart: back to 0
    rec.intentional = 0;
    store();
    Serial.printf("Reset: %s, crashes in a row: %u\n", ESP.getResetReason().c_str(), rec.crashes);
    return rec.crashes >= SAFE_MODE_CRASHES;
}

void safeModeStable() {
    if (!rec.crashes) return;
    rec.crashes = 0;
    store();
}

void restart() {
    rec.intentional = 1;
    store();
    ESP.restart();
}

void restartToUpdate() {
    rec.installUpdate = 1;
    restart();
}

bool takeUpdateRequest() {
    if (!rec.installUpdate) return false;
    rec.installUpdate = 0;   // one attempt per request: a failed install must not loop
    store();
    return true;
}
