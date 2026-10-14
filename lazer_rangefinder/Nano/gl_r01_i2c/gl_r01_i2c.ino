/*
  Монитор дальномера GL-R01 по шине I2C на Arduino Nano 3.

  Тот же порядок, что в драйвере ESPHome:
    адрес 0x74
    регистр 0x00 — версия, 2 байта
    регистр 0x10, байт 0xB0 — запустить замер
    пауза 40 мс
    регистр 0x02 — расстояние в миллиметрах, старший байт первым
    0xFFFF — негодно

  Проводка, разъём MX1.25:
    Красный  VCC -> 5V
    Чёрный   GND -> GND
    Жёлтый   SDA -> A4
    Зелёный  SCL -> A5

  Монитор порта (USB): 115200.
*/

#include <Wire.h>
#include <math.h>

static constexpr int kPinLed = 13;
static constexpr uint32_t kBaud = 115200;
static constexpr uint8_t kAddrDefault = 0x74;
static constexpr uint8_t kRegVersion = 0x00;
static constexpr uint8_t kRegDistance = 0x02;
static constexpr uint8_t kRegTrigger = 0x10;
static constexpr uint8_t kCmdTrigger = 0xB0;
static constexpr uint16_t kReadDelayMs = 40;
static constexpr uint8_t kWindowMax = 50;
static constexpr uint8_t kWindowDefault = 30;
static constexpr uint16_t kMinMm = 20;
static constexpr uint16_t kMaxMm = 3000;

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

uint8_t sensorAddr = kAddrDefault;
bool sensorOk = false;
uint16_t version = 0;
bool waitingResult = false;
uint32_t triggerMs = 0;
uint32_t lastMissMs = 0;

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

void printLive(uint16_t mm, bool ok) {
  if (!ok) {
    setLed(false);
    Serial.print(F("-- "));
    Serial.print(mm);
    Serial.println(F(" мм  негодный замер"));
    return;
  }

  setLed(true);
  int16_t shown = shownMm((int16_t)mm);
  Stats st;
  computeStats(&st);

  Serial.print(shown);
  Serial.print(F(" мм"));
  if (offsetMm != 0) {
    Serial.print(F("  raw "));
    Serial.print(mm);
  }
  Serial.print(F("  OK  окно "));
  Serial.print(filled);
  Serial.print('/');
  Serial.print(windowN);
  if (filled > 0) {
    Serial.print(F("  средн "));
    Serial.print(st.mean, 1);
    Serial.print(F("  СКО "));
    Serial.print(st.sd, 1);
  }
  if (refOn) {
    float err = (float)shown - (float)refMm;
    Serial.print(F("  ошибка "));
    if (err >= 0) Serial.print('+');
    Serial.print(err, 1);
  }
  if (shown < (int16_t)kMinMm) Serial.print(F("  слепая зона"));
  Serial.println();
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensorAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readReg16(uint8_t reg, uint16_t *value) {
  Wire.beginTransmission(sensorAddr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(sensorAddr, (uint8_t)2) != 2) return false;
  uint8_t hi = (uint8_t)Wire.read();
  uint8_t lo = (uint8_t)Wire.read();
  *value = ((uint16_t)hi << 8) | lo;
  return true;
}

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void scanBus() {
  Serial.print(F("Шина I2C:"));
  uint8_t found = 0;
  uint8_t only = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (!probe(addr)) continue;
    found++;
    only = addr;
    Serial.print(' ');
    Serial.print(addr, HEX);
  }
  if (found == 0) Serial.print(F(" пусто"));
  Serial.println();
  if (found == 1) sensorAddr = only;
}

bool attachSensor() {
  if (!probe(sensorAddr) && sensorAddr != kAddrDefault) {
    sensorAddr = kAddrDefault;
  }
  if (!probe(sensorAddr)) {
    scanBus();
    if (!probe(sensorAddr)) return false;
  }
  uint16_t ver = 0;
  if (!readReg16(kRegVersion, &ver)) return false;
  version = ver;
  sensorOk = true;
  Serial.print(F("GL-R01 I2C, адрес 0x"));
  Serial.print(sensorAddr, HEX);
  Serial.print(F(", версия 0x"));
  if (version < 0x1000) Serial.print('0');
  if (version < 0x100) Serial.print('0');
  if (version < 0x10) Serial.print('0');
  Serial.println(version, HEX);
  return true;
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
    float delta = sum / filled - (float)refMm;
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

void acceptMm(uint16_t mm) {
  bool ok = mm != 0xFFFF && mm >= kMinMm && mm <= kMaxMm;
  if (ok) pushRaw((int16_t)mm);
  else rejected++;
  printLive(mm, ok);
  if (ok && sinceReport >= windowN) {
    sinceReport = 0;
    rejected = 0;
  }
}

void pollSensor() {
  uint32_t now = millis();
  if (!sensorOk) {
    if (now - lastMissMs < 2000) return;
    lastMissMs = now;
    setLed(false);
    if (!attachSensor()) {
      Serial.println(F("Нет GL-R01 на I2C. Жёлтый SDA -> A4, зелёный SCL -> A5, красный -> 5V, чёрный -> GND."));
    }
    return;
  }

  if (!waitingResult) {
    if (!writeReg(kRegTrigger, kCmdTrigger)) {
      sensorOk = false;
      waitingResult = false;
      return;
    }
    triggerMs = now;
    waitingResult = true;
    return;
  }

  if (now - triggerMs < kReadDelayMs) return;
  waitingResult = false;
  uint16_t mm = 0;
  if (!readReg16(kRegDistance, &mm)) {
    sensorOk = false;
    return;
  }
  acceptMm(mm);
}

void setup() {
  pinMode(kPinLed, OUTPUT);
  setLed(false);
  Serial.begin(kBaud);
  Wire.begin();
  Wire.setClock(100000);
  Serial.println();
  Serial.println(F("GL-R01 — лазерный дальномер, I2C"));
  Serial.println(F("Жёлтый SDA -> A4, зелёный SCL -> A5"));
  printHelp();
  attachSensor();
}

void loop() {
  pollSerial();
  pollSensor();
}
