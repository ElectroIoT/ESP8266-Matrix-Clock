// Firmware updates from GitHub Releases
#pragma once

#include <Arduino.h>

struct OtaState {
    String   latest;        // newest version seen on GitHub, empty = not checked yet
    String   message;       // last result / error, shown on the web page
    uint32_t checkedAt = 0; // millis() of the last check, 0 = never
};
extern OtaState ota;

bool otaCheck();                  // fetch version.txt of the latest release; true if it is newer than FW_VERSION
bool otaNewer(const String& v);   // v > FW_VERSION ?
void otaInstall();                // download and flash firmware.bin of the latest release, restarts on success
