# MQTT protocol

## Device identity

Mỗi ESP32 có một `deviceId`. Giá trị mặc định dự kiến cho thiết bị đầu tiên:

```text
pump-controller-01
```

Topic gốc:

```text
pump/{deviceId}
```

## Command topics

### Điều khiển một relay

```text
Topic: pump/{deviceId}/relay/set
Payload: {"channel":1,"state":true}
```

- `channel`: số nguyên từ 1 đến 4.
- `state`: boolean; `true` là bật và `false` là tắt.
- Payload thiếu trường, sai kiểu hoặc sai phạm vi không được thực thi.

### Yêu cầu trạng thái

```text
Topic: pump/{deviceId}/relay/get
Payload: {}
```

## State topics

### Trạng thái bốn relay

```text
Topic: pump/{deviceId}/relay/state
Payload: {"relays":[true,false,false,true]}
Retain: true
```

Firmware chỉ publish snapshot mới sau khi thao tác ghi PCF8575 thành công. App dùng
snapshot này làm nguồn trạng thái chính thức, không tự xác nhận từ thao tác nhấn nút.

### Trạng thái kết nối

```text
Topic: pump/{deviceId}/status
Payload: online | offline
Retain: true
```

Khi kết nối MQTT, firmware publish `online`. MQTT Last Will publish `offline` nếu
ESP32 mất kết nối bất thường.

## Startup behavior

Firmware tắt cả bốn relay trước khi bắt đầu kết nối mạng. Phiên bản đầu không tự
khôi phục trạng thái relay sau khi mất điện.

## Web app transport

Web app kết nối cùng broker bằng MQTT over secure WebSocket:

```text
wss://mqtt.agribeacon.tech/mqtt
```

Firmware dùng MQTT over TLS tại `mqtt.agribeacon.tech:8883`. Hai transport dùng
chung topic và payload được định nghĩa trong tài liệu này.

## Timeout theo relay

Tạo hoặc thay thế timeout của một relay:

```text
Topic: pump/{deviceId}/relay/timeout/set
Payload: {"channel":1,"state":true,"durationSec":1800,"onExpire":false}
```

ESP lưu thời điểm hết hạn tuyệt đối vào NVS, chuyển relay sang `state` ngay và
chuyển sang `onExpire` khi hết hạn. `durationSec` hợp lệ từ 1 giây đến 30 ngày.

Hủy timeout:

```json
{"channel":1,"cancel":true}
```

Đọc và nhận snapshot retained:

```text
Request: pump/{deviceId}/relay/timeout/get, payload {}
State:   pump/{deviceId}/relay/timeout/state
```

```json
{"timeouts":[{"channel":1,"active":true,"state":true,"onExpire":false,"expiresAt":1787063400}]}
```

## Schedule theo relay

Ghi toàn bộ lịch của một relay (tối đa 8 event):

```text
Topic: pump/{deviceId}/relay/schedule/set
```

```json
{
  "channel":2,
  "enabled":true,
  "events":[
    {"id":1,"days":[1,2,3,4,5,6,7],"time":"06:00","state":true},
    {"id":2,"days":[1,2,3,4,5,6,7],"time":"06:30","state":false}
  ]
}
```

`days` dùng `1=Thứ hai ... 7=Chủ nhật`. Tắt lịch nhưng giữ trạng thái relay:

```json
{"channel":2,"enabled":false}
```

Đọc và nhận snapshot retained:

```text
Request: pump/{deviceId}/relay/schedule/get, payload {}
State:   pump/{deviceId}/relay/schedule/state
```

## Quy tắc ưu tiên (cách A)

- ON/OFF trực tiếp từ app hoặc nút vật lý hủy timeout của đúng relay, nhưng giữ schedule.
- Timeout mới thay timeout cũ của đúng relay.
- Event schedule đến giờ hủy timeout của đúng relay, nhưng giữ nguyên cấu hình schedule.
- Mọi timeout và schedule được lưu NVS; schedule chỉ chạy khi RTC/NTP cung cấp thời gian hợp lệ.
