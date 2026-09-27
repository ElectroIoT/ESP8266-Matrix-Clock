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

void applyBrightness(bool force) {
    uint8_t want = cfg.brightness;
    if (cfg.autoDim && timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        if (inNight(t.tm_hour)) want = cfg.nightBr;
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
    if (otaCheck() && req == OTA_INSTALL) otaInstall();
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

// FLASH button: short press = scroll IP, hold = forget WiFi and open setup
static void handleButton() {
    static uint32_t downAt = 0;
    static bool     held = false;
    bool down = digitalRead(PIN_BUTTON) == LOW;

    if (down) {
        if (!downAt) {
            downAt = millis() | 1;
            held = false;
        } else if (!held && millis() - downAt > BUTTON_HOLD_MS) {
            held = true;
            Serial.println(F("Button held: forgetting WiFi"));
            cfg.ssid[0] = 0;
            cfg.pass[0] = 0;
            settingsSave();
            scrollText("WiFi reset");
            restart();
        }
    } else if (downAt) {
        if (!held && millis() - downAt > 40 && appMode == MODE_CLOCK)
            scrollRequest = String("IP ") + WiFi.localIP().toString();
        downAt = 0;
    }
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
        face.reset();
        applyBrightness(true);
        showEventsToday();
        otaCheck();   // so the web page can tell whether an update is waiting
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

    displayBegin();
    settingsLoad();
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
    handleButton();
    if      (appMode == MODE_SETUP) setupLoop();
    else if (appMode == MODE_SAFE)  safeLoop();
    else                            clockLoop();
}
