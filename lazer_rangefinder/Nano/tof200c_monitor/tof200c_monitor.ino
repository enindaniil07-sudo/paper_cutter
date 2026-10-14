/*
  Монитор точности TOF200C на Arduino Nano 3 (ATmega328P).

  TOF200C — лазерный дальномер на чипе ST VL53L0X, I2C, адрес 0x29.
  Результат — расстояние в миллиметрах, штатный бюджет 33 мс.
  Паспортная дальность около 200 см по светлой мишени.
  Ближе ~40 мм — мёртвая зона, число там ненадёжно.

  Библиотека: VL53L0X by Pololu.
  Монитор порта: 115200.

  Проводка:
    TOF200C VIN  -> 3.3V   (пин 3.3V на Nano)
    TOF200C GND  -> GND
    TOF200C SDA  -> A4
    TOF200C SCL  -> A5
    TOF200C SHUT -> 3.3V
    INT          не подключать
*/

#include <Wire.h>
#include <VL53L0X.h>
#include <math.h>

static constexpr int kPinLed = 13;
static constexpr uint32_t kBaud = 115200;
static constexpr uint8_t kWindowMax = 50;
static constexpr uint8_t kWindowDefault = 30;
static constexpr uint8_t kModelId = 0xEE;

VL53L0X sensor;

int16_t samples[kWindowMax];
uint8_t windowN = kWindowDefault;
uint8_t filled = 0;
uint8_t head = 0;
uint8_t sinceReport = 0;
uint16_t rejected = 0;

bool sensorReady = false;
bool refOn = false;
int16_t refMm = 0;
int16_t offsetMm = 0;
uint8_t failStreak = 0;

char cmd[40];
uint8_t cmdLen = 0;

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

const char *statusText(uint8_t status) {
  switch (status) {
    case 0:  return "OK";
    case 1:  return "шумный замер";
    case 2:  return "слабое отражение";
    case 3:  return "у нижней границы";
    case 4:  return "дальше предела";
    case 5:  return "сбой чипа";
    case 7:  return "перекрытие импульса";
    case 12: return "нет цели";
    case 13: return "слишком близко";
    default: return "негодный замер";
  }
}

bool rangeOk(uint8_t status, uint16_t mm) {
  bool valid = status == 0 || status == 3 || status == 6 || status == 11;
  return valid && mm >= 20 && mm <= 2500;
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

bool startSensor() {
  sensorReady = false;
  clearWindow();

  Wire.begin();
  Wire.setClock(100000);

  sensor.setTimeout(500);
  uint8_t id = sensor.readReg(VL53L0X::IDENTIFICATION_MODEL_ID);
  if (sensor.last_status != 0 || id != kModelId) {
    Serial.println(F("TOF200C не отвечает (VL53L0X, адрес 0x29)."));
    Serial.print(F("  модель 0x"));
    Serial.print(id, HEX);
    Serial.print(F(", шина "));
    Serial.println(sensor.last_status);
    Serial.println(F("VIN->3.3V  GND->GND  SDA->A4  SCL->A5  SHUT->3.3V"));
    return false;
  }

  if (!sensor.init()) {
    Serial.println(F("Чип ответил, но настройка VL53L0X не прошла."));
    return false;
  }

  sensor.setMeasurementTimingBudget(33000);
  sensor.startContinuous(50);

  sensorReady = true;
  failStreak = 0;
  Serial.println(F("TOF200C готов, бюджет 33 мс"));
  return true;
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

bool readSample(uint16_t *mm, uint8_t *status, bool *timedOut) {
  *timedOut = false;
  *mm = 0;
  *status = 255;

  uint16_t range = sensor.readRangeContinuousMillimeters();
  if (sensor.timeoutOccurred()) {
    *timedOut = true;
    return false;
  }

  uint8_t rawStatus = sensor.readReg(VL53L0X::RESULT_RANGE_STATUS);
  if (sensor.last_status != 0) {
    *timedOut = true;
    return false;
  }
  *status = (rawStatus & 0x78) >> 3;
  *mm = range;
  return rangeOk(*status, *mm);
}

void printLive(uint16_t mm, uint8_t status, bool ok) {
  if (!ok) {
    setLed(false);
    Serial.print(F("-- "));
    Serial.print(mm);
    Serial.print(F(" мм  "));
    Serial.print(statusText(status));
    Serial.print(F(" (код "));
    Serial.print(status);
    Serial.println(')');
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
  if (shown < 40) Serial.print(F("  мёртвая зона"));
  Serial.println();
}

void printStats() {
  Stats st;
  computeStats(&st);
  Serial.println();
  Serial.print(F("---- окно "));
  Serial.print(filled);
  Serial.print('/');
  Serial.print(windowN);
  Serial.println(F("  режим 200 см ----"));
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
    if (v <= 0 || v > 2500) {
      Serial.println(F("Эталон от 1 до 2500 мм. Пример: ref 400"));
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

void setup() {
  pinMode(kPinLed, OUTPUT);
  setLed(false);
  Serial.begin(kBaud);
  Serial.println();
  Serial.println(F("TOF200C / VL53L0X — дальномер до ~200 см"));
  Serial.println(F("Arduino Nano 3, SDA=A4 SCL=A5, питание 3.3V"));
  printHelp();
  if (!startSensor()) {
    Serial.println(F("Жду датчик, повтор каждую секунду."));
  }
}

void loop() {
  pollSerial();

  if (!sensorReady) {
    setLed(((millis() / 150) & 1) != 0);
    static uint32_t tRetry = 0;
    if (millis() - tRetry >= 1000) {
      tRetry = millis();
      startSensor();
    }
    return;
  }

  uint16_t mm = 0;
  uint8_t status = 0;
  bool timedOut = false;
  bool ok = readSample(&mm, &status, &timedOut);
  if (timedOut) {
    failStreak++;
    setLed(false);
    Serial.println(F("Таймаут чтения."));
    if (failStreak >= 3) startSensor();
    return;
  }
  failStreak = 0;
  if (ok) pushRaw((int16_t)mm);
  else rejected++;
  printLive(mm, status, ok);
  if (ok && sinceReport >= windowN) {
    printStats();
    sinceReport = 0;
    rejected = 0;
  }
}
