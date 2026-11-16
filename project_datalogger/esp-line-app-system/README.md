# ESP32 / LINE app system

Server FastAPI nhận telemetry từ Smart Datalogger ESP32, lưu snapshot vào SQLite và gửi tin bằng LINE Messaging API. Dashboard hiển thị dữ liệu thực, trạng thái gửi và cho phép gửi tin thủ công.

- [Hướng dẫn server, LINE Official Account, webhook và triển khai](apps/line-dashboard/README.md).
- Firmware thực tế trong workspace nằm tại `../smart_datalogger_v1/esp32_smartlogger`; xem [README firmware](../smart_datalogger_v1/esp32_smartlogger/README.md).
- [Hướng dẫn tổng thể của workspace](../README.md).
- `apps/test` là các script/demo cũ, không dùng để chạy hệ thống mới.

LINE Notify đã ngừng hoạt động. Cấu hình bằng `LINE_CHANNEL_ACCESS_TOKEN` của Messaging API; không dùng `LINE_NOTIFY_TOKEN`.
