// ESP8266 Matrix Clock -- build-time configuration
#pragma once

#ifdef CRASH_TEST
#define FW_VERSION      "1.0.0"               // safe-mode test build (env:crashtest): always older than any release
#else
#define FW_VERSION      "1.3.0"               // bump for every release (tools/release.sh reads it from here)
#endif

#include <MD_MAX72xx.h>

// ---- Display wiring (MAX7219 8x8 modules, chained) ---------------------------
//   CLK -> D5 (GPIO14, hardware SPI SCK)
//   DIN -> D7 (GPIO13, hardware SPI MOSI)
//   CS  -> D6 (GPIO12)
#define PIN_CS          D6
#define MATRIX_HW       MD_MAX72XX::FC16_HW   // the common blue 4-in-1 modules; try GENERIC_HW / PAROLA_HW if text looks scrambled
#define MATRIX_MODULES  4                     // 4 x 8 = 32 columns
#define FLIP_VERTICAL   0                     // set to 1 if everything shows upside down
#define FLIP_HORIZONTAL 0                     // set to 1 if everything shows mirrored

// ---- Button ------------------------------------------------------------------
// NodeMCU "FLASH" button. Short press = scroll IP address, hold 5 s = WiFi setup.
#define PIN_BUTTON      0                     // GPIO0 = D3
#define BUTTON_HOLD_MS  5000                  // hold this long: WiFi setup
#define BUTTON_PASS_MS  10000                 // hold this long: remove the settings-page password

// ---- Network -----------------------------------------------------------------
#define AP_PREFIX       "MatrixClock-"        // setup hotspot name, followed by 4 chip-ID hex digits
#define HOSTNAME        "matrixclock"         // http://matrixclock.local
#define WIFI_CONNECT_MS 20000                 // give up on the saved network after this long and open setup
#define WIFI_RETRY_MS   120000                // while in setup mode, retry the saved network this often
#define NTP_SERVER_1    "in.pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"
#define NTP_SERVER_3    "pool.ntp.org"

// ---- Safe mode -----------------------------------------------------------------
// After this many crashes in a row the clock starts in safe mode (WiFi + update page only).
#define SAFE_MODE_CRASHES   3
#define SAFE_MODE_STABLE_MS 120000            // running this long counts as "not crashing"
#define SAFE_MODE_CHECK_MS  1800000           // in safe mode, look for a fixed release this often

// ---- Updates -------------------------------------------------------------------
// The clock installs new releases published at github.com/<GITHUB_REPO>/releases.
// Each release needs two assets: firmware.bin and version.txt (see tools/release.sh).
#define GITHUB_REPO     "ElectroIoT/ESP8266-Matrix-Clock"
#define AUTO_UPDATE_HOUR 3                    // nightly check at 03:xx local time
#define BOOT_UPDATE_DELAY_MS 60000            // after power-on, install a waiting update after this long

// Password for uploading a .bin from the web page. Only its SHA-256 is compiled in, taken
// from src/private_config.h (not in git); without it, manual upload is disabled.
#if __has_include("private_config.h")
#include "private_config.h"
#endif
#ifndef UPDATE_PASS_SHA256
#define UPDATE_PASS_SHA256 ""
#endif

// ---- Welcome text ----------------------------------------------------------------
// Scrolls at every power-on. Fixed in the firmware on purpose: it is not a setting
// and cannot be changed from the web page.
#define WELCOME_TEXT    "manoranjan.dev"

// ---- Defaults for first boot / factory reset (all changeable on the web page) --
#define DEF_TZ          "IST-5:30"            // POSIX TZ string, India
#define DEF_BRIGHTNESS  4                     // 0..15
