#include "settings.h"
#include "config.h"
#include <EEPROM.h>

// Bump when the Settings layout changes, so old flash contents are replaced by defaults.
static const uint32_t MAGIC = 0x4D434B31;   // "MCK1"

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
    strlcpy(cfg.welcome, DEF_WELCOME, sizeof(cfg.welcome));
}

void settingsLoad() {
    EEPROM.begin(sizeof(Settings));
    EEPROM.get(0, cfg);
    if (cfg.magic != MAGIC) {
        settingsDefaults();
        return;
    }
    // never trust flash contents blindly
    cfg.ssid[sizeof(cfg.ssid) - 1] = 0;
    cfg.pass[sizeof(cfg.pass) - 1] = 0;
    cfg.tz[sizeof(cfg.tz) - 1] = 0;
    cfg.welcome[sizeof(cfg.welcome) - 1] = 0;
    if (!cfg.tz[0]) strlcpy(cfg.tz, DEF_TZ, sizeof(cfg.tz));
    if (cfg.brightness > 15) cfg.brightness = DEF_BRIGHTNESS;
    if (cfg.nightFrom > 23) cfg.nightFrom = 22;
    if (cfg.nightTo > 23) cfg.nightTo = 7;
    if (cfg.nightBr > 16) cfg.nightBr = 0;
    if (cfg.roll > ROLL_NONE) cfg.roll = ROLL_DOWN;
    if (cfg.hourlyAnim > ANIM_RANDOM) cfg.hourlyAnim = ANIM_OFF;
    if (cfg.bootAnim > ANIM_RANDOM) cfg.bootAnim = ANIM_OFF;
}

void settingsSave() {
    EEPROM.put(0, cfg);
    EEPROM.commit();
}
