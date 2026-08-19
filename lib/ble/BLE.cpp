#include "BLE.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

// log.h là tiện ích tầng app (include/), không phải thành phần của lib này —
// fallback no-op nếu project khác không có, giữ lib tự đứng độc lập.
#if __has_include("log.h")
  #include "log.h"
#else
  #define LOG_PRINT(...)    ((void)0)
  #define LOG_PRINTLN(...)  ((void)0)
  #define LOG_PRINTF(...)   ((void)0)
  #define LOG_FLUSH()       ((void)0)
#endif

// Nordic UART Service (matches host in ota_upload.py)
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_RX_BUF       640
#define BLE_TX_BUF       640
#define BLE_RX_QUEUE_LEN 1024

#define BLE_MAX_CHANNELS 16
static BleRxHandler _handlers[BLE_MAX_CHANNELS];
static std::function<void(bool)> _connectCb;
static BleAuthVerifier _verifier;
static volatile bool _connected = false;
static volatile bool _raw_connected = false;
static volatile bool _authed = false;
static uint32_t      _auth_timeout_ms = 10000;
static uint32_t      _connect_ms = 0;
static volatile uint16_t _conn_id = 0xFFFF;

static NimBLEServer         *_server  = nullptr;
static NimBLECharacteristic *_txChar  = nullptr;
static NimBLECharacteristic *_rxChar  = nullptr;
static QueueHandle_t         _rxQueue = nullptr;
static volatile uint16_t     _mtu     = 23;
static bool                  _initialized = false;

static enum { RS_CH, RS_LL, RS_LH, RS_DATA } _rs = RS_CH;
static uint8_t  _rxCh;
static uint16_t _rxLen, _rxPos;
static uint8_t  _rxBuf[BLE_RX_BUF];

// NimBLE thay CCCD-descriptor-callback (BLE2902 kiểu Bluedroid) bằng
// onSubscribe() ngay trên characteristic — không cần tự tạo/theo dõi CCCD
// descriptor thủ công nữa, NimBLE tự quản lý.
class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
        _raw_connected = true;
        _conn_id       = info.getConnHandle();
        _connect_ms    = millis();
        if (!_verifier) {
            _authed = true;
            _connected = true;
            if (_connectCb) _connectCb(true);
        }
    }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
        bool wasAuthed = _authed;
        _raw_connected = false;
        _authed = false;
        _connected = false;
        _conn_id = 0xFFFF;
        _mtu = 23;
        if (wasAuthed && _connectCb) _connectCb(false);
        NimBLEDevice::startAdvertising();
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo &info) override {
        _mtu = mtu;
    }
};

class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
        const std::string &v = c->getValue();
        const uint8_t *d = (const uint8_t *)v.data();
        for (size_t i = 0; i < v.length(); i++) {
            xQueueSend(_rxQueue, &d[i], 0);
        }
    }
};

static void _send_auth_reply(const char *msg);

class TxCharCB : public NimBLECharacteristicCallbacks {
    // subValue: bit0=notify, bit1=indicate (0 = client vừa tắt/chưa bật).
    void onSubscribe(NimBLECharacteristic *c, NimBLEConnInfo &info, uint16_t subValue) override {
        if (subValue == 0) return;
        if (_raw_connected && !_authed && _verifier) {
            _send_auth_reply("AUTH_REQUIRED");
            LOG_PRINTLN("[BLE] AUTH_REQUIRED");
        }
    }
};

static void _ble_notify_raw(const uint8_t *data, size_t len);

static void _send_auth_reply(const char *msg) {
    size_t mlen = strlen(msg);
    uint8_t pkt[32];
    if (mlen > sizeof(pkt) - 3) mlen = sizeof(pkt) - 3;
    pkt[0] = BLE_CH_AUTH;
    pkt[1] = (uint8_t)(mlen & 0xFF);
    pkt[2] = (uint8_t)(mlen >> 8);
    memcpy(pkt + 3, msg, mlen);
    _ble_notify_raw(pkt, 3 + mlen);
}

static void _dispatch() {
    const uint8_t *data   = _rxLen ? _rxBuf : nullptr;
    const size_t   data_n = _rxLen;

    if (_rxCh == BLE_CH_AUTH) {
        bool ok = !_verifier || _verifier(data, data_n);
        if (ok) {
            bool wasAuthed = _authed;
            _authed        = true;
            _connected     = true;
            _send_auth_reply("AUTH_OK");
            if (!wasAuthed && _connectCb) _connectCb(true);
        } else {
            _send_auth_reply("AUTH_FAIL");
        }
        return;
    }

    if (!_authed) return;

    if (_rxCh < BLE_MAX_CHANNELS && _handlers[_rxCh]) _handlers[_rxCh](data, data_n);
}

static void _rx_byte(uint8_t b) {
    switch (_rs) {
    case RS_CH:
        _rxCh = b;  _rs = RS_LL;
        break;
    case RS_LL:
        _rxLen = b;  _rs = RS_LH;
        break;
    case RS_LH:
        _rxLen |= (uint16_t)b << 8;
        if (_rxLen > BLE_RX_BUF)  { _rs = RS_CH; break; }
        if (_rxLen == 0)          { _dispatch(); _rs = RS_CH; break; }
        _rxPos = 0;  _rs = RS_DATA;
        break;
    case RS_DATA:
        _rxBuf[_rxPos++] = b;
        if (_rxPos >= _rxLen) { _dispatch(); _rs = RS_CH; }
        break;
    }
}

static void _ble_notify_raw(const uint8_t *data, size_t len) {
    if (!_raw_connected || !_txChar) return;
    size_t chunk = (_mtu > 3) ? (size_t)(_mtu - 3) : 20;
    if (chunk > 244) chunk = 244;
    size_t off = 0;
    while (off < len) {
        size_t n = (len - off > chunk) ? chunk : (len - off);
        _txChar->setValue((uint8_t *)(data + off), n);
        _txChar->notify();
        off += n;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

static void _ble_notify(const uint8_t *data, size_t len) {
    if (!_connected) return;
    _ble_notify_raw(data, len);
}

void ble_set_auth_verifier(BleAuthVerifier verifier, uint32_t timeout_ms) {
    _verifier = verifier;
    _auth_timeout_ms = timeout_ms;
}

void ble_begin(const char *name) {
    if (_initialized) return;
    _initialized = true;

    if (_rxQueue == nullptr)
        _rxQueue = xQueueCreate(BLE_RX_QUEUE_LEN, sizeof(uint8_t));

    NimBLEDevice::init(name);
    NimBLEDevice::setMTU(247);

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(new ServerCB());

    NimBLEService *svc = _server->createService(BLE_SERVICE_UUID);

    _txChar = svc->createCharacteristic(
        BLE_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY);
    _txChar->setCallbacks(new TxCharCB());

    _rxChar = svc->createCharacteristic(
        BLE_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    _rxChar->setCallbacks(new RxCB());

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->enableScanResponse(true);
    // NimBLEDevice::init(name) chỉ set GAP Device Name characteristic (đọc
    // được SAU KHI đã kết nối) — KHÔNG tự thêm tên vào gói quảng bá như
    // Bluedroid mặc định làm trước đây. Không gọi setName() ở đây → máy
    // scan thấy thiết bị nhưng không có tên, không lọc/tìm được theo tên
    // "VY-..." (đã xác nhận đây là nguyên nhân "không tìm thấy ble" sau khi
    // migrate). Gọi SAU enableScanResponse(true) để tên nằm trong gói scan
    // response (đủ chỗ hơn — gói adv chính đã có service UUID 128-bit).
    adv->setName(name);
    NimBLEDevice::startAdvertising();
}

void ble_set_connect_cb(std::function<void(bool)> cb) {
    _connectCb = cb;
}

void ble_register_channel(uint8_t ch, BleRxHandler h) {
    if (ch < BLE_MAX_CHANNELS) _handlers[ch] = h;
}

void ble_send(uint8_t ch, const uint8_t *data, size_t len) {
    static uint8_t pkt[BLE_TX_BUF + 3];
    if (len > BLE_TX_BUF) return;
    pkt[0] = ch;
    pkt[1] = (uint8_t)(len & 0xFF);
    pkt[2] = (uint8_t)(len >> 8);
    if (len > 0) memcpy(pkt + 3, data, len);
    _ble_notify(pkt, 3 + len);
}

void ble_disconnect() {
    if (_server && _conn_id != 0xFFFF) {
        _server->disconnect(_conn_id);
    }
}

void ble_stop() {
    if (_server && _conn_id != 0xFFFF) {
        _server->disconnect(_conn_id);
    }
    NimBLEDevice::getAdvertising()->stop();
}

void ble_dispatch() {
    if (!_rxQueue) return;
    uint8_t b;
    while (xQueueReceive(_rxQueue, &b, 0) == pdTRUE) {
        _rx_byte(b);
    }

    if (_raw_connected && !_authed && _verifier &&
        _auth_timeout_ms > 0 &&
        (millis() - _connect_ms) > _auth_timeout_ms &&
        _server && _conn_id != 0xFFFF) {
        _server->disconnect(_conn_id);
    }
}
