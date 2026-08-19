# MQTT protocol

## Định danh thiết bị

Thiết bị hiện dùng:

```text
deviceId: pump-controller-01
topic gốc: pump/pump-controller-01
```

Firmware kết nối `mqtt.agribeacon.tech:8883` bằng MQTT over TLS. Web app dùng
MQTT over secure WebSocket tại `wss://mqtt.agribeacon.tech/mqtt` và dùng chung
topic/payload trong tài liệu này.

## Trạng thái kết nối

```text
Topic: pump/{deviceId}/status
Payload: online | offline
Retain: true
```

Firmware publish `online` sau khi subscribe thành công. MQTT Last Will publish
`offline` nếu thiết bị mất kết nối bất thường.

## Điều khiển relay trực tiếp

```text
Topic: pump/{deviceId}/relay/set
Payload: {"channel":1,"state":true}
```

- `channel`: số nguyên từ 1 đến 4.
- `state`: `true` để bật, `false` để tắt.
- Lệnh trực tiếp hủy timeout đang hoạt động của đúng relay, nhưng không xóa
  schedule.

Yêu cầu snapshot:

```text
Topic: pump/{deviceId}/relay/get
Payload: {}
```

Snapshot retained:

```text
Topic: pump/{deviceId}/relay/state
Payload: {"relays":[true,false,false,true]}
Retain: true
```

App phải dùng snapshot này làm trạng thái chính thức, không tự xác nhận trạng
thái chỉ từ thao tác nhấn nút.

## Timeout theo relay

Tạo hoặc thay thế timeout:

```text
Topic: pump/{deviceId}/relay/timeout/set
Payload: {"channel":1,"state":true,"durationSec":1800,"onExpire":false}
```

- `durationSec`: từ 1 giây đến 30 ngày.
- Relay chuyển sang `state` ngay khi lệnh thành công.
- Firmware lưu epoch hết hạn vào NVS.
- Khi hết hạn, relay chuyển sang `onExpire` và timeout được xóa khỏi NVS.

Hủy timeout:

```json
{"channel":1,"cancel":true}
```

Yêu cầu và nhận snapshot:

```text
Request: pump/{deviceId}/relay/timeout/get
Payload: {}
State:   pump/{deviceId}/relay/timeout/state
Retain:  true
```

Timeout đang hoạt động:

```json
{"timeouts":[{"channel":1,"active":true,"state":true,"onExpire":false,"expiresAt":1787063400}]}
```

Relay không có timeout chỉ trả `channel` và `active:false`.

## One-shot schedule theo relay

Mỗi relay có tối đa 8 event. Event chạy một lần tại thời điểm tuyệt đối và
được lưu trong NVS.

```text
Topic command: pump/{deviceId}/relay/schedule/set
```

### Tạo hoặc cập nhật event

```json
{
  "channel":1,
  "action":"upsert",
  "event":{
    "id":1,
    "day":"2026-08-20",
    "dueDate":"2026-08-20T18:05:00+07:00",
    "state":true
  }
}
```

- `channel`: từ 1 đến 4.
- `id`: từ 1 đến 255 và duy nhất trong cùng relay.
- `day`: đúng định dạng `YYYY-MM-DD`.
- `dueDate`: đúng định dạng `YYYY-MM-DDTHH:MM:SS+07:00`.
- Ngày trong `day` phải trùng với phần ngày của `dueDate`.
- `state`: trạng thái relay cần áp dụng khi event chạy.
- Upsert ID đã tồn tại sẽ thay event cũ và đặt status về `pending`.

### Xóa event

```json
{"channel":1,"action":"delete","eventId":1}
```

### Bật hoặc tắt schedule

```json
{"channel":1,"action":"set_enabled","enabled":true}
```

Tắt schedule không xóa event và không thay đổi trạng thái relay hiện tại.

### Đọc schedule

```text
Request: pump/{deviceId}/relay/schedule/get
Payload: {}
State:   pump/{deviceId}/relay/schedule/state
Retain:  true
```

Ví dụ state:

```json
{
  "schedules":[
    {
      "channel":1,
      "enabled":true,
      "events":[
        {
          "id":1,
          "day":"2026-08-20",
          "dueDate":"2026-08-20T18:05:00+07:00",
          "state":true,
          "status":"pending"
        }
      ]
    }
  ]
}
```

Giá trị `status`:

- `pending`: event chưa chạy hoặc chưa đến hạn.
- `blocked_by_timeout`: event đã đến hạn nhưng timeout của relay đang active.
- `executed`: event đã chạy và không được chạy lại sau reboot.

`blocked_by_timeout` là trạng thái suy ra khi publish, không được lưu trong NVS.
NVS chỉ lưu `pending` hoặc `executed`.

## Quy tắc ưu tiên

1. Timeout đang active có ưu tiên cao hơn schedule của cùng relay.
2. Schedule đến hạn không hủy timeout và tiếp tục giữ `pending`.
3. Timeout hoàn tất và được xóa thành công thì event quá hạn chạy trong vòng xử
   lý tiếp theo rồi chuyển thành `executed`.
4. Nếu thao tác relay hoặc lưu NVS của timeout thất bại, timeout vẫn active và
   schedule tiếp tục bị chặn.
5. Lệnh ON/OFF trực tiếp hủy timeout của đúng relay nhưng giữ schedule.
6. Timeout mới thay thế timeout cũ của đúng relay.
7. Schedule chỉ chạy khi RTC/NTP cung cấp thời gian hợp lệ.

## ACK và error

Mỗi command gửi vào `relay/set`, `relay/timeout/set` hoặc `relay/schedule/set`
nhận một ACK:

```text
Topic: pump/{deviceId}/relay/command/ack
Retain: false
```

Thành công:

```json
{"channel":1,"ok":true,"result":"ok"}
```

Lỗi:

```json
{"channel":1,"ok":false,"result":"storage_error"}
```

Các giá trị `result`:

- `ok`
- `invalid_channel`
- `invalid_argument`
- `time_unavailable`
- `storage_error`
- `relay_error`
- `not_started`

Nếu payload không thể decode, ACK trả `channel:0`, `ok:false` và
`result:"invalid_argument"`.

## Hành vi khi khởi động

- Firmware khởi tạo cả 4 relay ở trạng thái OFF.
- Timeout và schedule được đọc lại từ NVS.
- Event có status `executed` không chạy lại.
- Event `pending` đã quá hạn chạy khi thời gian hợp lệ và không có timeout active
  trên cùng relay.
