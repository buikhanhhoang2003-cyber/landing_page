# LINE FastAPI server cho Smart Datalogger

Server nhận snapshot cảm biến từ ESP32, lưu vào SQLite, chuyển thành tin nhắn LINE và cung cấp dashboard. Dữ liệu gồm nhiệt độ, độ ẩm, áp suất, ánh sáng, tiếng ồn, ADC/DI/relay và tóm tắt nhiệt ảnh; không gửi toàn bộ 768 pixel nhiệt ảnh.

```text
STM32 → UART → ESP32 → POST /api/telemetry → FastAPI → LINE Messaging API
                                                └→ SQLite → Dashboard
LINE → POST /api/line/webhook → xác minh chữ ký → danh sách user/group ID
```

## 1. Chuẩn bị LINE Official Account

LINE Notify đã [ngừng hoạt động từ 31/03/2025](https://developers.line.biz/en/news/2025/04/01/line-notify/). Token Notify không dùng được cho Messaging API.

1. [Tạo LINE Official Account và bật Messaging API](https://developers.line.biz/en/docs/messaging-api/getting-started/) trong LINE Official Account Manager.
2. Mở channel tương ứng tại [LINE Developers Console](https://developers.line.biz/console/).
3. Ở tab **Messaging API**, cấp channel access token và lưu thành `LINE_CHANNEL_ACCESS_TOKEN` trên server.
4. Thêm Official Account làm bạn trong LINE app. Nếu gửi vào nhóm, bật **Allow bot to join group chats** và mời bot vào nhóm.
5. Chọn ID nhận tin:
   - Cá nhân: lấy **Your user ID** trong Basic settings, hoặc lấy từ webhook. Đây là ID dạng `U` + 32 ký tự hex, không phải tên hiển thị hay LINE ID dùng để tìm bạn.
   - Nhóm: lấy `groupId` dạng `C...` từ webhook, theo mục 5 dưới đây.
   - Chat nhiều người: `roomId` dạng `R...` nếu được webhook cung cấp.
6. Đặt ID vào `LINE_TARGET_ID`. `LINE_GROUP_ID` được hỗ trợ khi `LINE_TARGET_ID` để trống.

Server chỉ dùng **Messaging API push/broadcast**. Webhook không tự chọn người nhận hoặc tự gửi tin. Xem [tài liệu gửi tin](https://developers.line.biz/en/docs/messaging-api/sending-messages/) và [lấy user ID](https://developers.line.biz/en/docs/messaging-api/getting-user-ids/).

## 2. Cài đặt và chạy local

Yêu cầu Python 3.10+ và máy có kết nối Internet tới `api.line.me`. Từ thư mục này:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp .env.example .env
python -c 'import secrets; print(secrets.token_urlsafe(32))'
```

Dùng chuỗi vừa tạo làm `DATALOGGER_API_KEY`, rồi chỉnh `.env`:

```dotenv
DATALOGGER_API_KEY=replace-with-the-generated-random-key
LINE_CHANNEL_ACCESS_TOKEN=your-messaging-api-channel-access-token
LINE_TARGET_ID=U0123456789abcdef0123456789abcdef
LINE_CHANNEL_SECRET=
DATABASE_PATH=data/datalogger.sqlite3
LINE_HTTP_TIMEOUT_SECONDS=6
```

ID trên là ví dụ, phải thay bằng ID thật. `LINE_CHANNEL_SECRET` chỉ cần cho webhook; lấy từ **Basic settings → Channel secret**. Không điền channel secret vào chỗ channel access token. Không commit `.env`.

```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

- Dashboard: `http://localhost:8000/`. Nhập API key để xem dữ liệu và gửi tin. Key chỉ giữ trong trang đang mở; channel access token không đưa xuống browser.
- OpenAPI: `http://localhost:8000/docs`.
- Health: `http://localhost:8000/healthz`.
- Máy khác/ESP32 dùng `http://<IP-LAN-của-server>:8000`. Cho phép TCP 8000 qua firewall và bảo đảm Wi-Fi không bật client isolation.
- Thiếu API key sẽ khóa các API dữ liệu/gửi tin. Health vẫn trả trạng thái sống của ứng dụng, không xác nhận token LINE hợp lệ.
- Thay `.env` cần khởi động lại server; biến môi trường đã export được ưu tiên hơn `.env`.

## 3. Kiểm tra trước khi nạp ESP32

Các lệnh POST dưới đây thực sự gửi tin khi cấu hình đúng. Đặt API key trong terminal:

```bash
read -rsp 'Datalogger API key: ' DATALOGGER_API_KEY
export DATALOGGER_API_KEY
```

Gửi tin tới `LINE_TARGET_ID` mặc định:

```bash
curl --fail-with-body http://localhost:8000/api/line/push \
  -H "X-API-Key: $DATALOGGER_API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"message":"Kiem tra Smart Datalogger → LINE"}'
```

Giả lập một snapshot đúng định dạng ESP32:

```bash
EVENT_ID=$(python -c 'import uuid; print(uuid.uuid4())')
curl --fail-with-body http://localhost:8000/api/telemetry \
  -H "X-API-Key: $DATALOGGER_API_KEY" \
  -H "X-Event-ID: $EVENT_ID" \
  -H 'Content-Type: application/json' \
  -d '{"device_id":"esp32-demo","uptime_ms":6000,"online":true,"mode":1,"temperature10":285,"humidity10":654,"pressure10":10132,"lightLux":320,"noise10":452,"validMask":15,"adc1Mv":1234,"adc2Mv":2345,"diMask":3,"relays":1,"thermalReady":false,"thermalAmbient10":0,"thermalCenter10":0,"thermalMin10":0,"thermalMax10":0}'
```

Kết quả thành công có `success: true`, `line_status: "sent"`. Snapshot mẫu tương ứng 28.5 °C, 65.4 %, 1013.2 hPa, 320 lux, 45.2 dB. Gửi lại **cùng payload và cùng EVENT_ID** không tạo tin mới; snapshot mới phải dùng UUID mới.

```bash
curl --fail-with-body http://localhost:8000/api/telemetry/latest \
  -H "X-API-Key: $DATALOGGER_API_KEY"
```

## 4. Cấu hình ESP32

Xem [README firmware](../../../smart_datalogger_v1/esp32_smartlogger/README.md) trong workspace; nếu chỉ clone repo server thì firmware ở project `smart_datalogger_v1` riêng.

Copy `include/line_config.example.h` thành `include/line_config.h` trong firmware:

```cpp
#define LINE_ENABLED true
#define LINE_SERVER_URL "http://192.168.1.100:8000"
#define LINE_API_KEY "same-value-as-DATALOGGER_API_KEY"
#define LINE_DEVICE_ID "logger-01"
#define LINE_SEND_INTERVAL_MS 300000UL
```

`LINE_SERVER_URL` là base URL, không kèm `/api/telemetry`; không dùng `localhost`. Bật Wi-Fi STA để ESP32 truy cập server. Mặc định gửi mỗi 5 phút; tăng chu kỳ để phù hợp hạn mức tin của Official Account. Firmware chỉ chứa key gọi server, không chứa token LINE.

## 5. Webhook để lấy User / Group ID

Bỏ qua bước này nếu đã biết ID nhận tin. LINE cần truy cập được URL **HTTPS công khai** của server. Dùng domain triển khai hoặc tunnel HTTPS tới cổng 8000 cho thử nghiệm; địa chỉ LAN/localhost không dùng làm webhook URL.

1. Đặt `LINE_CHANNEL_SECRET` đúng channel rồi restart server.
2. Trong LINE Developers Console, đặt Webhook URL: `https://your-domain/api/line/webhook`.
3. Nhấn **Verify**, sau đó bật **Use webhook**. Server nhận được cả request xác minh có `events: []`.
4. Gửi tin cho bot hoặc gửi tin trong nhóm có bot.
5. Trên dashboard, nhấn **Tải danh sách**, hoặc:

   ```bash
   curl --fail-with-body http://localhost:8000/api/line/targets \
     -H "X-API-Key: $DATALOGGER_API_KEY"
   ```

6. Copy `target_id` mong muốn vào `LINE_TARGET_ID`, restart server và thử push.

Webhook dùng `X-Line-Signature`, không dùng `X-API-Key`. Server [kiểm tra HMAC-SHA256 trên body nguyên bản](https://developers.line.biz/en/docs/messaging-api/verify-webhook-signature/), rồi mới đọc event. Nếu reverse proxy sửa body, chữ ký sẽ không hợp lệ.

## 6. API và lưu trữ

| Endpoint | Xác thực | Chức năng |
| --- | --- | --- |
| `GET /healthz` | Không | Kiểm tra process hoạt động |
| `GET /api/status` | Không | Cờ đã cấu hình token, key, target và webhook; không trả bí mật |
| `POST /api/telemetry` | `X-API-Key`, `X-Event-ID` UUID | Lưu snapshot rồi gửi LINE |
| `GET /api/telemetry/latest` | `X-API-Key` | Snapshot mới nhất theo thiết bị và trạng thái gửi |
| `POST /api/line/push` | `X-API-Key` | `{"message":"...","user_id":"U..."}`; bỏ ID dùng target mặc định |
| `POST /api/line/broadcast` | `X-API-Key` | `{"message":"..."}` gửi tất cả người theo dõi |
| `POST /api/notify` | `X-API-Key` | API tương thích dashboard cũ: `message`, `target`, `type` |
| `POST /api/line/webhook` | `X-Line-Signature` | Nhận và xác minh webhook LINE |
| `GET /api/line/targets` | `X-API-Key` | Các ID đã thấy từ webhook |

Tin thủ công còn hỗ trợ `image` là URL HTTPS và `sticker: ["packageId", "stickerId"]`. Không nhận token LINE do browser/ESP32 truyền vào. Không có cơ chế điều khiển relay qua tin nhắn LINE trong bản này.

`validMask` của firmware: bit 0 cho nhiệt độ/độ ẩm (SHT30), bit 1 áp suất (BME280), bit 2 ánh sáng (SEN0390), bit 3 tiếng ồn (SM7901B). Các trường có hậu tố `10` chia 10 khi hiển thị. STM32 offline hoặc bit không hợp lệ được hiển thị là không có dữ liệu.

SQLite được tạo tự động ở `DATABASE_PATH`. Giữ thư mục này nếu restart server. API trả trạng thái LINE `pending`, `failed` hoặc `sent`; lỗi gửi LINE không làm mất snapshot đã nhận. Không có tiến trình tự gửi lại các bản ghi `failed`: ESP32 thử lại có giới hạn rồi chuyển sang snapshot kế tiếp. Không phải cơ chế bảo đảm lưu mọi mẫu khi mất mạng.

Retry giữ nguyên nội dung và người nhận; server dùng [X-Line-Retry-Key](https://developers.line.biz/en/docs/messaging-api/retrying-api-request/) để tránh gửi trùng. Chỉ retry trong 24 giờ; giữ UUID cho cùng một lần gửi. LINE chấp nhận request không bảo đảm người dùng nhìn thấy tin nếu họ đã block bot.

## 7. Triển khai HTTPS / Render

Để ESP32 gửi qua Internet, dùng HTTPS với chứng chỉ hợp lệ. Đặt `LINE_SERVER_URL` thành `https://your-domain` và `LINE_ROOT_CA` thành root CA PEM của chứng chỉ server. ESP32 cần NTP để kiểm tra hạn chứng chỉ; firmware không tắt kiểm tra TLS. Dashboard cũng nên mở bằng HTTPS vì API key được gửi trong header.

Repo có Blueprint ở `render.yaml` và bản cùng cấu hình ở `apps/line-dashboard/render.yaml`. Khi triển khai repo `esp-line-app-system`:

1. Kết nối repo trong Render, tạo Blueprint từ `render.yaml` ở root.
2. Đặt `DATALOGGER_API_KEY`, `LINE_CHANNEL_ACCESS_TOKEN`, `LINE_TARGET_ID`, và `LINE_CHANNEL_SECRET` nếu dùng webhook.
3. Service dùng root directory `apps/line-dashboard`, build `pip install -r requirements.txt`, start `uvicorn main:app --host 0.0.0.0 --port $PORT`, health check `/healthz`.
4. Dùng URL HTTPS nhận được cho firmware và webhook.

Filesystem mặc định của Render là tạm thời: để giữ SQLite qua redeploy/restart, gắn persistent disk, ví dụ `/var/data`, rồi đặt `DATABASE_PATH=/var/data/datalogger.sqlite3`. Blueprint không tự tạo disk. Xem [Render persistent disks](https://render.com/docs/disks). Chạy một instance với SQLite; không chia database này giữa nhiều máy. Dịch vụ bị sleep/cold start có thể làm ESP32 timeout, cần xét khi chọn cấu hình hosting.

## 8. Xử lý lỗi và kiểm thử

| Hiện tượng | Kiểm tra |
| --- | --- |
| ESP32 log uplink disabled | Copy `line_config.example.h` → `line_config.h`, bật `LINE_ENABLED`, build/nạp lại |
| HTTP âm/timeout từ ESP32 | IP server, port/firewall, cùng LAN, Wi-Fi có Internet, server đang chạy |
| HTTPS không gửi | Root CA đúng domain, thời gian NTP, chứng chỉ còn hiệu lực; không dùng chứng chỉ tự ký khi chưa cấu hình CA |
| `401` từ server | `LINE_API_KEY` của ESP32 và `DATALOGGER_API_KEY` của server phải giống nhau |
| `422` | JSON/kiểu dữ liệu sai hoặc thiếu/sai UUID `X-Event-ID` |
| `409` | UUID được dùng lại cho nội dung khác; tạo UUID mới cho snapshot mới |
| `503` | Server thiếu cấu hình key/token/target hoặc webhook secret |
| `502` / `504` | LINE lỗi hoặc timeout; xem `line_error`, kiểm tra token, target, hạn mức tin |
| LINE không có tin dù accepted | Add friend/bỏ block bot; bot đã ở đúng nhóm; ID từ đúng provider/channel |
| Webhook verify thất bại | HTTPS công khai, secret đúng, proxy giữ nguyên body |
| STM32 offline hoặc giá trị `--` | UART và cảm biến; xem trạng thái hợp lệ trên dashboard ESP32 |

Chạy bộ test với upstream LINE giả lập, không gửi tin thật:

```bash
python -m pip install -r requirements-dev.txt
python -m pytest -q
```

Không commit `.env`, database, hoặc cấu hình firmware chứa key.
