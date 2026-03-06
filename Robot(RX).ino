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
  if (len != (int)sizeof(ControlPacket)) return;
  
  memcpy((void*)&g_pkt, incomingData, sizeof(ControlPacket));
  g_newPkt = true;
  g_lastRecv = millis();

  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void setup() {
  Serial.begin(115200);
  delay(500); // 給予 Serial 穩定時間
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 初始化 WiFi 並列印 MAC
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  Serial.println("\n==============================");
  Serial.print("Device MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("==============================\n");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("RX ready.");
}

void loop() {
  static uint32_t lastPrint = 0;
  uint32_t now = millis();

  if (g_newPkt && now - lastPrint >= 50) {
    lastPrint = now;
    g_newPkt = false;

    ControlPacket p;
    memcpy(&p, (const void*)&g_pkt, sizeof(ControlPacket));

    Serial.printf("seq=%lu joy1(%d,%d) joy2(%d,%d) joy3(%d,%d) joy4(%d,%d) btn=0x%02X age=%lums\n",
                  (unsigned long)p.seq,
                  p.joy1x, p.joy1y,
                  p.joy2x, p.joy2y,
                  p.joy3x, p.joy3y,
                  p.joy4x, p.joy4y,
                  p.buttons,
                  (unsigned long)(now - g_lastRecv));
  }

  if (now - g_lastRecv > 500) {
    digitalWrite(LED_PIN, HIGH);
  }
}
