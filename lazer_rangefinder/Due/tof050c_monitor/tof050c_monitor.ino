/*
  Монитор точности TOF050C на Arduino Due.

  TOF050C — лазерный дальномер на чипе ST VL6180X, I2C, адрес 0x29.
  Внутри чипа результат — один байт. Чтобы достать до ~50 см, включён
  масштаб 3: один шаг = 3 мм, верхний предел регистра около 765 мм.
  На тёмной мишени реальная дальность часто меньше паспортных 50 см.

  Плата: Arduino Due (Programming Port), кабель в разъём ближе к питанию.
  Библиотека: VL6180X by Pololu.
  Монитор порта: 115200.

  Проводка:
    TOF050C VIN  -> 3.3V
    TOF050C GND  -> GND
    TOF050C SDA  -> SDA  (пин 20, не SDA1)
    TOF050C SCL  -> SCL  (пин 21, не SCL1)
    TOF050C SHUT -> пин 2
    INT          не подключать
*/

#include <Wire.h>
#include <VL6180X.h>
#include <math.h>

static constexpr int kPinLed = 13;
static constexpr int kPinShut = 2;
static constexpr uint32_t kBaud = 115200;
static constexpr uint16_t kWindowMax = 200;
static constexpr uint16_t kWindowDefault = 50;
static constexpr uint8_t kModelId = 0xB4;

VL6180X sensor;

int32_t samples[kWindowMax];
uint16_t windowN = kWindowDefault;
uint16_t filled = 0;
uint16_t head = 0;
uint16_t sinceReport = 0;
uint16_t rejected = 0;

bool sensorReady = false;
bool refOn = false;
int32_t refMm = 0;
int32_t offsetMm = 0;
uint8_t failStreak = 0;

char cmd[48];
uint8_t cmdLen = 0;

struct Stats {
  float mean;
  float sd;
  int32_t minV;
  int32_t maxV;
  float median;
};

void setLed(bool on) {
  digitalWrite(kPinLed, on ? HIGH : LOW);
}

void enableI2CPullups() {
  PIOB->PIO_PUER = PIO_PB12 | PIO_PB13;
}

void clearWindow() {
  filled = 0;
  head = 0;
  sinceReport = 0;
  rejected = 0;
}

const char *statusText(uint8_t status) {
  switch (status) {
    case VL6180X_ERROR_NONE:       return "OK";
    case VL6180X_ERROR_ECEFAIL:    return "слабое отражение";
    case VL6180X_ERROR_NOCONVERGE: return "слишком далеко";
    case VL6180X_ERROR_SNR:        return "мешает засветка";
    case VL6180X_ERROR_RAWUFLOW:
    case VL6180X_ERROR_RANGEUFLOW: return "слишком близко";
    case VL6180X_ERROR_RAWOFLOW:
    case VL6180X_ERROR_RANGEOFLOW: return "дальше предела";
    default:                       return "негодный замер";
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("Команды:"));
  Serial.println(F("  help          справка"));
  Serial.println(F("  ref 200       эталон с линейки, миллиметры"));
  Serial.println(F("  ref off       убрать эталон"));
  Serial.println(F("  cal           убрать постоянный сдвиг"));
  Serial.println(F("  uncal         вернуть сырые миллиметры"));
  Serial.println(F("  clear         очистить окно"));
  Serial.println(F("  win 50        размер окна, 10…200"));
  Serial.println(F("  scale 1       шаг 1 мм, надёжно примерно до 20 см"));
  Serial.println(F("  scale 2       шаг 2 мм"));
  Serial.println(F("  scale 3       шаг 3 мм, потолок около 50–75 см"));
  Serial.println();
  Serial.println(F("Масштаб 3 включён сразу: так модуль на 50 см дотягивает дальше."));
  Serial.println(F("Систематическая ошибка — среднее минус эталон. СКО — дрожание."));
  Serial.println();
}

bool startSensor(uint8_t scaling) {
  sensorReady = false;
  clearWindow();

  pinMode(kPinShut, OUTPUT);
  digitalWrite(kPinShut, LOW);
  delay(10);
  digitalWrite(kPinShut, HIGH);
  delay(50);

  Wire.begin();
  Wire.setClock(100000);
  enableI2CPullups();

  sensor.setTimeout(500);
  uint8_t id = sensor.readReg(VL6180X::IDENTIFICATION__MODEL_ID);
  if (sensor.last_status != 0 || id != kModelId) {
    Serial.println(F("TOF050C не отвечает (VL6180X, адрес 0x29)."));
    Serial.print(F("  модель 0x"));
    Serial.print(id, HEX);
    Serial.print(F(", шина "));
    Serial.println(sensor.last_status);
    Serial.println(F("VIN->3.3V  GND->GND  SDA->пин 20  SCL->пин 21  SHUT->пин 2"));
    return false;
  }

  sensor.init();
  sensor.configureDefault();
  if (scaling < 1) scaling = 1;
  if (scaling > 3) scaling = 3;
  sensor.setScaling(scaling);
  // Регистр в миллисекундах, рабочий потолок чипа — 63. Выше он обрезает время и срывает замер.
  sensor.writeReg(VL6180X::SYSRANGE__MAX_CONVERGENCE_TIME, 63);

  sensorReady = true;
  failStreak = 0;
  // Первый пуск уходит на калибровку VHV и часто без годной дистанции.
  uint16_t discardMm = 0;
  uint8_t discardStatus = 0;
  bool discardTimeout = false;
  readSample(&discardMm, &discardStatus, &discardTimeout);
  Serial.print(F("TOF050C готов, масштаб "));
  Serial.print(sensor.getScaling());
  Serial.print(F(" (шаг "));
  Serial.print(sensor.getScaling());
  Serial.println(F(" мм)"));
  return true;
}

int32_t shownMm(int32_t raw) {
  return raw - offsetMm;
}

void pushRaw(int32_t raw) {
  if (filled < windowN) {
    samples[filled++] = raw;
  } else {
    samples[head] = raw;
    head = (uint16_t)((head + 1) % windowN);
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

  int32_t sorted[kWindowMax];
  double sum = 0;
  double sum2 = 0;
  int32_t mn = shownMm(samples[0]);
  int32_t mx = mn;
  for (uint16_t i = 0; i < filled; i++) {
    int32_t v = shownMm(samples[i]);
    sorted[i] = v;
    sum += v;
    sum2 += (double)v * (double)v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  for (uint16_t i = 1; i < filled; i++) {
    int32_t v = sorted[i];
    int j = (int)i - 1;
    while (j >= 0 && sorted[j] > v) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = v;
  }
  float mean = (float)(sum / filled);
  float var = (float)(sum2 / filled) - mean * mean;
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
  sensor.writeReg(VL6180X::SYSRANGE__START, 0x01);
  if (sensor.last_status != 0) {
    *timedOut = true;
    return false;
  }

  // Пока чип считает, шину не трогаем: опрос I2C на Due срывает замер.
  delay(70);
  uint8_t irq = 0;
  for (uint8_t n = 0; n < 4; n++) {
    irq = sensor.readReg(VL6180X::RESULT__INTERRUPT_STATUS_GPIO);
    if (sensor.last_status != 0) {
      *timedOut = true;
      return false;
    }
    if ((irq & 0x07) == 0x04) break;
    delay(20);
  }
  if ((irq & 0x07) != 0x04) {
    *timedOut = true;
    return false;
  }

  uint8_t raw = sensor.readReg(VL6180X::RESULT__RANGE_VAL);
  *status = sensor.readReg(VL6180X::RESULT__RANGE_STATUS) >> 4;
  sensor.writeReg(VL6180X::SYSTEM__INTERRUPT_CLEAR, 0x07);
  if (sensor.last_status != 0) {
    *timedOut = true;
    return false;
  }
  *mm = (uint16_t)raw * sensor.getScaling();
  return *status == VL6180X_ERROR_NONE;
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
  int32_t shown = shownMm((int32_t)mm);
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
  if (shown < 20) Serial.print(F("  мёртвая зона"));
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
  Serial.print(F("  масштаб "));
  Serial.print(sensor.getScaling());
  Serial.println(F(" ----"));
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
    Serial.println(F("Эталон не задан. Пример: ref 150"));
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
      Serial.println(F("Сначала эталон, например: ref 150"));
      return;
    }
    if (filled < windowN) {
      Serial.println(F("Подождите, пока окно заполнится."));
      return;
    }
    double sum = 0;
    for (uint16_t i = 0; i < filled; i++) sum += samples[i];
    float meanRaw = (float)(sum / filled);
    float delta = meanRaw - (float)refMm;
    offsetMm = (int32_t)(delta >= 0 ? delta + 0.5f : delta - 0.5f);
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
    if (v <= 0 || v > 1000) {
      Serial.println(F("Эталон от 1 до 1000 мм. Пример: ref 200"));
      return;
    }
    refMm = (int32_t)v;
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
      Serial.println(F("Окно от 10 до 200."));
      return;
    }
    windowN = (uint16_t)v;
    clearWindow();
    Serial.print(F("Окно "));
    Serial.println(windowN);
    return;
  }
  if (strncmp(line, "scale", 5) == 0) {
    const char *arg = line + 5;
    while (*arg == ' ') arg++;
    long v = strtol(arg, nullptr, 10);
    if (v < 1 || v > 3) {
      Serial.println(F("Масштаб 1, 2 или 3."));
      return;
    }
    startSensor((uint8_t)v);
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
  Serial.println(F("TOF050C / VL6180X — дальномер до ~50 см"));
  Serial.println(F("Arduino Due, SDA=20 SCL=21, питание 3.3V"));
  printHelp();
  if (!startSensor(3)) {
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
      startSensor(3);
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
    if (failStreak >= 3) startSensor(sensor.getScaling());
    return;
  }
  failStreak = 0;
  if (ok) pushRaw((int32_t)mm);
  else rejected++;
  printLive(mm, status, ok);
  if (ok && sinceReport >= windowN) {
    printStats();
    sinceReport = 0;
    rejected = 0;
  }
}
