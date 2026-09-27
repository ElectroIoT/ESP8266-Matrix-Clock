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

// ---- pages ---------------------------------------------------------------------

static void handleRoot() {
    server.send_P(200, "text/html", appMode == MODE_SETUP ? WIFI_HTML : MAIN_HTML);
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
               ",\"ssid\":" + jsonStr(sta ? WiFi.SSID() : String()) +
               ",\"ip\":" + jsonStr(sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) +
               ",\"rssi\":" + String(sta ? WiFi.RSSI() : 0) + ",\"up\":" + String(millis() / 1000) +
               ",\"version\":" + jsonStr(FW_VERSION) + ",\"latest\":" + jsonStr(ota.latest) +
               ",\"newer\":" + (ota.latest.length() && otaNewer(ota.latest) ? "true" : "false") +
               ",\"otaMsg\":" + jsonStr(ota.message) + ",\"otaBusy\":" + (otaRequest ? "true" : "false") + "}";
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
               ",\"pinUpload\":" + (strlen(UPDATE_PASS_SHA256) ? "true" : "false") + ",\"events\":[";
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
    server.on("/", HTTP_GET, handleRoot);
    server.on("/wifi", HTTP_GET, []() { server.send_P(200, "text/html", WIFI_HTML); });
    server.on("/style.css", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "max-age=86400");
        server.send_P(200, "text/css", STYLE_CSS);
    });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/config", HTTP_GET, handleConfig);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/api/show", HTTP_POST, handleShow);
    server.on("/update", HTTP_GET, []() { server.send_P(200, "text/html", UPDATE_HTML); });
    server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
    server.on("/api/ota/check", HTTP_POST, []() { otaRequest = OTA_CHECK; sendOk(); });
    server.on("/api/ota/install", HTTP_POST, []() { otaRequest = OTA_INSTALL; sendOk(); });
    server.on("/api/message", HTTP_POST, handleMessage);
    server.on("/api/event/add", HTTP_POST, handleEventAdd);
    server.on("/api/event/del", HTTP_POST, handleEventDelete);
    server.on("/api/event/show", HTTP_POST, handleEventShow);
    server.on("/api/digits", HTTP_POST, []() { digitDemo = true; sendOk(); });
    server.on("/api/anim", HTTP_POST, []() { animRequest = constrain(server.arg("n").toInt(), 0, (int)ANIM_RANDOM); sendOk(); });
    server.on("/api/sync", HTTP_POST, []() { startNtp(); sendOk(); });
    server.on("/api/restart", HTTP_POST, []() { sendOk(); scheduleRestart(800); });
    server.on("/api/factory", HTTP_POST, []() { settingsDefaults(); settingsSave(); sendOk(); scheduleRestart(800); });
    server.onNotFound(handleNotFound);
    server.collectHeaders("X-Pin");   // update password for /api/update
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
