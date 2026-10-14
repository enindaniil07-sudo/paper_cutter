/*
  Монитор дальномера GL-R01 на Arduino Nano 3.

  Сырой поток UART Automatic, 115200.
  Датчик сам шлёт кадр FF HH LL SUM каждые 30 мс.
  Миллиметры = HH * 256 + LL. Это значение реального времени, без окна.

  Один раз по Modbus ставится цикл 30 мс (регистр 0x0203):
    01 06 02 03 00 1E F8 7A
  Жёлтый RX в момент включения датчика держится низким, чтобы шёл сырой замер,
  а не обработанный.

  Проводка:
    Красный  VCC -> 5V
    Чёрный   GND -> GND
    Зелёный  TX  -> D8
    Жёлтый   RX  -> D9
*/

#include <AltSoftSerial.h>
#include <math.h>

static constexpr int kPinLed = 13;
static constexpr uint32_t kBaud = 115200;
static constexpr uint8_t kWindowMax = 50;
static constexpr uint8_t kWindowDefault = 30;
static constexpr uint16_t kMinMm = 20;
static constexpr uint16_t kMaxMm = 3000;

AltSoftSerial sensorPort;

int16_t samples[kWindowMax];
uint8_t windowN = kWindowDefault;
uint8_t filled = 0;
uint8_t head = 0;
uint8_t sinceReport = 0;
uint16_t rejected = 0;

bool refOn = false;
int16_t refMm = 0;
int16_t offsetMm = 0;

char cmd[40];
uint8_t cmdLen = 0;

uint8_t frame[7];
uint8_t frameN = 0;
uint32_t lastByteMs = 0;
uint32_t lastFrameMs = 0;
uint32_t lastAskMs = 0;
uint16_t byteCount = 0;
bool announcedWait = false;
bool askSent = false;

static const uint8_t kSetCycle30[] = {0x01, 0x06, 0x02, 0x03, 0x00, 0x1E, 0xF8, 0x7A};

struct Stats {
  float mean;
  float sd;
  int16_t minV;
  int16_t maxV;
  float median;
};

void setLed(bool on) {
  digitalWrite(kPinLed, on ? HIGH : LOW);
}

void clearWindow() {
  filled = 0;
  head = 0;
  sinceReport = 0;
  rejected = 0;
}

void printHelp() {
  Serial.println();
  Serial.println(F("Команды:"));
  Serial.println(F("  help          справка"));
  Serial.println(F("  ref 400       эталон с линейки, миллиметры"));
  Serial.println(F("  ref off       убрать эталон"));
  Serial.println(F("  cal           убрать постоянный сдвиг"));
  Serial.println(F("  uncal         вернуть сырые миллиметры"));
  Serial.println(F("  clear         очистить окно"));
  Serial.println(F("  win 30        размер окна, 10…50"));
  Serial.println();
  Serial.println(F("Систематическая ошибка — среднее минус эталон. СКО — дрожание."));
  Serial.println();
}

int16_t shownMm(int16_t raw) {
  return (int16_t)(raw - offsetMm);
}

void pushRaw(int16_t raw) {
  if (filled < windowN) {
    samples[filled++] = raw;
  } else {
    samples[head] = raw;
    head = (uint8_t)((head + 1) % windowN);
  }
  sinceReport++;
}

void computeStats(Stats *out) {
  out->mean = 0;
  out->sd = 0;
  out->minV = 0;
  out->maxV = 0;
  out->median = 0;
  if (filled == 0) return;

  int16_t sorted[kWindowMax];
  float sum = 0;
  float sum2 = 0;
  int16_t mn = shownMm(samples[0]);
  int16_t mx = mn;
  for (uint8_t i = 0; i < filled; i++) {
    int16_t v = shownMm(samples[i]);
    sorted[i] = v;
    sum += v;
    sum2 += (float)v * (float)v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  for (uint8_t i = 1; i < filled; i++) {
    int16_t v = sorted[i];
    int j = (int)i - 1;
    while (j >= 0 && sorted[j] > v) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = v;
  }
  float mean = sum / filled;
  float var = sum2 / filled - mean * mean;
  if (var < 0) var = 0;
  out->mean = mean;
  out->sd = sqrtf(var);
  out->minV = mn;
  out->maxV = mx;
  if ((filled & 1u) == 1u) out->median = (float)sorted[filled / 2];
  else out->median = 0.5f * (float)(sorted[filled / 2 - 1] + sorted[filled / 2]);
}

void printLive(uint16_t mm) {
  if (Serial.availableForWrite() < 16) return;
  setLed(mm != 0xFFFF);
  Serial.print(mm);
  Serial.println(F(" мм"));
}

void printStats() {
  Stats st;
  computeStats(&st);
  Serial.println();
  Serial.print(F("---- окно "));
  Serial.print(filled);
  Serial.print('/');
  Serial.print(windowN);
  Serial.println(F("  GL-R01 ----"));
  if (filled == 0) {
    Serial.println(F("Годных замеров ещё нет."));
    Serial.println();
    return;
  }
  Serial.print(F("среднее "));
  Serial.print(st.mean, 1);
  Serial.print(F(" мм   медиана "));
  Serial.print(st.median, 1);
  Serial.println(F(" мм"));
  Serial.print(F("СКО "));
  Serial.print(st.sd, 1);
  Serial.print(F(" мм   мин "));
  Serial.print(st.minV);
  Serial.print(F("   макс "));
  Serial.print(st.maxV);
  Serial.print(F("   пик-пик "));
  Serial.print(st.maxV - st.minV);
  Serial.println(F(" мм"));
  if (refOn) {
    float bias = st.mean - (float)refMm;
    Serial.print(F("эталон "));
    Serial.print(refMm);
    Serial.print(F(" мм   систематическая ошибка "));
    if (bias >= 0) Serial.print('+');
    Serial.print(bias, 1);
    Serial.println(F(" мм"));
  } else {
    Serial.println(F("Эталон не задан. Пример: ref 400"));
  }
  Serial.print(F("отбраковано: "));
  Serial.println(rejected);
  Serial.println();
}

void handleCmd(char *line) {
  while (*line == ' ') line++;
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\r')) line[--n] = 0;
  if (n == 0) return;

  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "clear") == 0) {
    clearWindow();
    Serial.println(F("Окно очищено."));
    return;
  }
  if (strcmp(line, "uncal") == 0) {
    offsetMm = 0;
    Serial.println(F("Сдвиг сброшен."));
    return;
  }
  if (strcmp(line, "cal") == 0) {
    if (!refOn) {
      Serial.println(F("Сначала эталон, например: ref 400"));
      return;
    }
    if (filled < windowN) {
      Serial.println(F("Подождите, пока окно заполнится."));
      return;
    }
    float sum = 0;
    for (uint8_t i = 0; i < filled; i++) sum += samples[i];
    float meanRaw = sum / filled;
    float delta = meanRaw - (float)refMm;
    offsetMm = (int16_t)(delta >= 0 ? delta + 0.5f : delta - 0.5f);
    Serial.print(F("Сдвиг "));
    Serial.print(offsetMm);
    Serial.println(F(" мм. Показ = raw - сдвиг."));
    return;
  }
  if (strcmp(line, "ref off") == 0) {
    refOn = false;
    Serial.println(F("Эталон выключен."));
    return;
  }
  if (strncmp(line, "ref", 3) == 0) {
    const char *arg = line + 3;
    while (*arg == ' ') arg++;
    long v = strtol(arg, nullptr, 10);
    if (v <= 0 || v > kMaxMm) {
      Serial.println(F("Эталон от 1 до 3000 мм. Пример: ref 400"));
      return;
    }
    refMm = (int16_t)v;
    refOn = true;
    Serial.print(F("Эталон "));
    Serial.print(refMm);
    Serial.println(F(" мм."));
    return;
  }
  if (strncmp(line, "win", 3) == 0) {
    const char *arg = line + 3;
    while (*arg == ' ') arg++;
    long v = strtol(arg, nullptr, 10);
    if (v < 10 || v > kWindowMax) {
      Serial.println(F("Окно от 10 до 50."));
      return;
    }
    windowN = (uint8_t)v;
    clearWindow();
    Serial.print(F("Окно "));
    Serial.println(windowN);
    return;
  }
  Serial.println(F("Неизвестная команда. help — список."));
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c == '\r') continue;
    if (c == '\n') {
      cmd[cmdLen] = 0;
      cmdLen = 0;
      handleCmd(cmd);
      return;
    }
    if (cmdLen < sizeof(cmd) - 1) cmd[cmdLen++] = c;
  }
}

uint16_t modbusCrc(const uint8_t *data, uint8_t n) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < n; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 1u) crc = (uint16_t)((crc >> 1) ^ 0xA001);
      else crc >>= 1;
    }
  }
  return crc;
}

void acceptFrame(uint16_t mm) {
  lastFrameMs = millis();
  announcedWait = false;
  if (mm <= kMaxMm) pushRaw((int16_t)mm);
  printLive(mm);
}

void setFastCycle() {
  sensorPort.write(kSetCycle30, sizeof(kSetCycle30));
}

void takeAutoFrame() {
  uint8_t sum = (uint8_t)(frame[0] + frame[1] + frame[2]);
  if (sum == frame[3]) {
    acceptFrame(((uint16_t)frame[1] << 8) | frame[2]);
    frameN = 0;
    return;
  }
  rejected++;
  uint8_t keep = 0;
  for (uint8_t i = 1; i < 4; i++) {
    if (frame[i] != 0xFF) continue;
    uint8_t n = (uint8_t)(4 - i);
    for (uint8_t j = 0; j < n; j++) frame[j] = frame[i + j];
    keep = n;
    break;
  }
  frameN = keep;
}

void takeModbusFrame() {
  if (frame[1] != 0x03 || frame[2] != 0x02) {
    rejected++;
    frameN = 0;
    return;
  }
  uint16_t crc = modbusCrc(frame, 5);
  if ((uint8_t)crc != frame[5] || (uint8_t)(crc >> 8) != frame[6]) {
    rejected++;
    frameN = 0;
    return;
  }
  acceptFrame(((uint16_t)frame[3] << 8) | frame[4]);
  frameN = 0;
}

void pollSensor() {
  uint8_t budget = 40;
  while (budget-- > 0 && sensorPort.available() > 0) {
    uint8_t b = (uint8_t)sensorPort.read();
    if (byteCount < 60000) byteCount++;
    uint32_t now = millis();
    if (now - lastByteMs > 15) frameN = 0;
    lastByteMs = now;

    if (frameN == 0 && b != 0x01 && b != 0xFF) continue;
    frame[frameN++] = b;
    if (frame[0] == 0xFF) {
      if (frameN >= 4) takeAutoFrame();
      continue;
    }
    if (frameN >= 7) takeModbusFrame();
  }

  uint32_t now = millis();
  if (now - lastFrameMs >= 1000 && now - lastByteMs >= 1000) {
    if (!announcedWait || now - lastFrameMs >= 2000) {
      announcedWait = true;
      lastFrameMs = now;
      setLed(false);
      Serial.print(F("Жду ответ GL-R01. Зелёный TX -> D8, жёлтый RX -> D9. Байт: "));
      Serial.println(byteCount);
      byteCount = 0;
    }
  }
}

void setup() {
  pinMode(kPinLed, OUTPUT);
  setLed(false);
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  delay(800);
  Serial.begin(kBaud);
  sensorPort.begin(kBaud);
  lastFrameMs = millis();
  setFastCycle();
  delay(30);
  setFastCycle();
  delay(30);
  setFastCycle();
  Serial.println();
  Serial.println(F("GL-R01 — сырой поток, цикл 30 мс"));
  Serial.println(F("Зелёный TX -> D8, жёлтый RX -> D9. Если поток редкий, выключите и включите питание датчика."));
  printHelp();
}

void loop() {
  pollSerial();
  pollSensor();
}
