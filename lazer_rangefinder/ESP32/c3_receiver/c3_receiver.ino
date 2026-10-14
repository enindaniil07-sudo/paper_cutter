/*
  WeAct ESP32-C3FH4: приём миллиметров от ESP32-S3-Zero.

  Светодиод платы сидит на контакте 8 и зажигается низким уровнем.
  Лампочка питания у USB от программы не зависит: она горит, пока есть 5 В.

  Радио ESP-NOW, канал 1. Кадр тот же, что шлёт S3:
    A5 01  seq  mm  sum
  seq и mm — uint16, младший байт первый.
  FFFF в миллиметрах значит, что у датчика ещё нет кадра.

  На контакт 21 (TX) уходит число и перевод строки, 115200.
  Контакт 20 (RX) слушает контроллер.

  К Black Pill (CLEAN_TABLO_FW): GPIO21 → PA10 (USART1_RX), GPIO20 → PA9,
  GND → GND, 5V → 5V. Прошивка парсит строки в range_link.cpp.
*/

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>

static constexpr uint8_t kChannel = 1;
static constexpr uint32_t kBaud = 115200;
static constexpr int kLedPin = 8;
static constexpr uint16_t kNoReading = 0xFFFF;
static constexpr uint32_t kSilenceMs = 1000;

static const uint8_t kS3Mac[6] = {0x90, 0xDA, 0x72, 0x49, 0x8C, 0x64};

struct __attribute__((packed)) RangePacket {
  uint8_t magic;
  uint8_t version;
  uint16_t seq;
  uint16_t mm;
  uint16_t sum;
};

volatile uint16_t lastSeq = 0;
volatile uint16_t lastMm = kNoReading;
volatile uint32_t lastRxMs = 0;
volatile bool fresh = false;
bool silenceNoted = false;

uint16_t packetSum(const uint8_t *raw) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 6; i++) sum = (uint16_t)(sum + raw[i]);
  return sum;
}

void led(bool on) {
  digitalWrite(kLedPin, on ? LOW : HIGH);
}

void accept(const uint8_t *data, size_t len) {
  if (len < sizeof(RangePacket)) return;
  RangePacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != 0xA5 || packet.version != 1) return;
  if (packetSum(data) != packet.sum) return;
  lastSeq = packet.seq;
  lastMm = packet.mm;
  lastRxMs = millis();
  fresh = true;
}

class SenderPeer : public ESP_NOW_Peer {
 public:
  explicit SenderPeer(const uint8_t *mac) : ESP_NOW_Peer(mac, kChannel, WIFI_IF_STA, nullptr) {}

  bool begin() {
    return add();
  }

  void onReceive(const uint8_t *data, size_t len, bool broadcast) override {
    (void)broadcast;
    accept(data, len);
  }
};

SenderPeer s3Peer(kS3Mac);

// Кадры принимаем только от известного S3 (kS3Mac → s3Peer.onReceive).
// Чужой передатчик на канале не должен управлять тормозом: его только
// отмечаем в логе, пир под него не создаём (иначе утечка и подмена данных).
void registerUnknown(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  (void)data;
  (void)len;
  (void)arg;
  static uint32_t lastNoteMs = 0;
  uint32_t now = millis();
  if (now - lastNoteMs < 5000) return;
  lastNoteMs = now;
  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", info->src_addr[0], info->src_addr[1],
           info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5]);
  Serial.print(F("чужой передатчик, игнорирую: "));
  Serial.println(mac);
}

void writeToController(uint16_t mm) {
  if (mm == kNoReading) Serial0.println(F("NA"));
  else Serial0.println(mm);
}

void setup() {
  pinMode(kLedPin, OUTPUT);
  led(true);

  Serial.begin(kBaud);
  Serial0.begin(kBaud, SERIAL_8N1, 20, 21);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(kChannel);
  while (!WiFi.STA.started()) delay(50);

  Serial.println();
  Serial.println(F("C3 принимает замеры от S3"));
  Serial.print(F("MAC "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("канал "));
  Serial.println(kChannel);

  if (!ESP_NOW.begin() || !s3Peer.begin()) {
    Serial.println(F("ESP-NOW не запустился"));
    while (true) {
      led(true);
      delay(100);
      led(false);
      delay(100);
    }
  }
  ESP_NOW.onNewPeer(registerUnknown, nullptr);
  led(true);
  Serial.println(F("слушаю"));
  // Иначе USB-печать встаёт, когда монитор порта не открыт, и UART к Black Pill молчит.
  Serial.setTxTimeoutMs(0);
}

void loop() {
  if (fresh) {
    fresh = false;
    silenceNoted = false;
    uint16_t seq = lastSeq;
    uint16_t mm = lastMm;
    led(true);
    writeToController(mm);
    Serial.print(F("принято "));
    Serial.print(seq);
    Serial.print(F("  "));
    if (mm == kNoReading) Serial.println(F("нет замера"));
    else {
      Serial.print(mm);
      Serial.println(F(" мм"));
    }
  }

  uint32_t now = millis();
  if (lastRxMs == 0) {
    led((now / 500) % 2 == 0);
  } else if (now - lastRxMs >= kSilenceMs) {
    led(false);
    if (!silenceNoted) {
      silenceNoted = true;
      Serial.println(F("пакетов нет"));
    }
  }
}
