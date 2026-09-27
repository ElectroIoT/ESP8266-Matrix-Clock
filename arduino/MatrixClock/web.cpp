#include "web.h"
#include "app.h"
#include "config.h"
#include "display.h"
#include "ota.h"
#include "settings.h"
#include "web_pages.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include <bearssl/bearssl_hash.h>

static ESP8266WebServer server(80);
static DNSServer        dns;
static bool             dnsOn = false;

static String jsonStr(const String& s) {
    String o = "\"";
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}

static void sendOk() {
    server.send(200, "text/plain", "OK");
}

// ---- optional settings-page login ------------------------------------------------
// No password set (the default) = open page. With a password, the browser gets a
// session cookie after logging in; changing the password renews it (logs everyone out).
// Forgotten password: hold the FLASH button for 10 s.

static bool hasPassword() {
    for (uint8_t b : cfg.adminHash)
        if (b) return true;
    return false;
}

static void hashPassword(const String& pw, const uint8_t salt[16], uint8_t out[32]) {
    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, salt, 16);
    br_sha256_update(&ctx, pw.c_str(), pw.length());
    br_sha256_out(&ctx, out);
}

static bool sameBytes(const uint8_t* a, const uint8_t* b, size_t n) {   // constant time
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

static String sessionHex() {
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + 2 * i, "%02x", cfg.sessionKey[i]);
    return hex;
}

static bool authed() {
    if (!hasPassword()) return true;
    String c = server.header("Cookie");
    int i = c.indexOf("mc_session=");
    if (i < 0) return false;
    String v = c.substring(i + 11, i + 11 + 32);
    String want = sessionHex();
    return v.length() == 32 && sameBytes((const uint8_t*)v.c_str(), (const uint8_t*)want.c_str(), 32);
}

static void setSessionCookie(bool valid) {
    server.sendHeader("Set-Cookie", valid ? "mc_session=" + sessionHex() + "; Path=/; Max-Age=31536000; HttpOnly; SameSite=Strict"
                                          : String("mc_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"));
}

// Who may call a route without logging in.
enum Access : uint8_t {
    OPEN,         // always (login, status, style, own-password firmware upload)
    SETUP_OPEN,   // in WiFi setup mode (so a new owner can connect the clock)
    SAFE_OPEN,    // in safe mode (only official GitHub releases can be installed)
    LOGIN,        // needs login when a password is set
};

// Plain function pointer (not std::function): keeps the wrapper small enough to need no heap
// per route, which matters on a ~40 KB heap that also has to hold a TLS connection.
static void route(const char* uri, HTTPMethod method, void (*fn)(), Access access = LOGIN) {
    server.on(uri, method, [fn, access]() {
        bool ok = access == OPEN || (access == SETUP_OPEN && appMode == MODE_SETUP) ||
                  (access == SAFE_OPEN && appMode == MODE_SAFE) || authed();
        if (!ok) {
            server.send(401, "text/plain", "Login required");
            return;
        }
        fn();
    });
}

static uint8_t  loginFails = 0;
static uint32_t loginLockedUntil = 0;

static void handleLogin() {
    if (loginLockedUntil && (int32_t)(millis() - loginLockedUntil) < 0) {
        server.send(429, "text/plain", "Too many wrong passwords - wait a minute");
        return;
    }
    uint8_t h[32];
    hashPassword(server.arg("pw"), cfg.adminSalt, h);
    if (!hasPassword() || sameBytes(h, cfg.adminHash, 32)) {
        loginFails = 0;
        setSessionCookie(true);
        sendOk();
        return;
    }
    if (++loginFails >= 5) {
        loginFails = 0;
        loginLockedUntil = millis() + 60000;
    }
    delay(400);   // slows down guessing
    server.send(403, "text/plain", "Wrong password");
}

// POST /api/password  cur=<current> new=<new password, empty = remove the password>
static void handlePassword() {
    if (hasPassword()) {
        uint8_t h[32];
        hashPassword(server.arg("cur"), cfg.adminSalt, h);
        if (!sameBytes(h, cfg.adminHash, 32)) {
            delay(400);
            server.send(403, "text/plain", "Current password is wrong");
            return;
        }
    }
    String pw = server.arg("new");
    if (!pw.length()) {
        memset(cfg.adminHash, 0, sizeof(cfg.adminHash));
        settingsSave();
        setSessionCookie(false);
        sendOk();
        return;
    }
    if (pw.length() < 6 || pw.length() > 64) {
        server.send(400, "text/plain", "Use 6 to 64 characters");
        return;
    }
    ESP.random(cfg.adminSalt, sizeof(cfg.adminSalt));
    ESP.random(cfg.sessionKey, sizeof(cfg.sessionKey));
    hashPassword(pw, cfg.adminSalt, cfg.adminHash);
    settingsSave();
    setSessionCookie(true);   // this browser stays logged in, every other one is logged out
    sendOk();
}

// ---- pages ---------------------------------------------------------------------

static void handleRoot() {
    const char* page = appMode == MODE_SETUP ? WIFI_HTML : appMode == MODE_SAFE ? SAFE_HTML
                     : authed() ? MAIN_HTML : LOGIN_HTML;
    server.send_P(200, "text/html", page);
}

// Captive portal: phones probe URLs like /generate_204 or /hotspot-detect.html;
// redirecting them to our page makes the "Sign in to network" screen pop up.
static void handleNotFound() {
    if (appMode == MODE_SETUP) {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

// ---- API -----------------------------------------------------------------------

static void handleStatus() {
    char t[12] = "--:--", ap[4] = "", d[40] = "";
    if (timeValid()) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(t, sizeof(t), cfg.fmt24 ? "%H:%M:%S" : "%I:%M:%S", &lt);
        if (!cfg.leadingZero && t[0] == '0') memmove(t, t + 1, strlen(t));
        if (!cfg.fmt24) strftime(ap, sizeof(ap), "%p", &lt);
        strftime(d, sizeof(d), "%A, %d %B %Y", &lt);
    }
    bool sta = WiFi.status() == WL_CONNECTED;
    String j = String("{\"time\":") + jsonStr(t) + ",\"ampm\":" + jsonStr(ap) + ",\"date\":" + jsonStr(d) +
               ",\"synced\":" + (timeValid() ? "true" : "false") +
               ",\"ssid\":" + jsonStr(sta && authed() ? WiFi.SSID() : String()) +   // no network name before login
               ",\"ip\":" + jsonStr(sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) +
               ",\"rssi\":" + String(sta ? WiFi.RSSI() : 0) + ",\"up\":" + String(millis() / 1000) +
               ",\"version\":" + jsonStr(FW_VERSION) + ",\"latest\":" + jsonStr(ota.latest) +
               ",\"newer\":" + (ota.latest.length() && otaNewer(ota.latest) ? "true" : "false") +
               ",\"otaMsg\":" + jsonStr(ota.message) + ",\"otaBusy\":" + (otaRequest ? "true" : "false") +
               ",\"safe\":" + (appMode == MODE_SAFE ? "true" : "false") +
               ",\"light\":" + String(lightPercent()) + ",\"brNow\":" + String(brightnessNow()) + "}";
    server.send(200, "application/json", j);
}

static void handleConfig() {
    String j = String("{\"br\":") + cfg.brightness + ",\"autoDim\":" + cfg.autoDim + ",\"nightFrom\":" + cfg.nightFrom +
               ",\"nightTo\":" + cfg.nightTo + ",\"nightBr\":" + cfg.nightBr + ",\"fmt24\":" + cfg.fmt24 +
               ",\"leadingZero\":" + cfg.leadingZero + ",\"blinkColon\":" + cfg.blinkColon +
               ",\"secondsBar\":" + cfg.secondsBar + ",\"showDate\":" + cfg.showDate + ",\"roll\":" + cfg.roll +
               ",\"hourlyAnim\":" + cfg.hourlyAnim + ",\"bootAnim\":" + cfg.bootAnim +
               ",\"tz\":" + jsonStr(cfg.tz) + ",\"ssid\":" + jsonStr(cfg.ssid) +
               ",\"autoUpdate\":" + cfg.autoUpdate + ",\"msg\":" + jsonStr(cfg.msg) + ",\"msgEvery\":" + cfg.msgEvery +
               ",\"pinUpload\":" + (strlen(UPDATE_PASS_SHA256) ? "true" : "false") +
               ",\"ldrOn\":" + cfg.ldrOn + ",\"ldrMin\":" + cfg.ldrMin + ",\"ldrMax\":" + cfg.ldrMax +
               ",\"ldrInvert\":" + cfg.ldrInvert + ",\"modules\":" + cfg.modules + ",\"hwType\":" + cfg.hwType +
               ",\"flipH\":" + cfg.flipH + ",\"flipV\":" + cfg.flipV + ",\"showSecs\":" + cfg.showSecs + ",\"hasPassword\":" + (hasPassword() ? "true" : "false") +
               ",\"events\":[";
    bool first = true;
    for (int i = 0; i < MAX_EVENTS; i++) {
        const Event& e = cfg.events[i];
        if (!e.month) continue;
        if (!first) j += ',';
        first = false;
        j += String("{\"i\":") + i + ",\"m\":" + e.month + ",\"d\":" + e.day + ",\"a\":" + e.anim + ",\"t\":" + jsonStr(e.text) + "}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

// POST /api/set  k=<setting> v=<value> [live=1 -> apply without writing flash]
static void handleSet() {
    String k = server.arg("k"), v = server.arg("v");
    int n = v.toInt();
    auto flag = [&](uint8_t& f) { f = n ? 1 : 0; };

    if      (k == "br")          cfg.brightness = constrain(n, 0, 15);
    else if (k == "autoDim")     flag(cfg.autoDim);
    else if (k == "nightFrom")   cfg.nightFrom = constrain(n, 0, 23);
    else if (k == "nightTo")     cfg.nightTo = constrain(n, 0, 23);
    else if (k == "nightBr")     cfg.nightBr = constrain(n, 0, 16);
    else if (k == "fmt24")       flag(cfg.fmt24);
    else if (k == "leadingZero") flag(cfg.leadingZero);
    else if (k == "blinkColon")  flag(cfg.blinkColon);
    else if (k == "secondsBar")  flag(cfg.secondsBar);
    else if (k == "showDate")    flag(cfg.showDate);
    else if (k == "autoUpdate")  flag(cfg.autoUpdate);
    else if (k == "ldrOn")       flag(cfg.ldrOn);
    else if (k == "ldrInvert")   flag(cfg.ldrInvert);
    else if (k == "ldrMin")      cfg.ldrMin = constrain(n, 0, 15);
    else if (k == "ldrMax")      cfg.ldrMax = constrain(n, 0, 15);
    else if (k == "flipH")       flag(cfg.flipH);
    else if (k == "flipV")       flag(cfg.flipV);
    else if (k == "showSecs")    flag(cfg.showSecs);
    else if (k == "modules" || k == "hwType") {   // the display driver is set up at boot: restart to apply
        if (k == "modules") cfg.modules = constrain(n, (int)MIN_MODULES, (int)MAX_MODULES);
        else                cfg.hwType = constrain(n, 0, (int)MT_ICSTATION);
        settingsSave();
        server.send(200, "text/plain", "Restarting to apply");
        scheduleRestart(1000);
        return;
    }
    else if (k == "roll")        { cfg.roll = constrain(n, 0, (int)ROLL_RANDOM); digitDemo = true; }
    else if (k == "hourlyAnim")  cfg.hourlyAnim = constrain(n, 0, (int)ANIM_RANDOM);
    else if (k == "bootAnim")    cfg.bootAnim = constrain(n, 0, (int)ANIM_RANDOM);
    else if (k == "tz") {
        if (!v.length() || v.length() >= sizeof(cfg.tz)) { server.send(400, "text/plain", "Invalid time zone"); return; }
        strlcpy(cfg.tz, v.c_str(), sizeof(cfg.tz));
        setenv("TZ", cfg.tz, 1);
        tzset();
    } else {
        server.send(400, "text/plain", "Unknown setting");
        return;
    }
    if (!server.hasArg("live")) settingsSave();
    applyBrightness(true);
    sendOk();
}

static void handleScan() {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        bool weakerDuplicate = false;   // mesh / dual-band routers list the same name several times
        for (int k = 0; k < n && !weakerDuplicate; k++)
            if (k != i && WiFi.SSID(k) == s && (WiFi.RSSI(k) > WiFi.RSSI(i) || (WiFi.RSSI(k) == WiFi.RSSI(i) && k < i)))
                weakerDuplicate = true;
        if (weakerDuplicate) continue;
        if (j.length() > 1) j += ',';
        j += String("{\"s\":") + jsonStr(s) + ",\"r\":" + WiFi.RSSI(i) + ",\"l\":" +
             (WiFi.encryptionType(i) == ENC_TYPE_NONE ? "0" : "1") + "}";
    }
    WiFi.scanDelete();
    server.send(200, "application/json", j + "]");
}

static void handleWifi() {
    String s = server.arg("ssid"), p = server.arg("pass");
    if (!s.length() || s.length() > 32) { server.send(400, "text/plain", "Enter the network name"); return; }
    if (p.length() > 64)                { server.send(400, "text/plain", "Password is too long"); return; }
    strlcpy(cfg.ssid, s.c_str(), sizeof(cfg.ssid));
    strlcpy(cfg.pass, p.c_str(), sizeof(cfg.pass));
    settingsSave();
    sendOk();
    scheduleRestart(1500);
}

static void handleShow() {
    scrollRequest = String("IP ") + WiFi.localIP().toString();
    sendOk();
}

// POST /api/message  t=<text> every=<minutes, 0 = off> [show=1 -> scroll it now]
static void handleMessage() {
    String t = server.arg("t");
    t.trim();
    if (t.length() >= sizeof(cfg.msg)) { server.send(400, "text/plain", "Message is too long"); return; }
    strlcpy(cfg.msg, t.c_str(), sizeof(cfg.msg));
    int every = server.arg("every").toInt();
    cfg.msgEvery = (every == 1 || every == 5 || every == 15 || every == 30 || every == 60) ? every : 0;
    settingsSave();
    if (server.arg("show") == "1" && cfg.msg[0]) scrollRequest = cfg.msg;
    sendOk();
}

// POST /api/event/add  m=<1..12> d=<1..31> a=<Anim> t=<text>
static void handleEventAdd() {
    int m = server.arg("m").toInt(), d = server.arg("d").toInt(), a = server.arg("a").toInt();
    String t = server.arg("t");
    t.trim();
    static const uint8_t DAYS[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12 || d < 1 || d > DAYS[m - 1]) { server.send(400, "text/plain", "Invalid date"); return; }
    if (!t.length() || t.length() > 40)               { server.send(400, "text/plain", "Text must be 1 to 40 characters"); return; }
    for (Event& e : cfg.events) {
        if (e.month) continue;
        e.month = m;
        e.day = d;
        e.anim = constrain(a, 0, (int)ANIM_RANDOM);
        strlcpy(e.text, t.c_str(), sizeof(e.text));
        settingsSave();
        sendOk();
        return;
    }
    server.send(400, "text/plain", "All 8 special days are used - delete one first");
}

static void handleEventDelete() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= MAX_EVENTS) { server.send(400, "text/plain", "Invalid entry"); return; }
    memset(&cfg.events[i], 0, sizeof(Event));
    settingsSave();
    sendOk();
}

static void handleEventShow() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= MAX_EVENTS || !cfg.events[i].month) { server.send(400, "text/plain", "Invalid entry"); return; }
    if (cfg.events[i].anim) animRequest = cfg.events[i].anim;
    scrollRequest = cfg.events[i].text;
    sendOk();
}

// ---- manual firmware upload (password protected) ----------------------------------------

static bool passwordOk(const String& pass) {
    if (!pass.length()) return false;
    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, pass.c_str(), pass.length());
    uint8_t h[32];
    br_sha256_out(&ctx, h);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
    return strcasecmp(hex, UPDATE_PASS_SHA256) == 0;
}

static String  uploadError;
static bool    uploadAuthed = false;
static uint8_t pinFailures = 0;

static void handleUploadDone() {
    bool ok = uploadAuthed && !uploadError.length() && !Update.hasError();
    if (!ok && !uploadError.length()) uploadError = "Update failed";
    server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : uploadError);
    if (ok) {
        scrollRequest = "";
        scheduleRestart(1000);
    }
}

static void handleUploadChunk() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        uploadError = "";
        uploadAuthed = false;
        if (!strlen(UPDATE_PASS_SHA256))            { uploadError = "Upload is disabled in this firmware"; return; }
        if (pinFailures >= 5)               { uploadError = "Too many wrong passwords - restart the clock to try again"; return; }
        if (!passwordOk(server.header("X-Pin"))) {
            pinFailures++;
            uploadError = "Wrong update password";
            return;
        }
        pinFailures = 0;
        uploadAuthed = true;
        fbClear();
        drawText(1, "Update");
        fbShow();
        uint32_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(space)) uploadError = "Not enough space: " + Update.getErrorString();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (uploadAuthed && !uploadError.length() && Update.write(up.buf, up.currentSize) != up.currentSize)
            uploadError = "Write failed: " + Update.getErrorString();
    } else if (up.status == UPLOAD_FILE_END) {
        if (uploadAuthed && !uploadError.length() && !Update.end(true))
            uploadError = "Invalid firmware: " + Update.getErrorString();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (uploadAuthed) Update.end(false);
        uploadError = "Upload aborted";
    }
}

// ---- setup -----------------------------------------------------------------------

void webBegin() {
    // pages
    route("/", HTTP_GET, handleRoot, OPEN);   // serves the login page itself when needed
    route("/login", HTTP_GET, []() { server.send_P(200, "text/html", LOGIN_HTML); }, OPEN);
    route("/style.css", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "max-age=86400");
        server.send_P(200, "text/css", STYLE_CSS);
    }, OPEN);
    route("/wifi", HTTP_GET, []() { server.send_P(200, "text/html", WIFI_HTML); }, SETUP_OPEN);
    route("/update", HTTP_GET, []() { server.send_P(200, "text/html", UPDATE_HTML); }, OPEN);

    // open API
    route("/api/status", HTTP_GET, handleStatus, OPEN);
    route("/api/login", HTTP_POST, handleLogin, OPEN);
    route("/api/logout", HTTP_POST, []() { setSessionCookie(false); sendOk(); }, OPEN);
    server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);   // has its own update password
    route("/api/scan", HTTP_GET, handleScan, SETUP_OPEN);
    route("/api/wifi", HTTP_POST, handleWifi, SETUP_OPEN);
    route("/api/ota/check", HTTP_POST, []() { otaRequest = OTA_CHECK; sendOk(); }, SAFE_OPEN);
    route("/api/ota/install", HTTP_POST, []() { otaRequest = OTA_INSTALL; sendOk(); }, SAFE_OPEN);
    route("/api/restart", HTTP_POST, []() { sendOk(); scheduleRestart(800); }, SAFE_OPEN);

    // settings API (login needed when a password is set)
    route("/api/config", HTTP_GET, handleConfig);
    route("/api/set", HTTP_POST, handleSet);
    route("/api/password", HTTP_POST, handlePassword);
    route("/api/show", HTTP_POST, handleShow);
    route("/api/message", HTTP_POST, handleMessage);
    route("/api/event/add", HTTP_POST, handleEventAdd);
    route("/api/event/del", HTTP_POST, handleEventDelete);
    route("/api/event/show", HTTP_POST, handleEventShow);
    route("/api/digits", HTTP_POST, []() { digitDemo = true; sendOk(); });
    route("/api/modules", HTTP_POST, []() { moduleTest = true; sendOk(); });
    route("/api/anim", HTTP_POST, []() { animRequest = constrain(server.arg("n").toInt(), 0, (int)ANIM_RANDOM); sendOk(); });
    route("/api/sync", HTTP_POST, []() { startNtp(); sendOk(); });
    route("/api/factory", HTTP_POST, []() { settingsDefaults(); settingsSave(); sendOk(); scheduleRestart(800); });

    server.onNotFound(handleNotFound);
    server.collectHeaders("X-Pin", "Cookie");   // update password, login session
    server.begin();
}

void webStartPortal() {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    dnsOn = true;
}

void webPause(bool pause) {
    if (pause) server.stop();
    else       server.begin();
}

void webLoop() {
    if (dnsOn) dns.processNextRequest();
    server.handleClient();
}
