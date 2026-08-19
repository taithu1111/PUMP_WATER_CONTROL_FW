# gsmlink

`GsmLink : public TinyGsm` — network link 2 chế độ (modem qua Serial / WiFi STA), chốt tại
construction, kế thừa thẳng `TinyGsm` nên code gọi `TinyGsm` hiện có (mqtt, http...) không cần
sửa khi đổi type khai báo từ `TinyGsm` sang `GsmLink`.

Chỉ dùng ở **node** (`src_node`) — root vẫn dùng `TinyGsm` trần, không cần WiFi transport.

---

## Vì sao kế thừa thay vì wrap

`TinyGsm`/`TinyGsmClient` không phải interface ảo (CRTP, resolve tại compile-time) — không thể
polymorph swap bằng con trỏ base. Kế thừa cho phép:
- Mọi lời gọi hiện có (`restart()`, `waitForNetwork()`, `gprsConnect()`, `isGprsConnected()`,
  `getModemInfo()`) **compile không đổi**, chỉ cần đổi type khai báo biến.
- Các hàm trên được **shadow lại** trong `GsmLink` để tự route theo mode đang active — không có
  virtual dispatch, dựa vào việc caller luôn cầm biến kiểu `GsmLink` (không phải `TinyGsm*`).

Điểm kế thừa **không đủ**: tạo socket I/O (`TinyGsmClient` vs `WiFiClient`) — 2 cơ chế hoàn toàn
khác nhau (AT mux vs TCP stack native), không thể "kế thừa cho qua". Dùng `client(mux)` cho việc
này thay vì tự `new TinyGsmClient(...)`.

## API

```cpp
#include <gsmlink.h>

GsmLink modem(_simSerial);    // modem-only
GsmLink modem(ssid, pass);    // wifi-only

modem.restart();              // no-op nếu wifi mode
modem.waitForNetwork(timeoutMs); // WiFi.begin / TinyGsm::waitForNetwork tuỳ mode
modem.gprsConnect(apn, user, pass); // check WiFi.status() nếu wifi mode
modem.isGprsConnected();
modem.getModemInfo();         // "WiFi <mac>" nếu wifi mode
modem.isWifiMode();

Client &c = modem.client(mux); // TinyGsmClient hoặc WiFiClient, mux 0-3
```

Mode chốt hẳn theo constructor được gọi, không tự đổi giữa chừng. Rớt mạng không tự chuyển
transport — cứ để retry loop của caller xử lý qua `waitForNetwork()`/`gprsConnect()` (đúng
transport đã chọn, tự reconnect lại chính nó).

## WiFi scan / test / credential — dùng chung BLE + serial cmd

```cpp
// Scan async — WiFi.scanNetworks() chiếm radio ~2-6s (channel hopping), cùng driver với ESP-NOW
// nên KHÔNG được gọi bản blocking nếu mesh có thể đang chạy. start() rồi poll() mỗi tick.
gsmlink_wifi_scan_start();
// ... mỗi tick trong _loop():
GsmWifiAp aps[10];
size_t    n = 0;
if (gsmlink_wifi_scan_poll(aps, 10, &n)) {
    // xong — n AP mạnh nhất, sort giảm dần RSSI
}

// Test-connect async — cùng lý do không được block như scan (WiFi.begin() cũng đổi channel).
gsmlink_wifi_test_start(ssid, pass);
// ... mỗi tick trong _loop():
bool ok = false;
if (gsmlink_wifi_test_poll(&ok)) {
    // xong — ok = connect thành công hay hết timeout. KHÔNG đụng NVS.
}

if (ok) gsmlink_wifi_cred_save(ssid, pass);    // lưu NVS GSMLINK_NVS_NS ("mesh" mặc định)
String saved = gsmlink_wifi_cred_ssid();       // đọc lại (dùng khi dựng GsmLink ở boot)
gsmlink_wifi_cred_clear();                     // xoá — gọi khi đổi sang mode khác wifi
```

Cả `ble_active.cpp` và `node_cmd.cpp` gọi thẳng các hàm này thay vì tự implement lại scan/test/NVS
— tránh 2 bản logic giống hệt nhau lặp lại giữa BLE và serial debug cmd.

## Cảnh báo: WiFi STA vs ESP-NOW cùng lúc

ESP-NOW dùng chung driver WiFi với STA. Nếu 1 thiết bị vừa chạy ESP-NOW mesh vừa dùng `GsmLink` ở
WiFi mode, WiFi STA connect vào AP sẽ đổi channel radio theo AP — ESP-NOW với peer ở channel khác
sẽ **ngưng hoạt động âm thầm** (không log lỗi). Node an toàn vì mesh mode và MQTT-mode (dù modem
hay WiFi) đã loại trừ nhau theo `NVS_NS_DEV/mode`. Nếu dùng ở thiết bị nào chạy ESP-NOW song song,
phải tự pin lại channel ESP-NOW khớp AP sau khi connect (`esp_wifi_set_channel`).

`gsmlink_wifi_scan_start()`/`WiFi.scanNetworks()` cũng chiếm radio (channel hopping qua từng kênh)
suốt vài giây, và `gsmlink_wifi_test_start()`/`WiFi.begin()` tự nó chính là WiFi STA connect nói
trên — cả 2 đều làm rớt ESP-NOW tạm thời với mọi peer nếu gọi lúc mesh đang chạy. Đây là lý do cả
2 API đều bắt buộc dùng bản async (`start`/`poll`) thay vì blocking.

## `Client` không có virtual destructor

ESP32 core's `Client.h` không khai báo `virtual ~Client()`. `GsmLink` không `delete` qua
`Client*` trực tiếp — destructor `static_cast` về đúng kiểu cụ thể (`WiFiClient*` hoặc
`TinyGsmClient*`, xác định qua `_wifiMode` — cố định cho cả object nên mọi slot cùng 1 kiểu)
trước khi xoá.
