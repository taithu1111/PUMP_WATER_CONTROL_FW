# BLE provisioning protocol

Thiết bị chỉ bật BLE provisioning khi NVS chưa có Wi-Fi credentials hợp lệ.
Toàn bộ message provisioning dùng channel `BLE_CH_PROVISION` (`0x03`) và payload
JSON UTF-8 không nén.

BLE transport đóng gói mỗi payload theo framing của `lib/ble`:

```text
[CH:1][LEN_L:1][LEN_H:1][JSON:LEN]
```

Payload JSON không được vượt quá giới hạn 640 byte của BLE transport.

## Scan Wi-Fi

App yêu cầu quét:

```json
{"type":"scan"}
```

Thiết bị phản hồi tối đa 5 access point mạnh nhất:

```json
{
  "type":"scan_result",
  "aps":[
    {"ssid":"real_casa_2.4g","rssi":-54,"channel":6}
  ]
}
```

Mỗi access point gồm:

- `ssid`: tối đa 32 byte.
- `rssi`: cường độ tín hiệu dBm.
- `channel`: kênh Wi-Fi.

## Gửi Wi-Fi credentials

```json
{
  "type":"wifi",
  "ssid":"real_casa_2.4g",
  "password":"666688889"
}
```

Ràng buộc:

- `ssid` phải là chuỗi từ 1 đến 32 byte.
- `password` phải là chuỗi tối đa 63 byte; chuỗi rỗng cho mạng mở.
- Credentials chỉ được lưu NVS sau khi test Wi-Fi và MQTT đều thành công.

## Trạng thái activation

Thiết bị gửi trạng thái theo thứ tự:

```json
{"type":"status","state":"testing_wifi"}
{"type":"status","state":"testing_mqtt"}
{"type":"status","state":"active"}
```

Sau `active`, thiết bị reboot. Lần boot kế tiếp có credentials nên không bật BLE.

## Lỗi

Payload lỗi có định dạng:

```json
{"type":"error","code":"wifi_failed"}
```

Các mã lỗi:

| Code | Ý nghĩa |
|---|---|
| `busy` | Đang scan hoặc test một cấu hình khác |
| `invalid_payload` | JSON hoặc field không hợp lệ |
| `wifi_failed` | Không kết nối được Wi-Fi trong timeout |
| `mqtt_failed` | Wi-Fi thành công nhưng MQTT không kết nối được |
| `scan_failed` | Quét Wi-Fi thất bại |

Sau lỗi, BLE tiếp tục hoạt động để app thử lại. Thiết bị không lưu credentials lỗi.

## Authentication

Provisioning chỉ tồn tại khi thiết bị chưa active nên không đăng ký auth verifier.
BLE library tự cho phép session đầu vào khi verifier là `nullptr`. Sau khi active,
thiết bị không khởi tạo BLE thay vì tiếp tục quảng bá và yêu cầu password.
