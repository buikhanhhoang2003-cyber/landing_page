const byId = (id) => document.getElementById(id);
const keyInput = byId("api-key");
const deviceSelect = byId("device");
let readings = [];
let polling = null;
let loading = false;

async function api(path, options = {}) {
  const key = keyInput.value.trim();
  if (!key) throw new Error("Nhập DATALOGGER_API_KEY trước khi tiếp tục.");
  const response = await fetch(path, {
    ...options,
    headers: { "X-API-Key": key, ...options.headers },
    signal: AbortSignal.timeout(15000),
  });
  const data = await response.json();
  if (!response.ok) {
    const detail = data.detail || data;
    throw new Error(typeof detail === "string" ? detail : JSON.stringify(detail));
  }
  return data;
}

async function fetchStatus() {
  try {
    const response = await fetch("/api/status", { signal: AbortSignal.timeout(10000) });
    if (!response.ok) throw new Error("status");
    const data = await response.json();
    const missing = [];
    if (!data.apiKeyConfigured) missing.push("DATALOGGER_API_KEY");
    if (!data.tokenConfigured) missing.push("LINE_CHANNEL_ACCESS_TOKEN");
    if (!data.targetConfigured) missing.push("LINE_TARGET_ID");
    byId("status").textContent = missing.length
      ? `Server còn thiếu: ${missing.join(", ")}. Xem README để cấu hình.`
      : "Server đã cấu hình API key, token LINE và người nhận.";
  } catch (_) {
    byId("status").textContent = "Không đọc được trạng thái server.";
  }
}

function renderReading() {
  const reading = readings.find((item) => item.device_id === deviceSelect.value);
  const values = [
    ["temperature", "temperature10", 1, 10],
    ["humidity", "humidity10", 1, 10],
    ["pressure", "pressure10", 2, 10],
    ["light", "lightLux", 4, 1],
    ["noise", "noise10", 8, 10],
  ];
  for (const [id, field, mask, scale] of values) {
    const data = reading?.data;
    byId(id).textContent = data?.online && (data.validMask & mask) && Number.isFinite(data[field])
      ? (data[field] / scale).toFixed(scale === 1 ? 0 : 1)
      : "--";
  }
  if (!reading) {
    byId("reading-status").textContent = "Chưa có dữ liệu ESP32. Kiểm tra cấu hình firmware và log Serial.";
    return;
  }
  const lineState = { sent: "LINE đã chấp nhận tin", failed: "gửi LINE thất bại", pending: "đang gửi LINE" };
  byId("reading-status").textContent = [
    `Nhận lúc ${new Date(reading.received_at).toLocaleString("vi-VN")}`,
    `STM32 ${reading.data.online ? "online" : "offline"} tại thời điểm gửi`,
    lineState[reading.line_status] || reading.line_status,
    reading.line_error,
  ].filter(Boolean).join(" · ");
}

async function refreshReadings() {
  if (loading) return;
  loading = true;
  try {
    const data = await api("/api/telemetry/latest");
    const selected = deviceSelect.value;
    readings = data.readings;
    deviceSelect.replaceChildren();
    for (const reading of readings) {
      deviceSelect.add(new Option(reading.device_id, reading.device_id));
    }
    if (!readings.length) deviceSelect.add(new Option("Chưa có dữ liệu", ""));
    if (readings.some((item) => item.device_id === selected)) deviceSelect.value = selected;
    renderReading();
  } catch (error) {
    byId("reading-status").textContent = `Không làm mới được dữ liệu: ${error.message}`;
  } finally {
    loading = false;
  }
}

byId("connect").addEventListener("click", () => {
  clearInterval(polling);
  refreshReadings();
  fetchStatus();
  polling = setInterval(() => { if (!document.hidden) refreshReadings(); }, 10000);
});
byId("refresh").addEventListener("click", refreshReadings);
deviceSelect.addEventListener("change", renderReading);

byId("notify-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const button = byId("send");
  const result = byId("result");
  const message = byId("message").value.trim();
  if (!message) { result.textContent = "Nhập nội dung tin nhắn."; return; }
  button.disabled = true;
  result.textContent = "Đang gửi...";
  const broadcast = byId("target-type").value === "broadcast";
  const body = { message };
  const target = byId("target-id").value.trim();
  if (!broadcast && target) body.user_id = target;
  try {
    await api(broadcast ? "/api/line/broadcast" : "/api/line/push", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    result.textContent = "LINE đã chấp nhận tin nhắn. Kiểm tra cuộc trò chuyện với Official Account.";
  } catch (error) {
    result.textContent = `Gửi thất bại: ${error.message}`;
  } finally {
    button.disabled = false;
  }
});

byId("target-type").addEventListener("change", () => {
  const hidden = byId("target-type").value === "broadcast";
  byId("target-id").hidden = hidden;
  byId("target-label").hidden = hidden;
});
byId("refresh-targets").addEventListener("click", async () => {
  try {
    const data = await api("/api/line/targets");
    byId("targets").textContent = data.targets.length
      ? data.targets.map((item) => `${item.type}: ${item.target_id}\n  ${item.last_seen}`).join("\n")
      : "Chưa có ID. Cấu hình webhook rồi gửi tin cho bot hoặc trong nhóm đã thêm bot.";
  } catch (error) {
    byId("targets").textContent = error.message;
  }
});
fetchStatus();
