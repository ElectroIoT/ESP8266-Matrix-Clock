#include "web.h"
#include "app.h"
#include "settings.h"
#include "web_pages.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

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
               ",\"rssi\":" + String(sta ? WiFi.RSSI() : 0) + ",\"up\":" + String(millis() / 1000) + "}";
    server.send(200, "application/json", j);
}

static void handleConfig() {
    String j = String("{\"br\":") + cfg.brightness + ",\"autoDim\":" + cfg.autoDim + ",\"nightFrom\":" + cfg.nightFrom +
               ",\"nightTo\":" + cfg.nightTo + ",\"nightBr\":" + cfg.nightBr + ",\"fmt24\":" + cfg.fmt24 +
               ",\"leadingZero\":" + cfg.leadingZero + ",\"blinkColon\":" + cfg.blinkColon +
               ",\"secondsBar\":" + cfg.secondsBar + ",\"showDate\":" + cfg.showDate + ",\"roll\":" + cfg.roll +
               ",\"hourlyAnim\":" + cfg.hourlyAnim + ",\"bootAnim\":" + cfg.bootAnim +
               ",\"tz\":" + jsonStr(cfg.tz) + ",\"ssid\":" + jsonStr(cfg.ssid) + "}";
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
    server.on("/api/digits", HTTP_POST, []() { digitDemo = true; sendOk(); });
    server.on("/api/anim", HTTP_POST, []() { animRequest = constrain(server.arg("n").toInt(), 0, (int)ANIM_RANDOM); sendOk(); });
    server.on("/api/sync", HTTP_POST, []() { startNtp(); sendOk(); });
    server.on("/api/restart", HTTP_POST, []() { sendOk(); scheduleRestart(800); });
    server.on("/api/factory", HTTP_POST, []() { settingsDefaults(); settingsSave(); sendOk(); scheduleRestart(800); });
    server.onNotFound(handleNotFound);
    server.begin();
}

void webStartPortal() {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    dnsOn = true;
}

void webLoop() {
    if (dnsOn) dns.processNextRequest();
    server.handleClient();
}
