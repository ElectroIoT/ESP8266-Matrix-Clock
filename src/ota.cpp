#include "ota.h"
#include "config.h"
#include "display.h"
#include "github_roots.h"
#include "safemode.h"
#include "web.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

// "latest" always points at the newest release; GitHub redirects it to the asset download.
static const char* URL_BASE = "https://github.com/" GITHUB_REPO "/releases/latest/download/";

OtaState ota;

// ---------------------------------------------------------------------------------
// Memory is the hard part on an ESP8266: ~40 KB of heap, one TLS connection needs
// ~17 KB, and every parsed root certificate a few KB more. So:
//  - each connection loads only the root set for the host it talks to, and
//  - redirects (github.com -> release-assets.githubusercontent.com) are followed by
//    hand, closing one connection before opening the next.
// Every connection verifies the server certificate, so only files really served by
// GitHub for this repo can be installed.
// ---------------------------------------------------------------------------------

static const uint32_t TLS_MIN_BLOCK = 18000;
static const uint32_t TLS_MIN_FREE  = 22000;

struct SecureClient {
    BearSSL::X509List         roots;
    BearSSL::WiFiClientSecure client;
    explicit SecureClient(const char* pemSet) : roots(pemSet) {
        client.setTrustAnchors(&roots);
        client.setX509Time(time(nullptr));   // certificate dates need the real time (NTP)
        client.setTimeout(15000);
    }
    bool enoughMemory() {
        uint32_t block = ESP.getMaxFreeBlockSize(), free = ESP.getFreeHeap();
        if (block >= TLS_MIN_BLOCK && free >= TLS_MIN_FREE) return true;
        ota.message = "Not enough memory right now, try again later";
        Serial.printf("OTA skipped: free %u, largest block %u\n", free, block);
        return false;   // skip instead of letting BearSSL abort, which would reboot the clock
    }
};

// Pauses the web server while TLS runs: queued browser requests would otherwise eat the heap.
struct WebPause {
    WebPause()  { webPause(true); }
    ~WebPause() { webPause(false); }
};

static String hostOf(const String& url) {
    int start = url.indexOf("://");
    start = start < 0 ? 0 : start + 3;
    int end = url.indexOf('/', start);
    return url.substring(start, end < 0 ? url.length() : end);
}

// Root set for a host; the other set is tried too, in case GitHub moves a host to another CA.
static const char* rootsFor(const String& host, bool other) {
    bool gh = host == "github.com" || host.endsWith(".github.com");
    return (gh != other) ? ROOTS_GITHUB : ROOTS_ASSETS;
}

// Follow redirects with HEAD requests, one connection at a time. Returns the final URL,
// or an empty string (ota.message says why).
static String resolve(String url) {
    static const char* HDRS[] = {"Location"};
    for (int hop = 0; hop < 5; hop++) {
        String host = hostOf(url);
        int code = -1;
        String location;
        for (int attempt = 0; attempt < 2 && code < 0; attempt++) {
            SecureClient sc(rootsFor(host, attempt == 1));
            if (!sc.enoughMemory()) return "";
            HTTPClient http;
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.setTimeout(15000);
            if (!http.begin(sc.client, url)) break;
            http.collectHeaders(HDRS, 1);
            code = http.sendRequest("HEAD");
            location = http.header("Location");
            http.end();
        }
        if (code == HTTP_CODE_OK) return url;
        if (code >= 300 && code < 400 && location.length()) {
            url = location;
            continue;
        }
        if (code == HTTP_CODE_NOT_FOUND) ota.message = "No release published yet";
        else ota.message = "Could not reach GitHub (" + HTTPClient::errorToString(code) + ")";
        Serial.printf("OTA: %s [%s]\n", ota.message.c_str(), host.c_str());
        return "";
    }
    ota.message = "Too many redirects";
    return "";
}

static bool ready() {
    if (WiFi.status() != WL_CONNECTED) {
        ota.message = "No WiFi";
        return false;
    }
    if (time(nullptr) < 1600000000) {
        ota.message = "Waiting for internet time";
        return false;
    }
    return true;
}

static void parseVersion(const String& v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    int part = 0;
    for (size_t i = 0; i < v.length() && part < 3; i++) {
        char c = v[i];
        if (c >= '0' && c <= '9') out[part] = out[part] * 10 + (c - '0');
        else if (c == '.') part++;
    }
}

bool otaNewer(const String& v) {
    int a[3], b[3];
    parseVersion(v, a);
    parseVersion(FW_VERSION, b);
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool otaCheck() {
    ota.checkedAt = millis() | 1;
    if (!ready()) return false;
    WebPause paused;

    String url = resolve(String(URL_BASE) + "version.txt");
    if (!url.length()) return false;

    String v;
    {
        SecureClient sc(rootsFor(hostOf(url), false));
        if (!sc.enoughMemory()) return false;
        HTTPClient http;
        http.setTimeout(15000);
        if (!http.begin(sc.client, url) || http.GET() != HTTP_CODE_OK) {
            ota.message = "Could not download version.txt";
            http.end();
            return false;
        }
        v = http.getString();
        http.end();
    }
    v.trim();
    if (v.startsWith("v") || v.startsWith("V")) v.remove(0, 1);
    if (!v.length() || v.length() > 16) {
        ota.message = "Bad version.txt in release";
        return false;
    }
    ota.latest = v;
    bool newer = otaNewer(v);
    ota.message = newer ? "Version " + v + " is available" : String("Up to date");
    Serial.printf("Update check: latest %s, running %s\n", v.c_str(), FW_VERSION);
    return newer;
}

void otaInstall() {
    if (!ready()) return;
    Serial.println(F("Installing update from GitHub"));
    fbClear();
    drawText(1, "Update");
    fbShow();
    WebPause paused;

    String url = resolve(String(URL_BASE) + "firmware.bin");
    if (!url.length()) {
        scrollText("Update failed");
        return;
    }

    SecureClient sc(rootsFor(hostOf(url), false));
    if (!sc.enoughMemory()) {
        scrollText("Update failed");
        return;
    }
    ESPhttpUpdate.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    ESPhttpUpdate.rebootOnUpdate(false);
    ESPhttpUpdate.setLedPin(-1);
    ESPhttpUpdate.onProgress([](int done, int total) {   // progress bar across the display
        if (total <= 0) return;
        fbClear();
        int len = (int64_t)done * W / total;
        for (int x = 0; x < len; x++)
            for (int y = 2; y < 6; y++) fbSet(x, y, true);
        for (int x = 0; x < W; x++) { fbSet(x, 0, true); fbSet(x, 7, true); }
        fbShow();
    });

    t_httpUpdate_return r = ESPhttpUpdate.update(sc.client, url);
    if (r == HTTP_UPDATE_OK) {
        Serial.println(F("Update OK, restarting"));
        scrollText("Updated!");
        restart();
    }
    ota.message = "Update failed: " + ESPhttpUpdate.getLastErrorString();
    Serial.println(ota.message);
    scrollText("Update failed");
}
