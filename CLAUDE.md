# Kiến trúc khởi đầu cho project firmware (PlatformIO/Arduino/FreeRTOS)

Đọc file này khi **tạo project mới** hoặc **tái cấu trúc project cũ**. Đây là checklist tổ chức
thư mục + rule kỹ thuật cho firmware C++ chạy FreeRTOS trên MCU (ESP32 và tương tự) — đúc kết
lại để dùng làm điểm khởi đầu, không phải tài liệu mô tả 1 sản phẩm cụ thể. Khi bootstrap 1
project mới, đi từ mục 1 xuống, quyết định từng phần có áp dụng hay không dựa trên nhu cầu thật
của project đó — không áp toàn bộ 1 cách máy móc.

---

## 1. Cấu trúc thư mục mặc định (1 firmware)

Baseline cho hầu hết project — **chỉ 1 firmware, 1 board**:

```
project/
├── platformio.ini
├── include/             # config dùng chung: pin map, keys, feature flags
├── src/
│   ├── main.cpp
│   ├── core/            # hạ tầng cross-cutting — xem mục 3
│   └── feature/<name>/  # từng khối nghiệp vụ độc lập — xem mục 4
└── lib/                 # thư viện tách rời khỏi ứng dụng — xem mục 2
```

4 thành phần `lib/` / `core/` / `feature/` / `main.cpp` trả lời 4 câu hỏi khác nhau: *cái này
tách khỏi project được không?* → `lib/`. *Cái này hạ tầng dùng chung trong toàn app không?* →
`core/`. *Cái này là 1 khối nghiệp vụ độc lập không?* → `feature/`. *Ai nối các khối lại với
nhau?* → `main.cpp`. Chi tiết từng phần ở mục 2–5.

### `include/log.h` — tạo sẵn nếu chưa có

Khi bootstrap project mới hoặc tái cấu trúc project cũ theo file này, luôn kiểm tra
`include/log.h` đã tồn tại chưa — nếu chưa, tạo file này với đúng nội dung sau (không tự đổi tên
macro/logic, đây là tiện ích dùng chung xuyên suốt toàn bộ hướng dẫn — `lib/` cần nó qua
`#if __has_include("log.h")` ở mục 2, `feature/`/`core/`/`main.cpp` include thẳng):

```cpp
#pragma once

// Log macros — chỉ compile ra code trong DEBUG build.
// Release build → không sinh code (Serial.print* biến mất hoàn toàn),
// tiết kiệm flash + tránh lộ log ở production.
//
// Cách dùng:
//   LOG_PRINTLN("hello");
//   LOG_PRINTF("val=%d\n", n);
//   LOG_FLUSH();
//
// Serial.begin(...) trong setup vẫn cần giữ (không dùng macro) — Serial
// vẫn có thể dùng cho input (Serial.available/read) ở loop().

#ifdef DEBUG
  #include <Arduino.h>
  #define LOG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define LOG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define LOG_PRINTF(...)   Serial.printf(__VA_ARGS__)
  #define LOG_FLUSH()       Serial.flush()
#else
  #define LOG_PRINT(...)    ((void)0)
  #define LOG_PRINTLN(...)  ((void)0)
  #define LOG_PRINTF(...)   ((void)0)
  #define LOG_FLUSH()       ((void)0)
#endif
```

`#ifdef DEBUG` (không phải `#if DEBUG`) chỉ đúng khi build system định nghĩa `DEBUG` bằng
`-D DEBUG` (không kèm giá trị, hoặc kèm giá trị khác 0) ở profile debug và **hoàn toàn không
define** `DEBUG` ở profile release — không dùng kiểu `-D DEBUG=0` cho release vì `#ifdef` không
phân biệt được với `DEBUG=1`.

### Khi nào cần nhiều firmware cho cùng 1 board (không phải mặc định)

Một số project cần build ra **nhiều firmware khác nhau** từ cùng 1 codebase (vd 2 loại thiết bị
trong cùng sản phẩm, chạy 2 vai trò khác nhau nhưng share phần lớn logic nền). Khi đó cấu trúc
trên nhân đôi thành `src_<vaiTroA>/`, `src_<vaiTroB>/` (mỗi cái có `main.cpp` + `core/` +
`feature/` riêng), chọn source theo build filter mỗi environment
(`build_src_filter = -<*> +<src_vaiTroA/>`), còn `lib/` vẫn dùng chung cho tất cả.

> **Rule bắt buộc cho AI agent đọc file này:** cấu trúc nhiều firmware/vai trò (`src_<role>/`)
> **KHÔNG phải mặc định**. Trước khi đề xuất hoặc thực hiện tách `src/` thành `src_<role>/`
> nhiều bản cho 1 project — dù project trông có vẻ "có thể cần nhiều vai trò" — **luôn dừng lại
> hỏi user xác nhận trước** có đúng đang cần build ≥2 firmware image từ cùng codebase hay không.
> Không tự suy ra từ tên gọi, domain, hay so sánh với project khác từng gặp. Mặc định giữ 1
> `src/` duy nhất cho đến khi user xác nhận rõ ràng.

---

## 2. `lib/` — thư viện tách rời, không biết gì về ứng dụng

**Là gì:** code triển khai 1 khả năng/giao thức/thuật toán tự thân. Nó không biết nghiệp vụ cụ
thể của project là gì — chỉ nhận input qua tham số/config, trả output qua API. Về lý thuyết, đem
`lib/xxx` bỏ sang project khác vẫn compile và chạy đúng nếu cấu hình tương thích.

**Điều kiện bắt buộc để 1 đoạn code đủ tư cách vào `lib/`:**
- **Không phụ thuộc bất kỳ thành phần nào bên ngoài chính nó** — phép thử bắt buộc: đem riêng
  thư mục `lib/xxx` đó bỏ sang 1 project khác (kể cả project hoàn toàn mới, không liên quan gì
  tới project hiện tại) thì nó vẫn phải tự compile và chạy đúng, không cần mang theo bất kỳ file
  nào khác ngoài chính `lib/xxx`. Áp dụng cho MỌI include ra ngoài `lib/xxx`, không riêng gì
  `config.h` — kể cả những header *trông có vẻ* dùng chung/generic (tiện ích log, assert, string
  helper...) nhưng thực chất nằm ở tầng app (`include/`, `src/`) của project hiện tại chứ không
  nằm trong chính `lib/xxx`; project khác chưa chắc có sẵn file đó, dù nội dung không mang
  nghiệp vụ gì. Không include ngược lên `src/` (không biết `config.h` tầng ứng dụng) là 1 trường
  hợp cụ thể của nguyên tắc này, không phải toàn bộ nguyên tắc.
- Cần 1 tiện ích hay có sẵn ở project khác (logging, assert...) nhưng không chắc luôn tồn tại
  đúng tên/API → dùng `#if __has_include(<config.h>)` (hoặc header tương ứng) với giá trị default
  an toàn định nghĩa ngay trong lib, không include thẳng rồi giả định nó luôn có mặt.
- Có ranh giới API tường minh: header public khai báo đủ những gì consumer cần; phần triển khai
  private không leak ra ngoài.
- Không mang state gắn với 1 phiên chạy nghiệp vụ cụ thể (state nội bộ — buffer, connection —
  thì được, nhưng không phải "trạng thái tiến trình nghiệp vụ" như lịch trình hay hàng đợi lệnh
  của app).

**Checklist quyết định đưa 1 đoạn code vào `lib/` (đủ đa số điều kiện mới nên tách):**

1. **Có ≥2 consumer độc lập thật sự cần nó không** (2 phần khác nhau của app, 2 firmware nếu có
   nhiều firmware, hoặc 1 app + 1 tool/test riêng)? Nếu hiện tại chỉ 1 nơi dùng → để trong
   `feature/` hoặc `core/` trước; chỉ tách ra `lib/` khi consumer thứ 2 **thật sự xuất hiện**,
   không tách trước "phòng khi cần".
2. **Có phụ thuộc business logic cụ thể của project không** (tên topic riêng, format lệnh
   riêng, hằng số nghiệp vụ)? Nếu có → đây là `feature`, không phải `lib`.
3. **Test được độc lập** (không cần network/modem/hardware thật) không? Nếu phần lõi cần
   hardware mới chạy được, tách riêng: logic thuần (parse/encode/state machine) vào `lib/`, phần
   điều khiển hardware ở lại `feature/`.
4. Đưa vào `lib/` **có buộc lib khác phải include nó** dù chỉ dùng 1 hàm nhỏ không? Nếu 1 hàm
   trong lib A chỉ được dùng bởi lib B (và toàn bộ consumer của hàm đó vốn đã transitively phụ
   thuộc B qua đường khác) → hàm đó nên **sống trong B**, không kéo cả A về làm dependency mới.
   Grep toàn repo để chốt chính xác tập consumer hiện tại trước khi di chuyển.

**Dependency giữa lib với lib: KHÔNG cho phép.** Mỗi lib trong `lib/` phải tự đứng độc lập, không
include lib khác trong cùng thư mục `lib/`. Nếu code trong lib A cần dùng 1 hàm/khả năng đang nằm
ở lib B, không giữ include A→B — **di chuyển hẳn phần code đó (hàm, struct, macro liên quan) từ B
sang A** để A tự chứa, không phụ thuộc B nữa. Nếu hàm đó còn được dùng ở nơi khác ngoài A (nhiều
consumer thật), xem lại theo checklist mục 1–4 xem có nên ở nguyên chỗ cũ, dời hẳn, hay tách thành
1 lib thứ 3 độc lập — nhưng không bao giờ chấp nhận A giữ include sang B chỉ vì "tiện".

**Trường hợp cụ thể — nhiều firmware (mục 1):** nếu 1 feature được **cả 2 (hoặc nhiều) firmware
dùng lại y hệt nhau**, và feature đó **không phụ thuộc `core/` của bất kỳ role nào** (không
include ngược `core/` của node hay root) → đủ điều kiện đưa thẳng vào `lib/`, mỗi role chỉ cần
include lại. Nếu vẫn còn phụ thuộc `core/` của 1 role cụ thể (device identity, filesystem mount
riêng của role đó...) thì chưa đủ điều kiện — giữ nguyên bản song sinh riêng theo mục 10 cho tới
khi tách được phần phụ thuộc core đó ra (hoặc route qua tham số/callback do caller truyền vào).

- **Trước khi promote, xác nhận lại 2 bản song sinh CÒN thực sự giống hệt nhau** — không chỉ tin
  vào lần review trước đó. 2 bản có thể đã phân kỳ hành vi từ lúc đó (vd 1 bên được thêm nhánh
  `#ifdef DEBUG` mà bên kia cố tình không có) — nếu đã phân kỳ thật sự, chưa đủ điều kiện promote
  dù cấu trúc còn giống nhau phần lớn; giữ nguyên song sinh.
- **Kỹ thuật tách dependency khỏi `core/` khi accessor đơn giản** (get-resource / lock / unlock,
  không phải business logic): thêm 1 hàm `_begin(getResourceFn, lockFn, unlockFn)` trong lib để
  lib lưu lại các function pointer này, dùng nội bộ thay cho gọi thẳng hàm `core/`. Mỗi role giữ
  lại file feature cũ làm **wrapper mỏng**: gọi `_begin()` (lazy, 1 lần) rồi forward nguyên văn
  sang API của lib — giữ tên hàm/type cũ để không phải sửa `main.cpp`.

**KHÔNG đưa vào `lib/`:** logic chỉ 1 nơi dùng; driver gắn chặt 1 loại hardware cụ thể của board
hiện tại; code chỉ tồn tại để "nối" các module khác lại (đó là việc của `main.cpp`).

**Dọn dẹp khi tách/gộp:** 1 file `.cpp` không còn nội dung sau khi di chuyển hàm đi nơi khác thì
xoá hẳn, không giữ lại rỗng.

---

## 3. `core/` — hạ tầng cross-cutting

**Là gì:** dịch vụ nền mà **nhiều feature** cần dùng chung, nhưng bản thân nó gắn với đặc thù
của app (không đủ tổng quát để lên `lib/`, hoặc cố ý không tách vì chỉ app này cần) — không thoả
điều kiện "consumer độc lập bên ngoài project" của mục 2.

**Ví dụ loại nội dung thuộc `core/`:** định danh thiết bị (đọc từ đâu, cache ra sao); quản lý
thời gian hệ thống (đồng bộ nguồn thời gian, cung cấp "now" dùng chung); mount point + lock của
filesystem; init phần cứng dùng chung nhiều feature (bus I2C/SPI chia sẻ).

**Tiêu chí xác nhận 1 thứ thuộc `core/` chứ không phải `feature/`:**

1. **Có ≥2 feature cần dùng nó không?** Chỉ 1 feature dùng → thuộc về feature đó, không phải
   core.
2. **Nó có phải "nguồn sự thật" (source of truth) mà các feature khác phải đồng bộ theo không**
   (giờ hệ thống, identity, trạng thái filesystem)? Đây là dấu hiệu mạnh cho `core/`.
3. **Nó có mang tính "khởi tạo 1 lần, đọc nhiều lần" không** (`_begin()` chạy sớm trong
   `setup()`, sau đó các feature chỉ gọi hàm đọc/ghi qua API ổn định)?

**`core/` KHÔNG nên chứa:** logic theo lịch/sự kiện nghiệp vụ cụ thể (đó là feature); state
machine phức tạp của 1 luồng nghiệp vụ; bất cứ thứ gì chỉ 1 feature quan tâm dù nó "nghe có vẻ
hạ tầng".

---

## 4. `feature/<name>/` — 1 khối nghiệp vụ độc lập

**Là gì:** 1 khối chức năng có thể thêm/bớt mà không kéo đổ chức năng khác. Test ranh giới: *nếu
xoá thư mục feature này khỏi build (và xoá lời gọi nó trong `main.cpp`), phần còn lại của
firmware vẫn compile và chạy đúng — có thể mất 1 chức năng, nhưng không mất tính đúng đắn của
các feature khác.*

**Giới hạn (feature KHÔNG được làm):**

- **Không include trực tiếp header của feature khác cùng cấp.** Giao tiếp feature-to-feature đi
  qua callback (`std::function`) đăng ký ở `main.cpp`, hoặc qua `core/` nếu là hạ tầng dùng
  chung thật sự. Dependency ngang giữa các feature khiến xoá 1 cái kéo theo phải sửa nhiều cái
  khác.
- **Không tự ý gọi hàm cụ thể của feature khác dựa trên biết trước tên hàm.** Expose qua
  callback interface được wiring ở `main.cpp` — 2 feature không biết về sự tồn tại của nhau, chỉ
  `main.cpp` biết cả 2 và nối chúng.
- **Không giữ state mà feature khác cần đọc trực tiếp.** Nếu cần chia sẻ state, nó thuộc về
  `core/`, hoặc đi qua callback/return value — không expose biến toàn cục xuyên feature.

**Kích thước hợp lý:** đủ nhỏ để đọc hết 1 file `.cpp` trong 1 lần review (mục 12). Nếu 1
feature phình to nhiều mối quan tâm khác nhau (vừa lo giao thức, vừa lo retry, vừa lo
persistence phức tạp) → tách thành nhiều file **trong cùng** `feature/<name>/` (không nhất thiết
nâng thành feature riêng — chỉ tách khi thực sự có ranh giới trách nhiệm rõ).

**API tối thiểu của 1 feature:** `_begin(cb...)` load state + đăng ký callback; `_loop()` poll
rẻ rồi mới làm việc nặng; setter cập nhật cả persistent storage lẫn RAM trong cùng 1 lời gọi
(không để 2 nguồn lệch nhau).

---

## 5. `main.cpp` — nơi lắp ráp (wiring), không chứa logic

**Vai trò, đúng thứ tự:**

1. **Init `core/` trước tiên**, theo đúng thứ tự phụ thuộc thật (vd filesystem phải mount trước
   khi bất kỳ feature nào load state từ file).
2. **Gọi `_begin()` của từng feature**, và — quan trọng nhất — **đăng ký callback nối feature
   này với feature khác tại đây**. `main.cpp` là nơi DUY NHẤT được phép biết cả 2 feature tồn
   tại và nối chúng lại; đây là điểm khác biệt giữa "wiring" và "logic".
3. **Trong `loop()`**: gọi `_loop()` của từng feature cần polling định kỳ, theo đúng thứ tự ưu
   tiên nếu có phụ thuộc ngầm; xử lý flag toàn cục nhẹ (đèn trạng thái, nút bấm) trực tiếp tại
   đây nếu đủ đơn giản.

**KHÔNG chứa trong `main.cpp`:** phân tích dữ liệu, state machine nghiệp vụ, xử lý lỗi chi tiết
theo domain — những cái đó nằm trong feature tương ứng; `main.cpp` chỉ gọi và log kết quả ở mức
tổng quát, không quyết định phải làm gì tiếp theo dựa trên chi tiết lỗi.

**Dấu hiệu `main.cpp` đang phình sai vai trò:** 1 callback lambda truyền vào `_begin()` dài hơn
vài chục dòng, hoặc có nhánh `if`/state phức tạp bên trong thân lambda → rút logic đó ra 1 hàm
thuộc feature tương ứng — `main.cpp` chỉ giữ lại lời gọi + 1-2 dòng chuyển tiếp.

---

## 6. Task (FreeRTOS task riêng) — khi nào thực sự cần

**Mặc định: KHÔNG cần task riêng.** Đa số feature chỉ cần 1 hàm `_loop()` "im lặng" (so sánh
tick/millis, return sớm nếu chưa tới hạn) gọi từ main loop task có sẵn — mỗi task mới tốn tối
thiểu vài KB stack và thêm 1 điểm cần đồng bộ hoá dữ liệu. Task là ngoại lệ cần lý do rõ, không
phải mỗi feature mặc định có 1 task riêng.

**Trước khi quyết định, TÍNH TOÁN/ước tính theo các khía cạnh sau, giả định volume/tần suất lớn
nhất có thể gặp — phép tính này là CĂN CỨ ĐỦ để quyết định ngay, không phải bước tạm chờ đo thật
xác nhận lại:**

- **Timing:** chu kỳ cần chạy? deadline cụ thể? trễ tối đa chấp nhận được? Tính execution time
  worst-case từ logic thực tế của code (số vòng lặp lớn nhất, kích thước dữ liệu lớn nhất có thể
  xử lý) — dùng ngay làm căn cứ.
- **Blocking:** có `delay()`, chờ I/O/network, mutex/semaphore có thể block, hay gọi API không
  kiểm soát được thời gian hoàn thành không? **Chỉ riêng có blocking không kiểm soát đã là lý do
  đủ mạnh để cân nhắc task riêng** — tính worst-case blocking time từ giới hạn/timeout đã có
  trong code (timeout mạng, kích thước dữ liệu tối đa cho phép...).
- **CPU:** `CPU load ≈ execution_time × frequency` (vd 2ms × 100 lần/s ≈ 20%). Có burst workload
  hay xử lý nặng (parse protocol, crypto, decode...) không?
- **Memory:** cần bao nhiêu RAM/stack? Có cấp phát động/buffer lớn/nguy cơ fragmentation không?
  Nếu tách task, tính stack cần bằng chi phí mỗi frame gọi hàm (theo kiến trúc CPU đang dùng) ×
  độ sâu lồng gọi lớn nhất, cộng biên an toàn rộng rãi có chủ đích.
- **I/O:** loại gì (WiFi/MQTT/HTTP/SPI/I2C/UART/CAN/SD...)? Driven bởi callback/interrupt không?
  Thiết bị đối tác có thể chậm/timeout không?
- **Concurrency:** có chạy song song / chia sẻ dữ liệu hoặc peripheral với feature khác không?
  Cần mutex, hay giao tiếp qua queue/event né được lock hẳn?
- **Fault isolation:** feature này timeout/lỗi có kéo feature khác chết theo không? Retry có nguy
  cơ làm quá tải CPU/network không?

**Dự đoán ở volume lớn — không dừng lại ở số đo lúc test nhẹ tay.** Với MỌI khía cạnh trên áp dụng
`mức tiêu thụ ≈ tần suất chạy × volume/kích thước lớn nhất có thể gặp` (không phải trường hợp test
tay thông thường — 1 lần gọi, vài byte, vài giây). Tần suất chạy của feature (gọi bao nhiêu
lần/giây, nhận bao nhiêu message/giây...) phải được xác định rõ trước, rồi tự giả định kịch bản
volume lớn thật sự có thể xảy ra trong vận hành thật (nhiều client cùng lúc, burst traffic, chạy
liên tục nhiều giờ/ngày, payload lớn nhất giao thức cho phép) — tính CPU/RAM/buffer/băng
thông/storage cần ở kịch bản đó có đủ không, TRƯỚC khi chọn mô hình ở bảng dưới. Không lấy hành vi
quan sát được lúc code/test tay (luôn nhẹ, luôn ít) làm căn cứ kết luận "ổn".

**Kết quả không nhị phân "task hay không" — chọn đúng mô hình theo dữ liệu đo được:**
- **Mainloop** (`_loop()` hiện có): execution ngắn, không block, timing không nghiêm ngặt.
- **Task riêng**: blocking không kiểm soát được, workload lớn, hoặc cần cách ly timing/fault.
- **Callback/ISR + queue**: sự kiện bất đồng bộ cần phản hồi nhanh, xử lý thật ở task nền (mục 7).
- **Task dùng chung**: nhiều feature có timing/workload tương tự, không cần cách ly riêng.
- **Event-driven** (task notification/EventGroup): không cần polling liên tục.
- **Cooperative yield qua callback hook có sẵn**: không tạo task, chỉ nhường CPU định kỳ tại các
  điểm blocking đã có sẵn tham số yield/callback trong hàm đang gọi. Chỉ áp dụng được khi hàm
  blocking đó ĐÃ thiết kế sẵn hook này — không phải kỹ thuật tự chế thêm được ở mọi chỗ. 3 điều
  kiện bắt buộc, thiếu 1 là chưa đủ an toàn để chọn mô hình này: (1) liệt kê ĐỦ mọi consumer cần
  phản hồi trong lúc block — loại bất kỳ consumer nào ra khỏi danh sách phải có số đo cụ thể biện
  minh (worst-case block time so với deadline của consumer đó), không phải nhận xét định tính
  "chấp nhận được"; (2) tự ước tính độ sâu stack tăng thêm (chi phí mỗi frame × số frame lồng
  thêm, cộng biên an toàn) — dùng ngay làm căn cứ kết luận đủ hay không đủ; (3) chấp nhận KHÔNG có
  fault isolation như task thật: hàm blocking kẹt ở ngoài điểm gọi hook thì mọi thứ được bơm qua
  hook đó cũng ngừng theo, không có gì cách ly.

**Priority & core pinning:** task nền xử lý việc phụ nên có priority **thấp hơn** main loop task
trừ khi có lý do thời gian thực rõ ràng; pin core cụ thể nếu cần tách khỏi core đang chạy network
stack/ISR để giảm contention.

**"Chấp nhận được" phải đánh giá đúng khía cạnh, đúng ngưỡng, và đánh giá lại mỗi khi có feature
mới — không phải nhận xét định tính 1 lần rồi thôi.** 2 khía cạnh: (1) CPU/scheduler — code có
nhường CPU định kỳ, không deadlock/watchdog — CẦN nhưng KHÔNG ĐỦ; (2) khả năng phản hồi thực tế
của MỌI feature khác đang chạy chung main loop suốt thời gian block — đây mới là điều kiện quyết
định. 1 block khoẻ mạnh ở CPU vẫn có thể khiến toàn bộ phần còn lại của firmware (nhận sự kiện
bus, xử lý lệnh, hiển thị...) ngừng phản hồi — về chức năng đây LÀ treo dù CPU "không chết".
**Ngưỡng cụ thể:** so **thời lượng block worst-case thực tế** (không phải trường hợp hay gặp) với
**deadline/timeout ngắn nhất trong số mọi feature khác** đang chạy chung main loop (keepalive,
timeout giao thức, sức chứa buffer/queue driver...) — vượt quá dù chỉ 1 lần cũng đủ mất dữ
liệu/rớt kết nối không hồi phục được, kết luận cần task xong, không cần bàn thêm. **Đánh giá
không cố định 1 lần** — làm lại mỗi khi thêm feature mới cần phản hồi gần thời gian thực vào cùng
firmware; rà lại mọi block hiện có trong `_loop()`/command handler khác, không chỉ đánh giá
feature mới trong ốc đảo riêng của nó.

**Khi đã kết luận CẦN task — 1 checklist thực thi bắt buộc, không dừng ở việc gọi `xTaskCreate`.**
Tách task mới vào firmware trước đó chỉ chạy 1 task phá vỡ nhiều giả định sẵn có:
1. **Mọi lock/mutex đang no-op vì "chỉ có 1 task"** (comment kiểu "chưa cần khoá thật") phải audit
   lại, thay bằng semaphore thật nếu resource đó giờ bị truy cập từ ≥2 task (mục 7).
2. **Thao tác trước đây "tự serialize miễn phí" nhờ block** phải có cờ chặn tường minh khi chuyển
   sang non-blocking/task riêng — thiếu cờ này, 2 lần gọi chồng nhau sẽ đụng độ dữ liệu.
3. **Kênh báo kết quả/lỗi/tiến độ** về nơi cần biết phải theo đúng cơ chế mục 7 — không dùng biến
   chia sẻ trần không đồng bộ.
4. **Priority & stack size**: tự ước tính bằng chi phí đã biết (vd chi phí trung bình mỗi frame
   gọi hàm trên kiến trúc CPU đang dùng × số frame lồng thêm, cộng biên an toàn rộng rãi có chủ
   đích) rồi **quyết định giá trị ngay** — cùng nguyên tắc với "dự đoán ở volume lớn" phía trên,
   không phải ngoại lệ cần chờ gì thêm.

**Được phép chia checklist trên thành nhiều phase thay vì làm 1 lần — nhưng chia đúng theo "ai
cung cấp được thông tin còn thiếu", không phải "chỗ nào cũng dừng lại hỏi user".** Cả 4 mục trên
đều tự quyết định/làm được ngay bằng ước tính có căn cứ — không mục nào bắt buộc phải dừng lại chờ
user mới dám tiến hành. **Dù chia phase, không bao giờ ship 1 phase ở trạng thái kém an toàn hơn
lúc trước khi bắt đầu đổi** — giá trị priority/stack size ước tính phải có biên an toàn rộng rãi
có chủ đích, không để trống hay đoán liều không căn cứ; thiếu biên an toàn này, 1 ước tính ẩu có
thể gây lỗi mới (stack overflow, crash) còn nguy hiểm hơn vấn đề ban đầu đang sửa.

**Chuyển blocking → non-blocking (né task theo đúng hướng trên) không tự động an toàn** — phải
xác minh thao tác bên dưới AN TOÀN khi bị gọi lại trong lúc lần trước CHƯA XONG. Khác với "tự
serialize miễn phí" ở trên (2 REQUEST độc lập chồng nhau) — đây là 1 tiến trình vật lý nhiều bước
(connect mạng, mở phiên giao thức...) bị hàm gọi lặp lại hiểu nhầm là "chưa bắt đầu" mỗi lần gọi.
Nhiều API/driver không idempotent: gọi lại giữa chừng HUỶ/RESET tiến trình đang dở thay vì "thử
lại vô hại" → livelock dễ bị coi là đã xong vì triệu chứng "treo main loop" đã hết. Trước khi
convert 1 lệnh blocking `X(timeout)` sang gọi lặp qua `_loop()` với timeout ngắn/0: tự thêm state
đánh dấu "đang có 1 lần thực thi dở dang, đừng bắt đầu lại" + timeout hợp lý để thoát nếu kẹt thật.

**Ước tính baseline trước khi thêm feature** khi nghi ngờ ảnh hưởng timing (mainloop worst-case
latency, CPU load %, RAM/stack cần thêm) bằng đúng cách tính đã nêu ở trên — không có ước tính cụ
thể thì "chấp nhận được" chỉ là cảm tính.

---

## 7. ISR / driver-callback safety

Callback chạy trong context của driver/ISR (network stack callback, GPIO ISR, UART RX
interrupt...) có ràng buộc cứng — **tuyệt đối không**:

- Gọi persistent storage (NVS/flash config API)
- Gọi file I/O
- Gọi hàm blocking (delay, semaphore-take-với-timeout)
- Giữ lock có khả năng contend cross-core trong thời gian dài

**Thay vào đó:** set 1 flag (`volatile bool`) hoặc push vào queue ngay trong callback, xử lý
thật ở task nền (main loop task hoặc task riêng theo mục 6). Callback chỉ làm việc tối thiểu để
không giữ driver/ISR context lâu.

### Phân loại đồng bộ hoá theo kích thước dữ liệu chia sẻ giữa callback ↔ task

| Kiểu dữ liệu | Cơ chế |
|---|---|
| Single-word nguyên tử theo kiến trúc CPU (vd `uint32_t` trên MCU 32-bit) | Không cần lock |
| Đa-word (64-bit, struct nhỏ) | Critical section ngắn (`portENTER/EXIT_CRITICAL` hoặc tương đương) |
| Buffer/struct lớn, kích thước thay đổi (String, vector) | RAII guard object nắm semaphore trong ctor/dtor — không giữ lock qua I/O |
| Dữ liệu chỉ callback đọc, task ghi | Cache vào buffer cố định-kích-thước, ghi dưới critical section ngắn; đọc từ callback không cần lock nếu write tự vô hiệu hoá preemption trên cùng core |

---

## 8. Reliable delivery / durable work-queue pattern

Cho mọi thao tác **không được phép mất** (gửi dữ liệu, tải file cần cài đặt...):

1. **Ghi ý định xuống persistent storage TRƯỚC khi thử thao tác rủi ro** (cache request ra flash
   trước khi gửi qua mạng; enqueue vào "proc file" trước khi bắt đầu tải).
2. Thực hiện thao tác.
3. **Chỉ xoá record khi thao tác đã xác nhận hoàn tất** (nhận ACK; rename file tạm → file đích
   thành công) — không bao giờ xoá-trước-rồi-thử.
4. Nếu bước 2 fail hoặc timeout → record vẫn còn nguyên → tự động retry ở lần poll sau.

**Ghi file atomic:** luôn ghi vào path tạm rồi `rename()` đè lên path đích — không ghi trực tiếp
vào file đích (tránh để lại file half-written nếu mất điện/crash giữa chừng).

**Bug lớp này rất dễ tái diễn:** khi so khớp "đây có phải entry cần xoá không", đảm bảo 2 bên so
sánh **cùng một dạng chuẩn hoá** của key (vd cùng đã strip prefix hay cùng chưa strip) — lệch
chuẩn hoá ở 1 trong 2 phía khiến điều kiện xoá không bao giờ đúng, entry ở lại vĩnh viễn → work
queue lặp vô hạn. Trace tay qua toàn bộ vòng đời của 1 entry (ghi → đọc lại → xoá) trước khi coi
pattern này là xong.

**Khi thao tác thất bại giữa chừng** (timeout/lỗi ghi, không phải "server đóng kết nối sớm nhưng
biết chính xác length"): dọn sạch mọi file tạm/đích dở dang, và log phải phản ánh đúng trạng
thái thất bại — không log "done"/"success" vô điều kiện ở cuối hàm rồi mới check return value.

---

## 9. Debug-only fast-path phải tường minh

Khi 1 module có `#ifdef DEBUG` để tăng tốc test trên bench (rút ngắn interval, auto-fire...):

- Thay đổi **duy nhất tham số thời gian** (constant interval) — không đồng thời đổi số lượng
  item được xử lý hay logic chọn item; trộn 2 mối quan tâm (tốc độ vs. phạm vi xử lý) trong cùng
  1 field khiến hành vi DEBUG khó dự đoán và dễ che giấu bug ở nhánh production.
- Nếu có **nhiều module song sinh** (mục 10 — chỉ áp dụng khi project có nhiều firmware), sửa
  DEBUG-path ở 1 bên **phải chủ động kiểm tra** có áp dụng tương tự cho bên kia không.
- Comment giải thích **con số cụ thể** (interval bao nhiêu ms, vì sao) — không để comment mô tả
  1 giá trị còn code chạy giá trị khác.

---

## 10. Module "song sinh" giữa các firmware (chỉ áp dụng nếu có nhiều firmware — mục 1)

Khi cùng 1 protocol/feature tồn tại ở ≥2 firmware gần như giống hệt nhau:

- **Giữ 2 file song sinh giống nhau tối đa về cấu trúc** (chỉ đổi tiền tố hàm/tag log) — để lần
  sau đọc lại, khác biệt giữa 2 file lộ ra ngay dưới dạng diff.
- Khi review/sửa 1 trong 2 file, **luôn mở file kia song song**: review riêng từng file (không
  so sánh) dễ bỏ sót bug tồn tại độc lập ở cả 2 bên (code giống hệt → bug copy-paste cũng giống
  hệt); review dạng so sánh dễ phát hiện lệch pha ngoài ý muốn — **cả 2 kiểu đều cần làm**.
- Fix ở 1 file → chủ động kiểm tra file song sinh có cùng đoạn logic không trước khi coi task
  xong.
- Duplicate chấp nhận được khi hành vi có khả năng phân kỳ theo firmware; nếu qua nhiều lần sửa
  mà chưa từng phân kỳ thật, cân nhắc rút phần chung ra 1 hàm tham số hoá.

---

## 11. Identity vs. mutable config

- **Định danh bất biến của thiết bị** nên nằm ở kho write-once cấp phần cứng (eFuse/OTP) nếu
  chip hỗ trợ — không chung namespace với config runtime hay bị factory reset chạm tới.
- **Config runtime** (mode, lịch trình, feature flag) — namespace hoá theo từng concern. Factory
  reset phải liệt kê chính xác tập namespace đang dùng và chỉ xoá đúng tập đó.

---

## 12. Quy trình review & sửa code (áp dụng cho người lẫn AI agent)

- **Khi được yêu cầu sửa 1 tính năng, rà lại toàn bộ thành phần liên quan trước khi sửa** —
  không chỉ file được chỉ định. Gồm: file song sinh nếu có (mục 10); mọi caller/consumer của
  hàm/API sắp đổi (grep toàn repo, không đoán); header tương ứng (.h đi cùng .cpp); tài liệu
  (README/RULES) mô tả feature đó nếu có. Sửa xong 1 điểm mà bỏ sót thành phần liên quan là
  nguồn phổ biến nhất của bug "sửa chỗ này, vỡ chỗ khác" hoặc để lại state/hành vi không đồng bộ
  giữa 2 nơi lẽ ra phải khớp nhau.
- **Đọc toàn bộ file liên quan trước khi sửa** — không sửa chỉ dựa trên grep snippet; context
  xung quanh (macro, `#ifdef`, thứ tự gọi hàm khác trong cùng file) thường đổi ý nghĩa đoạn đang
  sửa.
- **Xác minh lại finding trước khi báo cáo hoặc fix** — đọc code thật tại vị trí report, trace
  luồng dữ liệu thật trước khi kết luận có bug. Nhiều finding "nhìn giống bug" là false positive
  khi đọc đủ ngữ cảnh.
- **Khi review 1 thay đổi đổi timeout của lệnh gọi blocking thành 0/ngắn để chạy lặp qua
  `_loop()`/retry loop (reconnect, polling...)**, bắt buộc mở đọc implementation THẬT của hàm bị
  gọi (không dừng ở đọc comment giải thích "không block" trong diff) để tự xác minh tính idempotent
  theo đúng mục 6 — comment giải thích lý do không block có thể đúng nhưng không chứng minh được
  an toàn khi gọi lặp. Đây là bug lớp rất dễ lọt qua review nếu chỉ đọc đoạn code thay đổi mà
  không trace tiếp vào hàm bị gọi.
- **Không để lại code chết**: nhánh không thể đạt được, check lặp liên tiếp không cần thiết, hàm
  không còn caller, biến flag mà logic tương ứng đã bị thay bằng cơ chế khác.
- **Dùng biến thể reentrant của hàm C stdlib giữ state toàn cục** (vd `strtok_r` thay `strtok`)
  trong môi trường multi-task.
- **Comment trên 1 hàm/đoạn code chỉ 1-2 dòng, mô tả hàm đó làm gì / hoạt động ra sao** — không
  viết comment block 3+ dòng giải thích đầy đủ lý do/thiết kế. Nếu WHY thực sự cần ghi lại, nén
  còn 1 dòng cốt lõi; phần diễn giải sâu hơn nói trong chat, không đưa vào code. Không áp dụng
  cho: divider trực quan (`// ── ... ──`), doc-comment API public trên khai báo hàm trong header,
  hay bảng/spec tham chiếu (wire format, danh sách lệnh).
- Sau khi sửa cấu trúc (đổi include, di chuyển hàm giữa lib, đổi flow), **build lại toàn bộ
  environment liên quan** trước khi báo hoàn thành.
- **Khi được yêu cầu làm 1 feature mới, trước tiên đánh giá code đó thuộc `core/`, `feature/`,
  hay `lib/`** theo checklist ở mục 2/3/4, rồi mới bắt đầu viết. Nếu sau khi áp checklist vẫn
  không rõ ràng (vd chưa chắc có consumer thứ 2 hay không, chưa chắc có nên tách hạ tầng dùng
  chung hay để riêng trong feature) → đề xuất phương án + hỏi user trước khi code, không tự chọn
  đại 1 hướng. Nếu checklist cho ra kết quả rõ ràng (vd chỉ 1 nơi dùng, gắn chặt business logic
  → chắc chắn là `feature/`) → làm luôn theo đúng vị trí đó, không cần hỏi lại.
- **Feature mới hoặc mở rộng feature có tần suất chạy/nhận dữ liệu đáng kể** → áp dụng bước "dự
  đoán ở volume lớn" của mục 6 (tần suất × volume lớn nhất có thể gặp) trước khi chốt thiết kế —
  kể cả khi đã rõ ràng không cần task riêng, vẫn phải tính xem buffer/queue/RAM có đủ ở volume
  lớn không, không chỉ ở mức test tay nhẹ nhàng.
- **Feature mới cần phản hồi gần thời gian thực** (đọc bus/giao thức ngoài có timeout riêng,
  polling cảm biến...) → bắt buộc quay lại rà mục 6 cho mọi block hiện có, không chỉ đánh giá
  feature mới riêng lẻ — 1 block từng "chấp nhận được" có thể không còn đúng sau khi thêm feature
  này. **Áp dụng CẢ khi đang review 1 thay đổi đã thêm loại feature này** (không chỉ khi tự viết
  code) — kết quả rà soát (có/không có block nào giờ vượt ngưỡng) phải nêu rõ trong báo cáo review,
  không phải bước ngầm có thể lặng lẽ bỏ qua vì "không nằm trong diff đang xem".
- **Sau khi phân tích/review/áp checklist kết luận rõ ràng "nên làm X"**, chủ động đề xuất thực
  hiện X (hỏi user có muốn làm luôn không) — không dừng lại ở việc nêu kết luận rồi im lặng chờ
  user tự hỏi lại bước tiếp theo hiển nhiên đó. Không áp dụng khi user đã yêu cầu rõ chỉ phân
  tích/trả lời, không sửa code — lúc đó vẫn có thể đề xuất bằng lời nhưng không tự ý code.

---

## 13. Khi nào 1 lesson đáng nâng lên thành rule ở đây

Chỉ thêm vào file này khi pattern/bug **có khả năng tái diễn ở project khác**, không phải chi
tiết nghiệp vụ 1 lần của project hiện tại. Chi tiết cụ thể (tên namespace, giá trị pin, format
packet...) thuộc tài liệu riêng của từng project — file này chỉ giữ lại *hình dạng* của vấn đề
và *nguyên tắc* xử lý.
