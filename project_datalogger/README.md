# Smart Datalogger: ESP32 → FastAPI → LINE

Firmware và server trong project kết nối theo luồng:

```text
STM32 (cảm biến/UART) → ESP32 (Wi-Fi)
                            │ POST /api/telemetry + X-API-Key + X-Event-ID
                            ▼
                     FastAPI + SQLite → LINE Messaging API → LINE app
                            │
                            └─ Dashboard: dữ liệu ESP32 gần nhất
```

- Firmware: [`smart_datalogger_v1/esp32_smartlogger`](smart_datalogger_v1/esp32_smartlogger/README.md).
- Server và hướng dẫn cấu hình LINE: [`esp-line-app-system/apps/line-dashboard`](esp-line-app-system/apps/line-dashboard/README.md).
- `esp-line-app-system` là Git repository riêng; firmware ở thư mục bên cạnh, cần sao lưu/commit riêng khi quản lý phiên bản.

## Copy full project bằng một file `.sh`

File đã đóng gói: [`dist/project_datalogger_installer.sh`](dist/project_datalogger_installer.sh). Chỉ cần copy **file này** sang máy Linux khác, rồi chạy:

```bash
bash project_datalogger_installer.sh --directory "$HOME/my_datalogger"
```

File `.sh` này tự chứa source của cả STM32, ESP32, FastAPI, test và tài liệu. Bạn chỉ cần copy file đó; không cần mang theo thư mục source gốc. Khi chạy, nó tạo cây project ở đường dẫn mới và kiểm tra checksum. **Mặc định chỉ tạo source, không tải thư viện, không build và không chạy server.** Thư mục đích phải mới hoặc rỗng; không ghi đè project đang làm việc.

```bash
# Chỉ tạo lại cây source, chưa cài/build/chạy
bash project_datalogger_installer.sh --directory ./my_datalogger --extract-only

# Muốn tạo source rồi setup luôn: thêm --setup; truyền tùy chọn sau dấu --
bash project_datalogger_installer.sh --directory ./my_datalogger --setup -- \
  --server-url http://192.168.1.100:8000 --no-start
```

Gói không chứa `.env`, `line_config.h`, database, Git history, virtualenv, toolchain hoặc file build/cache/metadata IDE. Các giá trị Wi-Fi viết cứng trong hai demo firmware cũ được đổi thành placeholder **trong bản đóng gói**; source gốc không bị sửa. `PROJECT_MANIFEST.json` ở bản giải nén liệt kê các file và checksum. Installer là snapshot; tạo lại sau mỗi lần thay đổi source:

```bash
python3 scripts/build_installer.py
```

Test riêng cho script/setup/installer, không tải thư viện, không gửi LINE và không nạp board:

```bash
python3 -m unittest discover -s tests -v
```

## Setup toàn project bằng một lệnh

Trên Linux có **Bash và Python 3.10+**, chạy ngay tại thư mục này:

```bash
./setup_project.sh
```

Script hỏi thông tin cấu hình, tạo `.env` của FastAPI và `include/line_config.h` của ESP32 với cùng API key, tạo virtualenv/cài thư viện, chuẩn bị PlatformIO nếu chưa có, build ESP32 rồi chạy server. Token/secret nhập trong terminal được ẩn. Cần Internet để tải các dependency/toolchain lần đầu. Script không dùng `sudo` và không tự nạp board.

Source STM32, ESP32, FastAPI, project STM32CubeIDE và các PDF đều nằm trong project. Phần STM32 giữ nguyên source và cấu hình để import bằng STM32CubeIDE; script không tự cài IDE hoặc nạp STM32. LINE Official Account/token/ID người nhận phải do bạn cung cấp; nếu bỏ trống, server vẫn có thể chạy nhưng gửi LINE chưa hoạt động.

Các cách chạy thường dùng:

```bash
# Tạo cấu hình, không cài thư viện/build/chạy server (dùng được offline)
./setup_project.sh --configure-only

# Setup + build xong thì thoát, chưa chạy server
./setup_project.sh --no-start

# Chỉ setup server, bỏ qua build ESP32
./setup_project.sh --skip-build

# Chỉ rõ địa chỉ mà ESP32 sẽ gọi; không dùng localhost
./setup_project.sh --server-url http://192.168.1.100:8000

# HTTPS: dùng root CA của domain server
./setup_project.sh --server-url https://your-domain --ca-file /path/to/root-ca.pem

./setup_project.sh --help
```

Chạy lại giữ nguyên cấu hình đã có. Nếu cố ý muốn cấu hình lại, dùng `--force-config`; tùy chọn này tạo lại header với chu kỳ gửi 5 phút và timeout/retry mặc định, nên kiểm tra lại các giá trị trước khi nạp firmware. Mở dashboard tại `http://localhost:8000` và lấy `DATALOGGER_API_KEY` trong file `.env` để đăng nhập. Thay port bằng `--port`, đồng thời đặt URL ESP32 đúng port đó.

Để chạy không hỏi nhập, dùng `--non-interactive`; có thể cấp `LINE_CHANNEL_ACCESS_TOKEN`, `LINE_TARGET_ID`, `LINE_CHANNEL_SECRET`, `DATALOGGER_API_KEY`, `LINE_SERVER_URL` qua biến môi trường. Tránh ghi token thật vào lệnh lưu trong shell history. Key được tự sinh nếu chưa có.

## Setup thủ công

1. Tạo LINE Official Account, bật Messaging API, lấy channel access token và ID người/nhóm nhận tin theo README server. LINE Notify cũ đã [ngừng hoạt động ngày 31/03/2025](https://developers.line.biz/en/news/2025/04/01/line-notify/); không dùng token Notify.
2. Khởi động server:

   ```bash
   cd esp-line-app-system/apps/line-dashboard
   python3 -m venv .venv
   source .venv/bin/activate
   python -m pip install -r requirements.txt
   cp .env.example .env
   # Điền DATALOGGER_API_KEY, LINE_CHANNEL_ACCESS_TOKEN, LINE_TARGET_ID trong .env.
   uvicorn main:app --host 0.0.0.0 --port 8000
   ```

3. Mở `http://localhost:8000`, nhập API key và gửi một tin thử để kiểm tra LINE.
4. Copy `smart_datalogger_v1/esp32_smartlogger/include/line_config.example.h` thành `line_config.h` cùng thư mục. Bật `LINE_ENABLED`, đặt `LINE_SERVER_URL` là địa chỉ LAN của máy chạy server, ví dụ `http://192.168.1.100:8000`, và đặt `LINE_API_KEY` trùng server.
5. Giữ terminal server đang chạy. Mở terminal khác tại thư mục `project_datalogger`, rồi build và nạp ESP32:

   ```bash
   cd smart_datalogger_v1/esp32_smartlogger
   pio run -e esp32dev
   pio run -e esp32dev --target upload
   pio device monitor --baud 115200
   ```

6. Nếu ESP32 chưa có Wi-Fi: kết nối AP `SmartDatalogger-Setup`, mật khẩu mặc định `12345678`, mở `http://192.168.4.1`, lưu Wi-Fi có thể truy cập server. Xem log `[LINE]`, dashboard server và LINE app.

`localhost` trên ESP32 là chính ESP32; phải dùng IP LAN/domain của server. HTTP phù hợp thử trên LAN tin cậy; triển khai qua Internet cần HTTPS và CA trong firmware. Webhook LINE chỉ cần khi muốn lấy user/group ID tự động, không bắt buộc cho push nếu đã biết ID.

Snapshot gửi theo chu kỳ, mặc định 5 phút; không phải hệ thống lưu và chuyển lại toàn bộ bản ghi khi mất mạng. Xem README thành phần để biết retry, giới hạn lưu trữ và xử lý lỗi.
