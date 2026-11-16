# ESP32 Smart Datalogger — gửi dữ liệu qua LINE

ESP32 nhận trạng thái cảm biến từ STM32 qua UART, phục vụ dashboard điều khiển hiện có và gửi snapshot tới FastAPI. Server giữ channel access token và chuyển dữ liệu sang LINE Messaging API. Xem [hướng dẫn server](../../esp-line-app-system/apps/line-dashboard/README.md).

## Cấu hình

Copy file mẫu, không chỉnh trực tiếp file mẫu để giữ key ngoài source:

```bash
cp include/line_config.example.h include/line_config.h
```

Điền trong `include/line_config.h`:

```cpp
#pragma once
#define LINE_ENABLED true
#define LINE_SERVER_URL "http://192.168.1.100:8000"
#define LINE_API_KEY "same-value-as-server-DATALOGGER_API_KEY"
#define LINE_DEVICE_ID "logger-01"
#define LINE_SEND_INTERVAL_MS 300000UL
#define LINE_HTTP_TIMEOUT_MS 10000UL
#define LINE_MAX_ATTEMPTS 4U
#define LINE_ROOT_CA ""
```

| Macro | Ý nghĩa |
| --- | --- |
| `LINE_ENABLED` | Bật gửi dữ liệu; không có config sẽ tắt gửi |
| `LINE_SERVER_URL` | Base URL máy chạy FastAPI, không kèm `/api/telemetry`; không dùng `localhost` |
| `LINE_API_KEY` | Giống `DATALOGGER_API_KEY` của server; không phải token LINE |
| `LINE_DEVICE_ID` | Tên 1–64 ký tự chữ/số hoặc `- _ . :`; để trống tự tạo từ MAC |
| `LINE_SEND_INTERVAL_MS` | Chu kỳ gửi, mặc định 300000 ms (5 phút); tối thiểu 1000 ms |
| `LINE_HTTP_TIMEOUT_MS` | Timeout kết nối/đọc HTTP, mặc định 10000 ms; khoảng 1000–60000 |
| `LINE_MAX_ATTEMPTS` | Tổng số lần thử mỗi snapshot, mặc định 4; khoảng 1–6 |
| `LINE_ROOT_CA` | Root CA PEM để xác minh HTTPS; để trống chỉ khi dùng HTTP |

HTTP dùng để thử nghiệm trên LAN tin cậy. Khi dùng URL HTTPS công khai, điền root CA của server theo ví dụ trong file config. Firmware xác minh certificate/domain và cần đồng bộ NTP (`pool.ntp.org`/`time.google.com`); không có chế độ bỏ xác minh TLS. Không dùng certificate/token LINE làm CA của server riêng.

Mỗi snapshot tạo một tin LINE. Chu kỳ 5 phút tương đương khoảng 288 tin/ngày/thiết bị khi chạy liên tục; tăng chu kỳ theo hạn mức của Official Account. Test nhanh có thể dùng 60000 ms rồi tăng lại.

## Build, nạp và nối Wi-Fi

Cài [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) hoặc extension PlatformIO trong VS Code. Chạy tại thư mục firmware:

Project khóa platform `espressif32@8.0.0` để các lần build dùng cùng phiên bản framework.

```bash
pio run -e esp32dev
pio run -e esp32dev --target upload
pio device monitor --baud 115200
```

Nếu cần chỉ định cổng USB:

```bash
pio run -e esp32dev --target upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Lần đầu chưa lưu Wi-Fi, ESP32 mở AP `SmartDatalogger-Setup`, mật khẩu mặc định `12345678`. Kết nối AP, mở `http://192.168.4.1` và nhập Wi-Fi của mạng có thể truy cập FastAPI. Sau khi kết nối STA, địa chỉ dashboard ESP32 được in ra Serial. AP phục vụ cấu hình; ESP32 chỉ gửi LINE khi đã kết nối STA.

Mỗi lần kết nối Wi-Fi có tối đa 15 giây để hoàn tất. Nếu thất bại, ESP32 mở AP cấu hình; khi đã có mạng lưu và AP không có client trong 60 giây, thiết bị tự thử kết nối lại. Nếu đang có client cấu hình nối vào AP, thiết bị giữ AP cho đến khi client ngắt kết nối.

Kiểm tra server trước bằng cách mở dashboard server và gửi một tin thủ công. ESP32 dùng địa chỉ LAN của máy server, ví dụ `http://192.168.1.100:8000`, không phải `http://localhost:8000`.

## Hoạt động và lỗi

- `LineUplink_Init()` tạo queue 1 snapshot và task gửi HTTP riêng. `LineUplink_Task()` trong loop chỉ copy trạng thái, không đợi request, nên UART và web điều khiển tiếp tục chạy.
- Lần đầu đợi 5 giây sau khi Wi-Fi kết nối để STATUS từ STM32 điền dữ liệu; sau đó theo chu kỳ cấu hình.
- Payload gửi đến `POST /api/telemetry` có `X-API-Key` và `X-Event-ID` UUID. Retry giữ nguyên UUID và payload để server/LINE tránh gửi trùng.
- Lỗi mạng, HTTP 408/429/5xx được thử lại với thời gian chờ tăng dần; mặc định tối đa 4 lần. Lỗi cấu hình 4xx khác cần sửa key/URL/payload.
- Queue chỉ giữ snapshot chờ mới nhất. Sau khi hết số lần retry thì bỏ snapshot đó; không ghi hàng đợi vào flash. Reset/mất điện/mất mạng có thể làm mất mẫu. Đây là gửi trạng thái định kỳ, không phải tải toàn bộ LOG của STM32.
- Nhiệt độ, độ ẩm, áp suất, tiếng ồn gửi dưới dạng số nguyên nhân 10; server đổi đơn vị. Trạng thái offline và validMask giúp server không báo dữ liệu lỗi thành số 0 hợp lệ.

Log Serial có prefix `[LINE]`:

```text
[LINE] Uplink enabled for logger-01; interval=300000 ms
[LINE] Telemetry accepted by server (HTTP 200)
```

Nếu `Uplink disabled`: kiểm tra config và nạp lại. Nếu `HTTP 401`: key hai phía khác nhau. Nếu `HTTP 503`: server thiếu cấu hình. Nếu `HTTP 502/504`: lỗi gửi LINE ở server. Nếu mã âm: kiểm tra Wi-Fi, IP/port, firewall, TLS/NTP. Xem [README server](../../esp-line-app-system/apps/line-dashboard/README.md) cho các bước kiểm tra LINE.

Các chức năng UART, dashboard local, relay/timer/thermal vẫn dùng giao thức cũ. Không cần thay firmware STM32 để gửi snapshot này.
