// State shared between the main loop (main.cpp) and the web handlers (web.cpp)
#pragma once

#include <Arduino.h>

enum AppMode : uint8_t { MODE_CLOCK, MODE_SETUP, MODE_SAFE };

extern AppMode  appMode;
extern String   apName;          // setup hotspot name, e.g. "MatrixClock-3F2A"
extern uint8_t  animRequest;     // Anim to play on the next loop pass (ANIM_OFF = none)
extern String   scrollRequest;   // text to scroll on the next loop pass (empty = none)
extern uint32_t restartAt;       // millis() at which to restart, 0 = never
extern bool     digitDemo;
enum OtaRequest : uint8_t { OTA_NONE, OTA_CHECK, OTA_INSTALL };
extern uint8_t  otaRequest;      // GitHub update work for the main loop (needs TLS, too slow for a web handler)       // play the digit change animation once, as a preview

bool timeValid();                      // true once NTP has delivered a real time
void applyBrightness(bool force);      // day / night brightness from settings
void startNtp();                       // (re)start SNTP with the configured time zone
void scheduleRestart(uint32_t delayMs);
void showEventsToday();                // queue today's special-day animation + text, if any
