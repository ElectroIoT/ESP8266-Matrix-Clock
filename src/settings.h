// Persistent settings (EEPROM emulation in flash)
#pragma once

#include <Arduino.h>

enum Anim : uint8_t { ANIM_OFF, ANIM_SPARKLE, ANIM_WIPE, ANIM_RAIN, ANIM_BOXES, ANIM_PACMAN, ANIM_RANDOM };
// Digit change animation. Values are stored in flash: only ever append.
enum Roll : uint8_t { ROLL_DOWN, ROLL_UP, ROLL_NONE, ROLL_DISSOLVE, ROLL_SLIDE, ROLL_FLIP, ROLL_DROP, ROLL_RANDOM };

struct Settings {
    uint32_t magic;
    char     ssid[33];
    char     pass[65];
    uint8_t  brightness;    // 0..15
    uint8_t  autoDim;       // night mode on/off
    uint8_t  nightFrom;     // hour 0..23
    uint8_t  nightTo;       // hour 0..23
    uint8_t  nightBr;       // 0..15, 16 = display off
    uint8_t  fmt24;
    uint8_t  leadingZero;
    uint8_t  blinkColon;
    uint8_t  secondsBar;    // thin progress line under the digits
    uint8_t  showDate;      // scroll the date once a minute
    uint8_t  roll;          // Roll
    uint8_t  hourlyAnim;    // Anim
    uint8_t  bootAnim;      // Anim
    char     tz[48];
    char     unused[64];    // was the editable welcome text; kept so the flash layout stays compatible
};

extern Settings cfg;

void settingsLoad();
void settingsSave();
void settingsDefaults();       // resets everything, including WiFi
