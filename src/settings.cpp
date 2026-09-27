#include "settings.h"
#include "config.h"
#include <EEPROM.h>

// Bump when the Settings layout changes, so old flash contents are replaced by defaults.
static const uint32_t MAGIC = 0x4D434B31;   // "MCK1"
static const uint8_t  SETTINGS_VER = 3;

static void defaultsV2() {   // fields added in layout version 2
    cfg.autoUpdate = 1;
    cfg.msgEvery   = 0;
    memset(cfg.msg, 0, sizeof(cfg.msg));
    memset(cfg.events, 0, sizeof(cfg.events));
}

static void defaultsV3() {   // fields added in layout version 3
    cfg.ldrOn     = 0;
    cfg.ldrMin    = 0;
    cfg.ldrMax    = 12;
    cfg.ldrInvert = 0;
    memset(cfg.adminHash, 0, sizeof(cfg.adminHash));
    memset(cfg.adminSalt, 0, sizeof(cfg.adminSalt));
    memset(cfg.sessionKey, 0, sizeof(cfg.sessionKey));
}

Settings cfg;

void settingsDefaults() {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic       = MAGIC;
    cfg.brightness  = DEF_BRIGHTNESS;
    cfg.autoDim     = 0;
    cfg.nightFrom   = 22;
    cfg.nightTo     = 7;
    cfg.nightBr     = 0;
    cfg.fmt24       = 0;
    cfg.leadingZero = 0;
    cfg.blinkColon  = 1;
    cfg.secondsBar  = 1;
    cfg.showDate    = 1;
    cfg.roll        = ROLL_DOWN;
    cfg.hourlyAnim  = ANIM_RANDOM;
    cfg.bootAnim    = ANIM_SPARKLE;
    strlcpy(cfg.tz, DEF_TZ, sizeof(cfg.tz));
    defaultsV2();
    defaultsV3();
    cfg.ver = SETTINGS_VER;
}

// EEPROM keeps a RAM copy of the whole area while open, so it is only opened to read
// or write: that ~1.2 KB of heap is needed for TLS during updates.
void settingsLoad() {
    EEPROM.begin(sizeof(Settings));
    EEPROM.get(0, cfg);
    EEPROM.end();
    if (cfg.magic != MAGIC) {
        settingsDefaults();
        return;
    }
    // never trust flash contents blindly
    cfg.ssid[sizeof(cfg.ssid) - 1] = 0;
    cfg.pass[sizeof(cfg.pass) - 1] = 0;
    cfg.tz[sizeof(cfg.tz) - 1] = 0;
    if (!cfg.tz[0]) strlcpy(cfg.tz, DEF_TZ, sizeof(cfg.tz));
    if (cfg.brightness > 15) cfg.brightness = DEF_BRIGHTNESS;
    if (cfg.nightFrom > 23) cfg.nightFrom = 22;
    if (cfg.nightTo > 23) cfg.nightTo = 7;
    if (cfg.nightBr > 16) cfg.nightBr = 0;
    if (cfg.roll > ROLL_RANDOM) cfg.roll = ROLL_DOWN;
    if (cfg.hourlyAnim > ANIM_RANDOM) cfg.hourlyAnim = ANIM_OFF;
    if (cfg.bootAnim > ANIM_RANDOM) cfg.bootAnim = ANIM_OFF;

    // Flash written by an older firmware: fields it didn't know hold erased-flash garbage.
    // (0xFF is > SETTINGS_VER, i.e. "older than version 2".)
    if (cfg.ver != SETTINGS_VER) {
        if (cfg.ver < 2 || cfg.ver > SETTINGS_VER) defaultsV2();
        if (cfg.ver < 3 || cfg.ver > SETTINGS_VER) defaultsV3();
        cfg.ver = SETTINGS_VER;
        settingsSave();
    }
    cfg.ldrOn = cfg.ldrOn ? 1 : 0;
    cfg.ldrInvert = cfg.ldrInvert ? 1 : 0;
    if (cfg.ldrMin > 15) cfg.ldrMin = 0;
    if (cfg.ldrMax > 15) cfg.ldrMax = 12;
    cfg.autoUpdate = cfg.autoUpdate ? 1 : 0;
    if (cfg.msgEvery > 60) cfg.msgEvery = 0;
    cfg.msg[sizeof(cfg.msg) - 1] = 0;
    for (Event& e : cfg.events) {
        e.text[sizeof(e.text) - 1] = 0;
        if (e.month > 12 || e.day > 31 || e.anim > ANIM_RANDOM) memset(&e, 0, sizeof(e));
    }
}

void settingsSave() {
    EEPROM.begin(sizeof(Settings));
    EEPROM.put(0, cfg);
    EEPROM.end();   // commits and frees the buffer
}
