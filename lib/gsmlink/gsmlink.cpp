#include "gsmlink.h"
#include <Preferences.h>

static GsmNullStream &_gsmNullStream() {
    static GsmNullStream s;
    return s;
}

GsmLink::GsmLink(Stream &serial) : TinyGsm(serial), _wifiMode(false) {}

GsmLink::GsmLink(const char *ssid, const char *pass)
    : TinyGsm(_gsmNullStream()), _wifiMode(true), _ssid(ssid), _pass(pass) {}

GsmLink::~GsmLink() {
    // Client (ESP32 core) không có virtual destructor — không được delete qua Client*.
    // _wifiMode cố định cho cả object nên mọi slot cùng 1 kiểu cụ thể, cast đúng kiểu trước khi xoá.
    for (Client *c : _slots) {
        if (!c) continue;

        if (_wifiMode)
            delete static_cast<WiFiClient *>(c);
        else
            delete static_cast<TinyGsmClient *>(c);
    }
}

bool GsmLink::_wifiWait(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= timeoutMs) return false;
        delay(100);
    }
    return true;
}

bool GsmLink::restart() {
    if (_wifiMode) return true; // WiFi không có khái niệm "restart modem"
    return TinyGsm::restart();
}

bool GsmLink::waitForNetwork(uint32_t timeoutMs) {
    if (_wifiMode) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(_ssid.c_str(), _pass.c_str()); // re-associate nếu đã rớt trước đó
        return _wifiWait(timeoutMs);
    }
    return TinyGsm::waitForNetwork(timeoutMs);
}

bool GsmLink::gprsConnect(const char *apn, const char *user, const char *pass) {
    if (_wifiMode) return WiFi.status() == WL_CONNECTED; // WiFi đã sẵn sàng ngay sau waitForNetwork()
    return TinyGsm::gprsConnect(apn, user, pass);
}

bool GsmLink::isGprsConnected() {
    if (_wifiMode) return WiFi.status() == WL_CONNECTED;
    return TinyGsm::isGprsConnected();
}

String GsmLink::getModemInfo() {
    if (_wifiMode) return "WiFi " + WiFi.macAddress();
    return TinyGsm::getModemInfo();
}

Client &GsmLink::client(uint8_t mux) {
    if (mux >= 4) mux = 0;

    if (!_slots[mux]) {
        if (_wifiMode)
            _slots[mux] = new WiFiClient();
        else
            _slots[mux] = new TinyGsmClient(*this, mux);
    }

    return *_slots[mux];
}

// ── WiFi scan / test / credential helpers ─────────────────────────────────────

static bool _scanStarted = false; // đã start ít nhất 1 lần, chưa được poll() tiêu thụ kết quả

void gsmlink_wifi_scan_start() {
    if (_scanStarted && WiFi.scanComplete() == WIFI_SCAN_RUNNING) return; // đang chạy dở

    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true); // async — không block, trả về ngay
    _scanStarted = true;
}

bool gsmlink_wifi_scan_poll(GsmWifiAp *out, size_t maxResults, size_t *outCount) {
    if (!_scanStarted) return false;

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;

    _scanStarted = false;
    if (n < 0) n = 0; // WIFI_SCAN_FAILED
    if (n > 32) n = 32; // cap trước khi sort — dư so với top-N thường cần lấy

    int idx[32];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) { // insertion sort giảm dần theo RSSI
        int key = idx[i];
        int keyRssi = WiFi.RSSI(key);
        int j = i - 1;
        while (j >= 0 && WiFi.RSSI(idx[j]) < keyRssi) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }

    size_t top = ((size_t)n < maxResults) ? (size_t)n : maxResults;
    for (size_t i = 0; i < top; i++) {
        String ssid = WiFi.SSID(idx[i]);
        size_t len = ssid.length();
        if (len > 32) len = 32;
        memcpy(out[i].ssid, ssid.c_str(), len);
        out[i].ssid[len] = '\0';
        out[i].rssi = (int8_t)WiFi.RSSI(idx[i]);
        out[i].channel = (uint8_t)WiFi.channel(idx[i]);
    }
    WiFi.scanDelete();
    if (outCount) *outCount = top;
    return true;
}

static bool     _testActive = false;
static uint32_t _testStartMs = 0;
static uint32_t _testTimeoutMs = 0;

void gsmlink_wifi_test_start(const char *ssid, const char *pass, uint32_t timeoutMs) {
    if (_testActive) return; // đang có 1 lượt test dở dang

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    _testStartMs = millis();
    _testTimeoutMs = timeoutMs;
    _testActive = true;
}

bool gsmlink_wifi_test_poll(bool *outOk) {
    if (!_testActive) return false;

    bool done = false;
    bool ok = false;

    if (WiFi.status() == WL_CONNECTED) {
        done = true;
        ok = true;
    } else if (millis() - _testStartMs >= _testTimeoutMs) {
        done = true;
        ok = false;
    }

    if (!done) return false;

    _testActive = false;
    WiFi.disconnect(true);
    if (outOk) *outOk = ok;
    return true;
}

void gsmlink_wifi_cred_save(const char *ssid, const char *pass) {
    Preferences p;
    p.begin(GSMLINK_NVS_NS, false);
    p.putString(GSMLINK_NVS_KEY_SSID, ssid);
    p.putString(GSMLINK_NVS_KEY_PASS, pass);
    p.end();
}

String gsmlink_wifi_cred_ssid() {
    Preferences p;
    p.begin(GSMLINK_NVS_NS, true);
    String ssid = p.getString(GSMLINK_NVS_KEY_SSID, "");
    p.end();
    return ssid;
}

String gsmlink_wifi_cred_pass() {
    Preferences p;
    p.begin(GSMLINK_NVS_NS, true);
    String pass = p.getString(GSMLINK_NVS_KEY_PASS, "");
    p.end();
    return pass;
}

void gsmlink_wifi_cred_clear() {
    Preferences p;
    p.begin(GSMLINK_NVS_NS, false);
    p.remove(GSMLINK_NVS_KEY_SSID);
    p.remove(GSMLINK_NVS_KEY_PASS);
    p.end();
}
