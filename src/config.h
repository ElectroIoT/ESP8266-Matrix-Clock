// ESP8266 Matrix Clock -- build-time configuration
#pragma once

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
#define BUTTON_HOLD_MS  5000

// ---- Network -----------------------------------------------------------------
#define AP_PREFIX       "MatrixClock-"        // setup hotspot name, followed by 4 chip-ID hex digits
#define HOSTNAME        "matrixclock"         // http://matrixclock.local
#define WIFI_CONNECT_MS 20000                 // give up on the saved network after this long and open setup
#define WIFI_RETRY_MS   120000                // while in setup mode, retry the saved network this often
#define NTP_SERVER_1    "in.pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"
#define NTP_SERVER_3    "pool.ntp.org"

// ---- Welcome text ----------------------------------------------------------------
// Scrolls at every power-on. Fixed in the firmware on purpose: it is not a setting
// and cannot be changed from the web page.
#define WELCOME_TEXT    "manoranjan.dev"

// ---- Defaults for first boot / factory reset (all changeable on the web page) --
#define DEF_TZ          "IST-5:30"            // POSIX TZ string, India
#define DEF_BRIGHTNESS  4                     // 0..15
