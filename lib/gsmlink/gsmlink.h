#pragma once
#include <Arduino.h>
#include <TinyGsmClient.h>
#include <WiFi.h>

#ifndef GSMLINK_NVS_NS
#define GSMLINK_NVS_NS "mesh"
#endif
#ifndef GSMLINK_NVS_KEY_SSID
#define GSMLINK_NVS_KEY_SSID "ssid"
#endif
#ifndef GSMLINK_NVS_KEY_PASS
#define GSMLINK_NVS_KEY_PASS "wpass"
#endif

// Stream giả — GsmLink ở WiFi mode vẫn phải bind 1 Stream& cho base TinyGsm (reference member,
// không thể để trống), nhưng không bao giờ thực sự đọc/ghi qua nó.
class GsmNullStream : public Stream {
  public:
    int    available() override { return 0; }
    int    read() override { return -1; }
    int    peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};

// Network link 2 chế độ, chốt tại construction: modem thật qua Serial, hoặc WiFi STA.
//
// Kế thừa TinyGsm — mọi lời gọi hiện có (restart/waitForNetwork/gprsConnect/isGprsConnected/
// getModemInfo) vẫn compile không đổi khi type đổi từ TinyGsm sang GsmLink; các hàm này được
// shadow lại để tự route theo mode đang active.
//
// CẢNH BÁO channel WiFi vs ESP-NOW: ESP-NOW dùng chung driver WiFi. Nếu 1 thiết bị vừa chạy
// ESP-NOW mesh vừa dùng GsmLink ở WiFi mode, WiFi STA connect sẽ đổi channel radio theo AP —
// ESP-NOW với peer khác channel sẽ ngưng hoạt động (im lặng, không log lỗi). Chỉ dùng WiFi mode
// trên thiết bị KHÔNG chạy ESP-NOW cùng lúc, hoặc tự pin lại channel ESP-NOW khớp AP sau khi
// connect (esp_wifi_set_channel) nếu bắt buộc chạy song song.
class GsmLink : public TinyGsm {
  public:
    explicit GsmLink(Stream &serial);           // modem-only
    GsmLink(const char *ssid, const char *pass); // wifi-only
    ~GsmLink();

    bool   restart();
    bool   waitForNetwork(uint32_t timeoutMs = 60000UL);
    bool   gprsConnect(const char *apn, const char *user = nullptr, const char *pass = nullptr);
    bool   isGprsConnected();
    String getModemInfo();

    bool isWifiMode() const {
        return _wifiMode;
    }

    // Client cho socket I/O. mux tách kênh khi ở modem mode (TinyGsmClient theo mux riêng);
    // WiFi mode bỏ qua mux, mỗi mux vẫn trả về 1 WiFiClient độc lập.
    Client &client(uint8_t mux = 0);

  private:
    bool    _wifiMode;
    String  _ssid;
    String  _pass;
    Client *_slots[4] = {nullptr, nullptr, nullptr, nullptr};

    bool _wifiWait(uint32_t timeoutMs);
};

// ── WiFi scan / test / credential helpers ─────────────────────────────────────
// Dùng chung bởi BLE activation và serial debug cmd — tránh lặp logic scan/test/NVS ở 2 nơi.

struct GsmWifiAp {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
};

// Scan native ESP32 (không qua modem) — async, KHÔNG block. WiFi.scanNetworks() chiếm radio
// (channel hopping) suốt 2-6s; ESP-NOW dùng chung radio nên scan đồng bộ sẽ làm mesh mất kết nối
// tạm thời với peer nếu gọi lúc mesh đang chạy. Gọi start() rồi poll() mỗi tick từ _loop().
//
// start(): kích hoạt scan. Gọi lại khi đang scan dở (WiFi.scanComplete()==RUNNING) là no-op —
// không huỷ/reset scan hiện tại.
void gsmlink_wifi_scan_start();

// poll(): gọi mỗi tick. Trả false nếu chưa start hoặc còn đang chạy (gọi lại tick sau). Trả true
// khi đã có kết quả (kể cả fail/0 AP) — out/outCount chỉ được ghi khi trả true, tối đa maxResults
// AP mạnh nhất (sort giảm dần RSSI).
bool gsmlink_wifi_scan_poll(GsmWifiAp *out, size_t maxResults, size_t *outCount);

// Test-connect async — WiFi.begin() cũng đổi channel radio theo AP giống scan, không được block
// nếu mesh có thể đang chạy. KHÔNG đụng NVS. Gọi start() rồi poll() mỗi tick từ _loop().
//
// start(): kích hoạt connect. Gọi lại khi đang test dở là no-op — không huỷ/reset lượt đang chạy.
void gsmlink_wifi_test_start(const char *ssid, const char *pass, uint32_t timeoutMs = 15000);

// poll(): gọi mỗi tick. Trả false nếu chưa start hoặc còn đang chờ (gọi lại tick sau). Trả true
// khi xong (connect OK hoặc hết timeoutMs) — *outOk chỉ được ghi khi trả true.
bool gsmlink_wifi_test_poll(bool *outOk);

// Credential WiFi STA, lưu NVS GSMLINK_NVS_NS (mặc định "mesh") — dùng lại nguyên vẹn sau reboot.
void   gsmlink_wifi_cred_save(const char *ssid, const char *pass);
String gsmlink_wifi_cred_ssid();
String gsmlink_wifi_cred_pass();
void   gsmlink_wifi_cred_clear();
