#include <WiFi.h>
#include <esp_now.h>

#define LED_PIN 2   // 多數 ESP32 DevKit 板載 LED 

// ====== 封包結構 (必須與 TX 端完全一致) ======
#pragma pack(push, 1)
typedef struct {
  int16_t joy1x;     // -1000..1000
  int16_t joy1y;     // -1000..1000
  int16_t joy2x;     // -1000..1000
  int16_t joy2y;     // -1000..1000
  int16_t joy3x;     // -1000..1000
  int16_t joy3y;     // -1000..1000
  int16_t joy4x;     // -1000..1000
  int16_t joy4y;     // -1000..1000
  uint8_t buttons;   // bit0..bit5 (對應 6 顆按鈕)
  uint32_t seq;
  uint32_t ms;
} ControlPacket;
#pragma pack(pop)

volatile ControlPacket g_pkt;
volatile bool g_newPkt = false;
volatile uint32_t g_lastRecv = 0;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  // 檢查封包大小是否相符，不相符代表結構不同步，直接丟棄
  if (len != (int)sizeof(ControlPacket)) return;
  
  memcpy((void*)&g_pkt, incomingData, sizeof(ControlPacket));
  g_newPkt = true;
  g_lastRecv = millis();

  // 立即反應：收到封包就閃一下 LED（示波器感）
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.print("RX MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }
  
  // 註冊接收回呼函式
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("RX ready.");
}

void loop() {
  static uint32_t lastPrint = 0;
  uint32_t now = millis();

  // 每 50ms 印一次最新狀態
  if (g_newPkt && now - lastPrint >= 50) {
    lastPrint = now;
    g_newPkt = false;

    // 複製出來印，避免被中斷覆寫
    ControlPacket p;
    memcpy(&p, (const void*)&g_pkt, sizeof(ControlPacket));

    // 更新列印格式，加入 joy3 與 joy4
    Serial.printf("seq=%lu joy1(%d,%d) joy2(%d,%d) joy3(%d,%d) joy4(%d,%d) btn=0x%02X age=%lums\n",
                  (unsigned long)p.seq,
                  p.joy1x, p.joy1y,
                  p.joy2x, p.joy2y,
                  p.joy3x, p.joy3y,
                  p.joy4x, p.joy4y,
                  p.buttons,
                  (unsigned long)(now - g_lastRecv));
  }

  // 失聯提示：超過 500ms 沒收到封包，LED 長亮
  if (now - g_lastRecv > 500) {
    digitalWrite(LED_PIN, HIGH);
  }
}
