#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

// ====== 填你的 RX MAC ======
uint8_t RX_MAC[] = {0xEC, 0xE3, 0x34, 0x46, 0xE8, 0xC0};

WebServer server(80);
esp_now_peer_info_t peerInfo;

// ====== 封包 ======
// 注意：因為 TX 端修改了封包結構，你的 RX (接收端) 程式也必須更新成一模一樣的 ControlPacket！
#pragma pack(push, 1)
typedef struct {
  int16_t joy1x;
  int16_t joy1y;
  int16_t joy2x;
  int16_t joy2y;
  int16_t joy3x;
  int16_t joy3y;
  int16_t joy4x;
  int16_t joy4y;
  uint8_t buttons;   // bit0..bit5 (支援 6 顆按鈕)
  uint32_t seq;
  uint32_t ms;
} ControlPacket;
#pragma pack(pop)

static ControlPacket pkt;
static uint32_t seqCounter = 0;
static uint32_t lastWebUpdate = 0;

// ====== 送出回呼 ======
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "ESPNOW OK" : "ESPNOW FAIL");
}

// ====== Web UI ======
const char* HTML_PAGE = R"rawliteral(
<!doctype html>
<html lang="zh-TW">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no"/>
<title>ESP32 Web Remote (4 Joysticks + 6 Buttons)</title>
<style>
  /* touch-action: none 非常重要，可以防止在手機上滑動搖桿時觸發網頁捲動 */
  body { 
    font-family: system-ui, -apple-system, sans-serif; 
    margin: 0; padding: 20px; 
    background: #000000; color: #e8eefc; 
    touch-action: none; 
    display: flex; flex-direction: column; align-items: center;
  }
  .main-container {
    display: flex; gap: 40px; justify-content: center; width: 100%; max-width: 900px;
  }
  .left-col {
    display: flex; flex-direction: column; gap: 50px; align-items: center; justify-content: space-between;
  }
  .right-col {
    display: flex; flex-direction: column; gap: 20px; align-items: center;
  }
  
  /* 按鈕排版 */
  .btn-grid {
    display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px;
  }
  button {
    width: 90px; height: 50px; border: 0; border-radius: 25px; 
    font-size: 16px; font-weight: bold; color: black; cursor: pointer;
    box-shadow: 0 4px 10px rgba(0,0,0,0.5); transition: filter 0.1s;
  }
  .on { filter: brightness(1.3) drop-shadow(0 0 8px rgba(255,255,255,0.6)); outline: 2px solid white; }
  
  /* 按鈕顏色參考圖示 */
  .b1 { background: #5EE1DF; }
  .b2 { background: #0A4BB5; color: white; }
  .b3 { background: #FA252A; color: white; }
  .b4 { background: #C2FA60; }
  .b5 { background: #78E861; }
  .b6 { background: #F8D849; }

  /* 搖桿外觀 */
  .joystick-box {
    width: 160px; height: 160px; background: #8E8E8E; display: flex; align-items: center; justify-content: center;
  }
  canvas { touch-action: none; }
  
  .status-bar { margin-top: 20px; font-family: monospace; opacity: 0.8; }
</style>
</head>
<body>

<div class="main-container">
  <div class="left-col">
    <div class="btn-grid">
      <button class="b1" id="b0">BTN1</button>
      <button class="b2" id="b1">BTN2</button>
      <button class="b3" id="b2">BTN3</button>
      <button class="b4" id="b3">BTN4</button>
      <button class="b5" id="b4">BTN5</button>
      <button class="b6" id="b5">BTN6</button>
    </div>
    
    <div class="joystick-box">
      <canvas id="joy4" width="160" height="160"></canvas>
    </div>
  </div>

  <div class="right-col">
    <div class="joystick-box"><canvas id="joy1" width="160" height="160"></canvas></div>
    <div class="joystick-box"><canvas id="joy2" width="160" height="160"></canvas></div>
    <div class="joystick-box"><canvas id="joy3" width="160" height="160"></canvas></div>
  </div>
</div>

<div class="status-bar" id="status">Status: Idle</div>

<script>
const el = id => document.getElementById(id);
let state = {
  j1x:0, j1y:0, j2x:0, j2y:0, j3x:0, j3y:0, j4x:0, j4y:0,
  b: [0,0,0,0,0,0]
};

// ====== 按鈕控制 ======
function bindBtn(btnId, idx){
  const b = el(btnId);
  // 使用 pointer events 確保觸控跟滑鼠都能完美觸發
  const down = e => { e.preventDefault(); b.setPointerCapture(e.pointerId); state.b[idx] = 1; b.classList.add("on"); };
  const up   = e => { e.preventDefault(); b.releasePointerCapture(e.pointerId); state.b[idx] = 0; b.classList.remove("on"); };
  b.addEventListener("pointerdown", down);
  b.addEventListener("pointerup", up);
  b.addEventListener("pointercancel", up);
}
for(let i=0; i<6; i++) bindBtn("b"+i, i);

// ====== 虛擬搖桿 (Canvas) ======
function initJoystick(canvasId, statePrefix) {
  const canvas = el(canvasId);
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  const cx = w/2, cy = h/2;
  const radius = cx - 15; // 可移動半徑
  
  let stickX = cx, stickY = cy;
  let isDragging = false;

  function draw() {
    ctx.clearRect(0, 0, w, h);
    
    // 繪製外圈與底色
    ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.fillStyle = '#225864'; ctx.fill();
    ctx.lineWidth = 6; ctx.strokeStyle = '#9FE3F8'; ctx.stroke();

    // 繪製內圈裝飾
    ctx.beginPath(); ctx.arc(cx, cy, radius * 0.4, 0, Math.PI * 2);
    ctx.lineWidth = 2; ctx.strokeStyle = 'rgba(255,255,255,0.4)'; ctx.stroke();

    // 繪製搖桿頭 (Thumbstick)
    ctx.beginPath(); ctx.arc(stickX, stickY, 25, 0, Math.PI * 2);
    ctx.fillStyle = '#1A4B56'; ctx.fill();
    ctx.lineWidth = 3; ctx.strokeStyle = '#9FE3F8'; ctx.stroke();
  }

  function updatePos(evt) {
    const rect = canvas.getBoundingClientRect();
    const x = evt.clientX - rect.left;
    const y = evt.clientY - rect.top;
    
    const dx = x - cx;
    const dy = y - cy;
    const dist = Math.sqrt(dx*dx + dy*dy);

    // 限制在半徑內
    if(dist > radius) {
      stickX = cx + (dx / dist) * radius;
      stickY = cy + (dy / dist) * radius;
    } else {
      stickX = x; stickY = y;
    }

    // 映射到 -1000 ~ 1000 (Y軸向上為正)
    state[statePrefix+'x'] = Math.round(((stickX - cx) / radius) * 1000);
    state[statePrefix+'y'] = Math.round(((cy - stickY) / radius) * 1000);
    draw();
  }

  canvas.addEventListener("pointerdown", e => { e.preventDefault(); isDragging = true; canvas.setPointerCapture(e.pointerId); updatePos(e); });
  canvas.addEventListener("pointermove", e => { if(isDragging) updatePos(e); });
  
  const reset = e => { 
    isDragging = false; stickX = cx; stickY = cy; 
    state[statePrefix+'x'] = 0; state[statePrefix+'y'] = 0; 
    canvas.releasePointerCapture(e.pointerId); draw(); 
  };
  canvas.addEventListener("pointerup", reset);
  canvas.addEventListener("pointercancel", reset);

  draw(); // 初始化繪製
}

initJoystick('joy1', 'j1');
initJoystick('joy2', 'j2');
initJoystick('joy3', 'j3');
initJoystick('joy4', 'j4');

// ====== 傳送數據 ======
async function sendState(){
  const qs = new URLSearchParams({
    j1x: state.j1x, j1y: state.j1y,
    j2x: state.j2x, j2y: state.j2y,
    j3x: state.j3x, j3y: state.j3y,
    j4x: state.j4x, j4y: state.j4y,
    b0: state.b[0], b1: state.b[1], b2: state.b[2], 
    b3: state.b[3], b4: state.b[4], b5: state.b[5]
  }).toString();

  try {
    const r = await fetch("/state?" + qs, {cache:"no-store"});
    if(r.ok) el("status").textContent = "Status: Transmitting... (ESP-NOW)";
    else el("status").textContent = "Status: HTTP Error";
  } catch(e) {
    el("status").textContent = "Status: Disconnected";
  }
}

// 25Hz 連續送
setInterval(sendState, 40);
</script>
</body>
</html>
)rawliteral";

// ====== Web handlers ======
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

static int16_t argToI16(const String& s) {
  long v = s.toInt();
  if (v < -1000) v = -1000;
  if (v > 1000)  v = 1000;
  return (int16_t)v;
}

void handleState() {
  // 讀取搖桿 (4組)
  if (server.hasArg("j1x")) pkt.joy1x = argToI16(server.arg("j1x"));
  if (server.hasArg("j1y")) pkt.joy1y = argToI16(server.arg("j1y"));
  if (server.hasArg("j2x")) pkt.joy2x = argToI16(server.arg("j2x"));
  if (server.hasArg("j2y")) pkt.joy2y = argToI16(server.arg("j2y"));
  if (server.hasArg("j3x")) pkt.joy3x = argToI16(server.arg("j3x"));
  if (server.hasArg("j3y")) pkt.joy3y = argToI16(server.arg("j3y"));
  if (server.hasArg("j4x")) pkt.joy4x = argToI16(server.arg("j4x"));
  if (server.hasArg("j4y")) pkt.joy4y = argToI16(server.arg("j4y"));

  // 讀取按鈕 (6顆)
  uint8_t b = 0;
  if (server.arg("b0").toInt() == 1) b |= (1 << 0);
  if (server.arg("b1").toInt() == 1) b |= (1 << 1);
  if (server.arg("b2").toInt() == 1) b |= (1 << 2);
  if (server.arg("b3").toInt() == 1) b |= (1 << 3);
  if (server.arg("b4").toInt() == 1) b |= (1 << 4);
  if (server.arg("b5").toInt() == 1) b |= (1 << 5);
  pkt.buttons = b;

  pkt.seq = seqCounter++;
  pkt.ms  = millis();
  lastWebUpdate = pkt.ms;

  // 立刻透過 ESP-NOW 送到 RX
  esp_now_send(RX_MAC, (uint8_t*)&pkt, sizeof(pkt));

  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);

  // ===== SoftAP =====
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP("ESP32-Remote", "88888888");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("TX MAC: ");
  Serial.println(WiFi.macAddress());

  // ===== ESP-NOW init =====
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }
  esp_now_register_send_cb(OnDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, RX_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Peer add failed");
    while (true) delay(1000);
  }

  // ===== Web server routes =====
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.begin();

  Serial.println("Web server ready: http://192.168.4.1");
}

void loop() {
  server.handleClient();
}
