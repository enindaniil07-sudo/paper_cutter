/*
  ESP32-S3-Zero: дальномер GL-R01 и отправка миллиметров на ESP32-C3FH4.

  Площадки платы, USB-C сверху:
    RX  — зелёный провод, выход датчика
    TX  — жёлтый провод, вход датчика
    5V  — красный
    GND — чёрный

  По радио ESP-NOW, канал 1, адрес — всем на этом канале.
  Кадр 8 байт, порядок байтов младший вперёд:
    A5          признак
    01          версия
    seq         номер пакета, uint16
    mm          миллиметры, uint16; FFFF значит кадра от датчика ещё не было
    sum         сумма первых шести байт, uint16
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>

static constexpr uint8_t kChannel = 1;
static constexpr uint32_t kBaud = 115200;
static constexpr int kSensorRx = 44;  // площадка RX
static constexpr int kSensorTx = 43;  // площадка TX
static constexpr int kRgbPin = 21;
static constexpr uint16_t kNoReading = 0xFFFF;
// Датчик молчит дольше этого — шлём FFFF, чтобы C3/Black Pill отличали
// «радио живо, датчик отвалился» от «S3 выключен».
static constexpr uint32_t kSensorSilenceMs = 500;

static const uint8_t kSetCycle30[] = {0x01, 0x06, 0x02, 0x03, 0x00, 0x1E, 0xF8, 0x7A};

HardwareSerial Sensor(1);

uint8_t frame[4];
uint8_t frameN = 0;
uint32_t lastByteMs = 0;
uint32_t lastFrameMs = 0;
uint16_t lastMm = kNoReading;
bool frameFresh = false;
uint16_t seq = 0;

struct __attribute__((packed)) RangePacket {
  uint8_t magic;
  uint8_t version;
  uint16_t seq;
  uint16_t mm;
  uint16_t sum;
};

class BroadcastPeer : public ESP_NOW_Peer {
 public:
  BroadcastPeer() : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, kChannel, WIFI_IF_STA, nullptr) {}

  bool begin() {
    return ESP_NOW.begin() && add();
  }

  bool sendPacket(const uint8_t *data, size_t len) {
    return send(data, len) > 0;
  }
};

BroadcastPeer broadcastPeer;

uint16_t packetSum(const RangePacket &packet) {
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&packet);
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 6; i++) sum = (uint16_t)(sum + raw[i]);
  return sum;
}

void paint(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(kRgbPin, r, g, b);
}

void sendReading() {
  RangePacket packet;
  packet.magic = 0xA5;
  packet.version = 1;
  packet.seq = seq++;
  packet.mm = lastMm;
  packet.sum = packetSum(packet);

  bool ok = broadcastPeer.sendPacket(reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
  paint(ok ? 0 : 12, ok ? 12 : 0, 0);

  Serial.print(ok ? F("ушло ") : F("нет подтверждения "));
  Serial.print(packet.seq);
  Serial.print(F("  "));
  if (packet.mm == kNoReading) Serial.print(F("нет замера"));
  else {
    Serial.print(packet.mm);
    Serial.print(F(" мм"));
  }
  Serial.println();
}

void takeFrame() {
  uint8_t sum = (uint8_t)(frame[0] + frame[1] + frame[2]);
  if (sum == frame[3]) {
    lastMm = ((uint16_t)frame[1] << 8) | frame[2];
    lastFrameMs = millis();
    frameFresh = true;
  }
  frameN = 0;
}

void pollSensor() {
  while (Sensor.available() > 0) {
    uint8_t b = (uint8_t)Sensor.read();
    uint32_t now = millis();
    if (now - lastByteMs > 15) frameN = 0;
    lastByteMs = now;
    if (frameN == 0 && b != 0xFF) continue;
    frame[frameN++] = b;
    if (frameN >= 4) takeFrame();
  }
}

void setup() {
  Serial.begin(kBaud);
  delay(300);
  Sensor.begin(kBaud, SERIAL_8N1, kSensorRx, kSensorTx);
  Sensor.write(kSetCycle30, sizeof(kSetCycle30));
  delay(30);
  Sensor.write(kSetCycle30, sizeof(kSetCycle30));

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(kChannel);
  while (!WiFi.STA.started()) delay(50);

  Serial.println();
  Serial.println(F("S3-Zero отправляет замеры на C3"));
  Serial.print(F("MAC "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("канал "));
  Serial.println(kChannel);
  Serial.println(F("Датчик: зелёный на RX, жёлтый на TX, красный на 5V, чёрный на GND"));
  // Иначе USB-печать встаёт, когда монитор порта не открыт, и замеры уходят с паузой.
  Serial.setTxTimeoutMs(0);

  if (!broadcastPeer.begin()) {
    Serial.println(F("ESP-NOW не запустился"));
    paint(12, 0, 0);
    while (true) delay(1000);
  }
  paint(0, 0, 12);
}

void loop() {
  pollSensor();
  // Каждый кадр датчика (цикл 30 мс) уходит сразу, без паузы 100 мс.
  if (frameFresh) {
    frameFresh = false;
    sendReading();
    return;
  }
  // Датчик замолчал — раз в kSensorSilenceMs шлём «нет замера».
  uint32_t now = millis();
  if (now - lastFrameMs >= kSensorSilenceMs) {
    lastFrameMs = now;
    lastMm = kNoReading;
    sendReading();
  }
}
