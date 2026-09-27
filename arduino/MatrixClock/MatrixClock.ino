// =====================================================================================
//  ESP8266 Matrix Clock -- Arduino IDE version
//  https://github.com/ElectroIoT/ESP8266-Matrix-Clock
//
//  This folder is generated from ../../src by tools/sync_arduino.sh -- the code is the
//  same as the PlatformIO version. Edit settings in the "config.h" tab.
//
//  1. Boards Manager: install "esp8266 by ESP8266 Community" (3.x)
//     (File > Preferences > Additional boards manager URLs:
//      https://arduino.esp8266.com/stable/package_esp8266com_index.json)
//  2. Library Manager: install "MD_MAX72XX" by majicdesigns
//  3. Tools > Board: "NodeMCU 1.0 (ESP-12E Module)"  (or "LOLIN(WEMOS) D1 R2 & mini")
//     Tools > Flash Size: "4MB (FS:2MB OTA:~1019KB)"
//     Tools > CPU Frequency: "160 MHz"
//  4. Upload. Then set up WiFi from your phone (see README).
//
//  Wiring: MAX7219 DIN -> D7, CLK -> D5, CS -> D6, VCC -> 5V (VIN), GND -> GND
// =====================================================================================

// ESP8266 Matrix Clock
// NodeMCU / Wemos D1 mini + 4x MAX7219 8x8 LED modules (32x8)
//
// Power-up:  animation -> welcome text -> connect WiFi -> scroll IP -> clock
// No WiFi:   opens hotspot "MatrixClock-XXXX" with a setup page (captive portal)
// Settings:  http://<clock-ip>/  or  http://matrixclock.local

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "app.h"
#include "config.h"
#include "display.h"
#include "ota.h"
#include "safemode.h"
#include "settings.h"
#include "web.h"

AppMode  appMode = MODE_CLOCK;
String   apName;
uint8_t  animRequest = ANIM_OFF;
String   scrollRequest;
uint32_t restartAt = 0;
bool     digitDemo = false;
bool     moduleTest = false;
uint8_t  otaRequest = OTA_NONE;

static bool      mdnsOn = false;
static uint8_t   appliedBr = 255;   // brightness currently on the display, 16 = switched off
static ClockFace face;

// =================================================================================
// Helpers shared with the web handlers (app.h)
// =================================================================================

bool timeValid() {
    return time(nullptr) > 1600000000;   // SNTP has replaced the 1970 boot time
}

void startNtp() {
    configTime(cfg.tz, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

void scheduleRestart(uint32_t delayMs) {
    restartAt = (millis() + delayMs) | 1;   // never 0, which means "no restart"
}

static bool inNight(int hour) {
    if (cfg.nightFrom == cfg.nightTo) return false;
    if (cfg.nightFrom < cfg.nightTo) return hour >= cfg.nightFrom && hour < cfg.nightTo;
    return hour >= cfg.nightFrom || hour < cfg.nightTo;   // window over midnight, e.g. 22 -> 7
}

// ---- optional light sensor (LDR) on A0 -------------------------------------------
static float lightLevel = -1;   // smoothed reading 0..1023 (bright = high), -1 = not read yet

static void readLight() {
    static uint32_t last = 0;
    if (!cfg.ldrOn) {
        lightLevel = -1;
        return;
    }
    if (millis() - last < 250) return;   // reading A0 too often disturbs WiFi
    last = millis();
    int raw = analogRead(A0);
    if (cfg.ldrInvert) raw = 1023 - raw;
    lightLevel = lightLevel < 0 ? raw : lightLevel * 0.85f + raw * 0.15f;
}

int lightPercent() {
    return lightLevel < 0 ? -1 : (int)(lightLevel * 100 / 1023 + 0.5f);
}

uint8_t brightnessNow() {
    return appliedBr;
}

void applyBrightness(bool force) {
    uint8_t want = cfg.brightness;
    bool night = false;
    if (cfg.autoDim && timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        night = inNight(t.tm_hour);
    }
    if (night) {
        want = cfg.nightBr;   // night mode wins over the light sensor
    } else if (cfg.ldrOn && lightLevel >= 0) {
        float exact = cfg.ldrMin + (cfg.ldrMax - cfg.ldrMin) * lightLevel / 1023.0f;
        // hysteresis: only change step when the light has clearly moved, so it doesn't flicker
        want = (appliedBr > 15 || fabsf(exact - appliedBr) > 0.7f) ? (uint8_t)lroundf(exact) : appliedBr;
    }
    if (want == appliedBr && !force) return;
    if (want > 15) {
        displayPower(false);
    } else {
        displayPower(true);
        displayIntensity(want);
    }
    appliedBr = want;
}

// Special days: text of every event matching today, animation of the first one.
void showEventsToday() {
    if (!timeValid()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    String text;
    uint8_t anim = ANIM_OFF;
    bool first = true;
    for (const Event& e : cfg.events) {
        if (e.month != t.tm_mon + 1 || e.day != t.tm_mday || !e.text[0]) continue;
        if (!first) text += "  *  ";
        else anim = e.anim;
        text += e.text;
        first = false;
    }
    if (!text.length()) return;
    if (anim) animRequest = anim;
    scrollRequest = text;
}

static void runOta() {
    uint8_t req = otaRequest;
    otaRequest = OTA_NONE;
    if (mdnsOn) MDNS.end();   // frees its buffers: TLS needs every KB of heap
    if (otaCheck() && req == OTA_INSTALL) {
        if (appMode == MODE_SAFE) {
            otaInstall();     // safe mode runs almost nothing, the heap is fine as it is
        } else {
            Serial.println(F("Update available: restarting to install it with a fresh heap"));
            scrollText("Update");
            restartToUpdate();
        }
    }
    if (mdnsOn && MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// =================================================================================
// Background work that must keep running, even during blocking animations
// =================================================================================

static void service() {
    webLoop();
    if (mdnsOn) MDNS.update();
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) restart();
}

void serviceWait(uint32_t ms) {
    uint32_t start = millis();
    do {
        service();
        delay(1);
    } while (millis() - start < ms);
}

// FLASH button, acted on when released:
//   short press      -> scroll the IP address
//   hold 5..10 s     -> forget WiFi and open the setup hotspot
//   hold 10 s+       -> remove the settings-page password (forgotten password)
// While held, the display shows what letting go will do. Returns true while that
// hint is on screen, so the normal display code leaves it alone.
static bool handleButton() {
    static uint32_t downAt = 0;
    bool down = digitalRead(PIN_BUTTON) == LOW;

    if (down) {
        if (!downAt) downAt = millis() | 1;
        uint32_t held = millis() - downAt;
        if (held < 1500) return false;
        fbClear();
        drawText(1, held >= BUTTON_PASS_MS ? "PW off" : held >= BUTTON_HOLD_MS ? "WiFi" : "hold");
        fbShow();
        return true;
    }
    if (!downAt) return false;

    uint32_t held = millis() - downAt;
    downAt = 0;
    if (held >= BUTTON_PASS_MS) {
        Serial.println(F("Button: settings password removed"));
        memset(cfg.adminHash, 0, sizeof(cfg.adminHash));
        settingsSave();
        scrollText("Password removed");
    } else if (held >= BUTTON_HOLD_MS) {
        Serial.println(F("Button: forgetting WiFi"));
        cfg.ssid[0] = 0;
        cfg.pass[0] = 0;
        settingsSave();
        scrollText("WiFi reset");
        restart();
    } else if (held > 40 && held < 1500 && appMode == MODE_CLOCK) {
        scrollRequest = String("IP ") + WiFi.localIP().toString();
    }
    fbClear();
    fbShow();
    return false;
}

// =================================================================================
// WiFi
// =================================================================================

static bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(cfg.ssid, cfg.pass);
    Serial.printf("Connecting to \"%s\"\n", cfg.ssid);

    uint32_t start = millis();
    uint16_t frame = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_MS) {
        spinnerFrame(frame++);
        serviceWait(40);
    }
    fbClear();
    fbShow();
    return WiFi.status() == WL_CONNECTED;
}

static void startSetupMode() {
    appMode = MODE_SETUP;
    WiFi.mode(WIFI_AP_STA);   // STA side is needed for scanning and for retrying the saved network
    WiFi.disconnect();
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(apName.c_str());
    webStartPortal();
    Serial.printf("Setup mode: join WiFi \"%s\" and open http://192.168.4.1\n", apName.c_str());
}

static void setupLoop() {
    static Marquee  msg;
    static uint32_t lastFrame = 0;
    static uint32_t lastRetry = millis();

    if (!msg.text.length()) msg.start(String("WiFi setup: join ") + apName + " then open 192.168.4.1");
    if (millis() - lastFrame >= 30) {
        lastFrame = millis();
        msg.step();
    }

    // The saved network may just be down (e.g. router still booting after a power cut):
    // retry it now and then, but never while someone is using the setup page.
    if (cfg.ssid[0] && WiFi.softAPgetStationNum() == 0 && millis() - lastRetry > WIFI_RETRY_MS) {
        Serial.println(F("Retrying saved WiFi"));
        WiFi.begin(cfg.ssid, cfg.pass);
        uint32_t start = millis();
        uint16_t frame = 0;
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            spinnerFrame(frame++);
            serviceWait(40);
        }
        if (WiFi.status() == WL_CONNECTED) restart();   // come back up in normal clock mode
        WiFi.disconnect();
        lastRetry = millis();
    }
}

// =================================================================================
// Clock
// =================================================================================

static void clockLoop() {
    static uint32_t lastFrame = 0;
    static uint32_t secStart = 0;
    static int      lastSec = -1;
    static bool     haveTime = false;
    static uint32_t bootUpdateAt = 0;   // millis() for the post-boot update install, 0 = none
    static Marquee  waitMsg;
    static String   slideText;       // text sliding through, pushing the clock out and back in
    static int      slidePos = -1;   // -1 = no slide running
    static int      slideLen = 0;

    if (animRequest) {
        uint8_t a = animRequest;
        animRequest = ANIM_OFF;
        playAnim(a);
        face.reset();
    }
    if (moduleTest) {
        moduleTest = false;
        showModuleNumbers();
        face.reset();
    }
    if (digitDemo) {
        digitDemo = false;
        face.demo();
    }
    if (otaRequest) {
        runOta();
        face.reset();
    }

    if (millis() - lastFrame < 30) return;   // ~33 frames per second
    lastFrame = millis();

    if (!timeValid()) {
        if (!waitMsg.text.length()) waitMsg.start("Getting time...");
        waitMsg.step();
        return;
    }
    if (!haveTime) {
        haveTime = true;
        Serial.printf("Time synced, heap %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
        if (takeUpdateRequest()) otaInstall();   // restarts on success; on failure the clock just carries on
        face.reset();
        applyBrightness(true);
        showEventsToday();
        // Check GitHub right away. A clock that was switched off for a while (or is off every
        // night at 3 AM) installs a waiting update a minute after power-on instead of never.
        if (otaCheck() && cfg.autoUpdate) bootUpdateAt = millis() + BOOT_UPDATE_DELAY_MS;
    }
    if (bootUpdateAt && (int32_t)(millis() - bootUpdateAt) >= 0) {
        bootUpdateAt = 0;
        otaRequest = OTA_INSTALL;
    }
    // A check or install that failed for a temporary reason is retried later.
    static uint32_t retryAt = 0;
    static uint8_t  retries = 0;
    if (ota.retry && cfg.autoUpdate && !retryAt && retries < OTA_RETRIES) {
        ota.retry = false;
        retries++;
        retryAt = (millis() + OTA_RETRY_MS) | 1;
        Serial.printf("OTA: retry %u in %u min\n", retries, OTA_RETRY_MS / 60000);
    }
    if (retryAt && (int32_t)(millis() - retryAt) >= 0) {
        retryAt = 0;
        otaRequest = OTA_INSTALL;
    }

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    if (t.tm_sec != lastSec) {
        lastSec = t.tm_sec;
        secStart = millis();
        applyBrightness(false);
        // Nightly update, at a per-clock minute so not every clock hits GitHub at once
        if (cfg.autoUpdate && t.tm_hour == AUTO_UPDATE_HOUR && t.tm_min == (int)(ESP.getChipId() % 60) && t.tm_sec == 5)
            otaRequest = OTA_INSTALL;

        if (t.tm_min == 0 && t.tm_sec == 0 && cfg.hourlyAnim) {
            animRequest = cfg.hourlyAnim;
        } else if (t.tm_sec == 10 && cfg.msgEvery && cfg.msg[0] && t.tm_min % cfg.msgEvery == 0 && !scrollRequest.length()) {
            scrollRequest = cfg.msg;
        } else if (t.tm_sec == 45 && t.tm_min % 15 == 0 && !scrollRequest.length()) {
            showEventsToday();
        } else if (t.tm_sec == 30 && cfg.showDate && slidePos < 0 && !scrollRequest.length()) {
            char d[32];
            strftime(d, sizeof(d), "%a %d %b %Y", &t);
            scrollRequest = d;
        }
    }

    if (scrollRequest.length() && slidePos < 0) {
        slideText = scrollRequest;
        scrollRequest = "";
        slidePos = 0;
        slideLen = W + 4 + textWidth(slideText) + 4;
    }

    uint16_t ms = min<uint32_t>(millis() - secStart, 999);
    fbClear();
    if (slidePos >= 0) {
        face.draw(t, ms, -slidePos);              // clock leaves to the left...
        drawText(W + 4 - slidePos, slideText);    // ...text follows...
        face.draw(t, ms, slideLen - slidePos);    // ...and the clock comes back in behind it
        if (++slidePos > slideLen) slidePos = -1;
    } else {
        face.draw(t, ms, 0);
    }
    face.tick();
    fbShow();
}

// =================================================================================

// Safe mode: WiFi, update page and GitHub updates only -- none of the clock features,
// in case one of them is what keeps crashing.
static void startSafeMode() {
    Serial.println(F("SAFE MODE: the firmware crashed several times in a row"));
    scrollText("Safe mode");
    if (!cfg.ssid[0] || !connectWiFi()) {
        startSetupMode();   // no WiFi: the setup hotspot is all we can offer
        return;
    }
    appMode = MODE_SAFE;
    WiFi.setAutoReconnect(true);
    startNtp();
    mdnsOn = MDNS.begin(HOSTNAME);
    if (mdnsOn) MDNS.addService("http", "tcp", 80);
    Serial.printf("Safe mode, update page at http://%s/\n", WiFi.localIP().toString().c_str());
}

static void safeLoop() {
    static Marquee  msg;
    static uint32_t lastFrame = 0;
    static uint32_t lastCheck = 0;

    if (!msg.text.length()) msg.start(String("Safe mode - open ") + WiFi.localIP().toString());
    if (millis() - lastFrame >= 30) {
        lastFrame = millis();
        msg.step();
    }
    // Keep looking for a fixed release and install it as soon as one is published.
    if (timeValid() && !otaRequest && (!lastCheck || millis() - lastCheck > SAFE_MODE_CHECK_MS)) {
        lastCheck = millis() | 1;
        otaRequest = OTA_INSTALL;
    }
    if (otaRequest) runOta();
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("\nESP8266 Matrix Clock " FW_VERSION));
    bool safe = safeModeCheck();
    Serial.printf("Firmware %u bytes, %u bytes free for updates\n", ESP.getSketchSize(), ESP.getFreeSketchSpace());
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    randomSeed(RANDOM_REG32);

    settingsLoad();   // first: module count, type and orientation are settings
    displayBegin();
    applyBrightness(true);

    WiFi.persistent(false);   // WiFi credentials live only in our settings, not in the SDK's own flash area
    char id[5];
    snprintf(id, sizeof(id), "%04X", (unsigned)(ESP.getChipId() & 0xFFFF));
    apName = String(AP_PREFIX) + id;
    webBegin();

    if (safe) {
        startSafeMode();
        return;
    }

    if (cfg.bootAnim) playAnim(cfg.bootAnim);
    scrollText(WELCOME_TEXT);

    if (!cfg.ssid[0] || !connectWiFi()) {
        startSetupMode();
        return;
    }

    Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
    WiFi.setAutoReconnect(true);
    startNtp();
    mdnsOn = MDNS.begin(HOSTNAME);
    if (mdnsOn) MDNS.addService("http", "tcp", 80);
    scrollText(String("IP ") + WiFi.localIP().toString());   // so a new owner knows where the settings page is
}

void loop() {
#ifdef CRASH_TEST
    if (appMode == MODE_CLOCK && millis() > 15000) {
        Serial.println(F("CRASH_TEST: simulated crash"));
        abort();
    }
#endif
    static bool stable = false;
    if (!stable && appMode != MODE_SAFE && millis() > SAFE_MODE_STABLE_MS) {
        stable = true;
        safeModeStable();
    }

    service();
    readLight();
    if (handleButton()) return;   // button hint on screen
    if      (appMode == MODE_SETUP) setupLoop();
    else if (appMode == MODE_SAFE)  safeLoop();
    else                            clockLoop();
}
