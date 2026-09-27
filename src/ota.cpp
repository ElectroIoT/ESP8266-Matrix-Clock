#include "ota.h"
#include "config.h"
#include "display.h"
#include "web.h"
#include "github_roots.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

// "latest" always points at the newest release; GitHub redirects it to the asset download.
static const char* URL_BASE = "https://github.com/" GITHUB_REPO "/releases/latest/download/";

OtaState ota;

// Every connection verifies the server certificate against the roots in github_roots.h,
// so only files really served by GitHub for this repo can be installed.
struct SecureClient {
    BearSSL::X509List        roots{GITHUB_ROOTS};
    BearSSL::WiFiClientSecure client;
    SecureClient() {
        client.setTrustAnchors(&roots);
        client.setX509Time(time(nullptr));   // certificate dates need the real time (NTP)
        client.setTimeout(15000);
    }
};

// TLS needs one ~17.3 KB I/O buffer plus ~9 KB of working memory. If the heap can't give
// that, skip this round instead of letting BearSSL abort (which would reboot the clock).
static const uint32_t TLS_MIN_BLOCK = 18000;
static const uint32_t TLS_MIN_FREE  = 26000;
static bool enoughMemory(const SecureClient&) {
    uint32_t block = ESP.getMaxFreeBlockSize(), free = ESP.getFreeHeap();
    if (block >= TLS_MIN_BLOCK && free >= TLS_MIN_FREE) return true;
    ota.message = "Not enough memory for the update check, try again later";
    Serial.printf("OTA skipped: free %u, largest block %u\n", free, block);
    return false;
}

// Pauses the web server while TLS runs: queued browser requests would otherwise eat the heap.
struct WebPause {
    WebPause()  { webPause(true); }
    ~WebPause() { webPause(false); }
};

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
    SecureClient sc;
    if (!enoughMemory(sc)) return false;
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(sc.client, String(URL_BASE) + "version.txt")) {
        ota.message = "Could not reach GitHub";
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ota.message = code == HTTP_CODE_NOT_FOUND ? String("No release published yet")
                                                  : "GitHub check failed (" + http.errorToString(code) + ")";
        http.end();
        char err[80] = "";
        int tlsErr = sc.client.getLastSSLError(err, sizeof(err));
        Serial.printf("%s  [TLS %d: %s, free heap %u]\n", ota.message.c_str(), tlsErr, err, ESP.getFreeHeap());
        return false;
    }
    String v = http.getString();
    http.end();
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
    SecureClient sc;
    if (!enoughMemory(sc)) {
        scrollText("Update failed");
        return;
    }
    ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
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

    t_httpUpdate_return r = ESPhttpUpdate.update(sc.client, String(URL_BASE) + "firmware.bin");
    if (r == HTTP_UPDATE_OK) {
        Serial.println(F("Update OK, restarting"));
        scrollText("Updated!");
        ESP.restart();
    }
    ota.message = "Update failed: " + ESPhttpUpdate.getLastErrorString();
    Serial.println(ota.message);
    scrollText("Update failed");
}
