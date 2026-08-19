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

## Interval schedule theo relay

Mỗi relay có tối đa 8 schedule. Mỗi schedule là một phiên chạy hoàn chỉnh:
relay ON tại `startAt` và OFF tại `endAt`. Schedule được lưu trong NVS.

```text
Topic command: pump/{deviceId}/relay/schedule/set
```

### Tạo hoặc cập nhật schedule

```json
{
  "channel":1,
  "action":"upsert",
  "schedule":{
    "id":1,
    "startAt":"2026-08-20T18:00:00+07:00",
    "endAt":"2026-08-20T18:30:00+07:00"
  }
}
```

- `channel`: từ 1 đến 4.
- `id`: từ 1 đến 255 và duy nhất trong cùng relay.
- `startAt`, `endAt`: đúng định dạng `YYYY-MM-DDTHH:MM:SS+07:00`.
- Bắt buộc `startAt < endAt`.
- Các schedule của cùng relay không được chồng thời gian.
- Upsert ID đã tồn tại sẽ thay schedule cũ và đặt status về `pending`.
- Không được upsert một schedule đang `active`; phải delete để dừng trước.

### Xóa schedule

```json
{"channel":1,"action":"delete","scheduleId":1}
```

Nếu schedule đang `active` và không có timeout, relay được OFF trước khi schedule
bị xóa. Nếu timeout đang active, timeout tiếp tục sở hữu trạng thái relay.

### Bật hoặc tắt schedule

```json
{"channel":1,"action":"set_enabled","enabled":true}
```

Tắt schedule không xóa dữ liệu. Schedule `pending` không được bắt đầu, nhưng
schedule đang `active` tiếp tục chạy đến `endAt` rồi OFF.

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
      "entries":[
        {
          "id":1,
          "startAt":"2026-08-20T18:00:00+07:00",
          "endAt":"2026-08-20T18:30:00+07:00",
          "status":"pending"
        }
      ]
    }
  ]
}
```

Giá trị `status`:

- `pending`: schedule chưa đến `startAt`.
- `active`: relay đã ON và chưa đến `endAt`.
- `blocked_by_timeout`: đã đến khoảng chạy nhưng timeout đang sở hữu relay.
- `completed`: relay đã OFF khi kết thúc phiên.
- `missed`: toàn bộ khoảng chạy đã qua mà schedule chưa thể bắt đầu.

`blocked_by_timeout` là trạng thái suy ra khi publish, không được lưu trong NVS.
NVS lưu `pending`, `active`, `completed` hoặc `missed`.

## Quy tắc ưu tiên

1. Timeout đang active có ưu tiên cao hơn schedule của cùng relay.
2. Timeout active tại `startAt`: schedule giữ `pending` và không bật relay.
3. Timeout kết thúc trước `endAt`: schedule bật relay và chuyển `active`.
4. Timeout kéo qua `endAt`: schedule chưa bắt đầu chuyển thành `missed`.
5. Timeout xuất hiện khi schedule đang `active`: timeout giành quyền relay.
6. Timeout kết thúc trước `endAt`: schedule đang active khôi phục relay ON.
7. Timeout kết thúc sau `endAt`: relay OFF và schedule chuyển `completed`.
8. Nếu thao tác relay hoặc lưu NVS của timeout thất bại, timeout vẫn active và
   schedule tiếp tục bị chặn.
9. Lệnh ON/OFF trực tiếp hủy timeout của đúng relay nhưng giữ schedule; trạng
   thái manual không bị schedule active ghi đè ngay.
10. Timeout mới thay thế timeout cũ của đúng relay.
11. Schedule chỉ chạy khi RTC/NTP cung cấp thời gian hợp lệ.

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
- Schedule `pending` trong khoảng `startAt`–`endAt` bắt đầu khi thời gian hợp lệ
  và không có timeout active.
- Schedule `active` được khôi phục ON nếu vẫn chưa đến `endAt` và không có
  timeout active.
- Schedule `pending` đã qua `endAt` chuyển thành `missed`.
- Schedule `active` đã qua `endAt` được OFF và chuyển thành `completed` sau khi
  timeout (nếu có) kết thúc.
