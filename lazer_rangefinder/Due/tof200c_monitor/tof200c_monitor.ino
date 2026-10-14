/*
  Монитор точности TOF200C на Arduino Due.

  TOF200C — лазерный дальномер на чипе ST VL53L0X, I2C, адрес 0x29.
  Результат — 16-битное расстояние в миллиметрах, штатный бюджет 33 мс.
  Ближе ~40 мм — мёртвая зона, число там ненадёжно.

  Плата: Arduino Due (Programming Port), кабель в разъём ближе к питанию.
  Библиотека: VL53L0X by Pololu.
  Обмен программный: Due не умеет ждать, пока чип держит SCL.
  Монитор порта: 115200.

  Проводка:
    TOF200C VIN  -> 3.3V
    TOF200C GND  -> GND
    TOF200C SDA  -> SDA  (пин 20, не SDA1)
    TOF200C SCL  -> SCL  (пин 21, не SCL1)
    TOF200C SHUT -> 3.3V
    INT          не подключать
*/

#include <Wire.h>
#include <VL53L0X.h>
#include <math.h>

static constexpr int kPinLed = 13;
static constexpr uint32_t kBaud = 115200;
static constexpr uint16_t kWindowMax = 200;
static constexpr uint16_t kWindowDefault = 50;
static constexpr uint8_t kModelId = 0xEE;
static constexpr uint32_t kBudgetUs = 100000;
static constexpr uint32_t kStretchUs = 1000000;
uint8_t bbFail = 0;
uint32_t bbFailUs = 0;
static constexpr uint32_t kSdaMask = PIO_PB12;
static constexpr uint32_t kSclMask = PIO_PB13;

VL53L0X sensor;

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
uint8_t stopVariable = 0;
bool rangeWithPreamble = false;
bool useHardwareI2c = false;

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

void lineLow(uint32_t mask) {
  PIOB->PIO_CODR = mask;
  PIOB->PIO_OER = mask;
}

void lineRelease(uint32_t mask) {
  PIOB->PIO_ODR = mask;
}

bool bbWaitScl() {
  lineRelease(kSclMask);
  uint32_t start = micros();
  // Чип прижимает SCL не мгновенно. Короткое окно, чтобы это не пропустить.
  while ((uint32_t)(micros() - start) < 5) {
    if ((PIOB->PIO_PDSR & kSclMask) == 0) break;
  }
  while ((PIOB->PIO_PDSR & kSclMask) == 0) {
    if ((uint32_t)(micros() - start) > kStretchUs) {
      bbFail = 1;
      bbFailUs = (uint32_t)(micros() - start);
      return false;
    }
  }
  return true;
}

void bbHalf() { delayMicroseconds(5); }

void bbStart() {
  lineRelease(kSdaMask);
  bbWaitScl();
  bbHalf();
  lineLow(kSdaMask);
  bbHalf();
  lineLow(kSclMask);
}

void bbStop() {
  lineLow(kSdaMask);
  bbHalf();
  bbWaitScl();
  bbHalf();
  lineRelease(kSdaMask);
  bbHalf();
}

bool bbWriteBit(bool one) {
  if (one) lineRelease(kSdaMask);
  else lineLow(kSdaMask);
  bbHalf();
  if (!bbWaitScl()) return false;
  bbHalf();
  lineLow(kSclMask);
  return true;
}

bool bbReadBit(bool *ok) {
  lineRelease(kSdaMask);
  bbHalf();
  if (!bbWaitScl()) {
    *ok = false;
    return true;
  }
  bbHalf();
  bool bit = (PIOB->PIO_PDSR & kSdaMask) != 0;
  lineLow(kSclMask);
  return bit;
}

bool bbWriteByte(uint8_t value) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    if (!bbWriteBit((value >> bit) & 1)) return false;
  }
  bool ok = true;
  bool nack = bbReadBit(&ok);
  if (ok && nack) {
    bbFail = 2;
    bbFailUs = 0;
  }
  return ok && !nack;
}

uint8_t bbReadByte(bool ack, bool *ok) {
  uint8_t value = 0;
  for (int8_t bit = 7; bit >= 0; bit--) {
    if (bbReadBit(ok)) value |= (uint8_t)(1u << bit);
    if (!*ok) return 0xFF;
  }
  if (!bbWriteBit(!ack)) *ok = false;
  return value;
}

uint8_t vl53Write(uint8_t addr, const uint8_t *data, uint8_t len) {
  bbStart();
  if (!bbWriteByte((uint8_t)(addr << 1))) {
    bbStop();
    return 2;
  }
  for (uint8_t i = 0; i < len; i++) {
    if (!bbWriteByte(data[i])) {
      bbStop();
      return 2;
    }
  }
  bbStop();
  return 0;
}

uint8_t vl53Read(uint8_t addr, uint8_t *data, uint8_t len) {
  bbStart();
  if (!bbWriteByte((uint8_t)((addr << 1) | 1))) {
    bbStop();
    return 2;
  }
  bool ok = true;
  for (uint8_t i = 0; i < len; i++) data[i] = bbReadByte(i + 1 < len, &ok);
  bbStop();
  return ok ? 0 : 2;
}

void beginHardI2C() {
  PIOB->PIO_PDR = kSdaMask | kSclMask;
  PIOB->PIO_ABSR &= ~(kSdaMask | kSclMask);
  Wire.begin();
  Wire.setClock(100000);
  sensor.setXfer(nullptr, nullptr);
}

void beginSoftI2C() {
  Wire.end();
  PMC->PMC_PCER0 = 1u << ID_PIOB;
  PIOB->PIO_PER = kSdaMask | kSclMask;
  PIOB->PIO_PUER = kSdaMask | kSclMask;
  lineRelease(kSdaMask | kSclMask);
  delayMicroseconds(50);
  if ((PIOB->PIO_PDSR & (kSdaMask | kSclMask)) != (kSdaMask | kSclMask)) {
    Serial.println(F("Линии SDA/SCL сидят в нуле."));
  }
}

void printBusScan() {
  Serial.print(F("  адреса:"));
  bool any = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    bbStart();
    bool ack = bbWriteByte((uint8_t)(addr << 1));
    bbStop();
    if (ack) {
      any = true;
      Serial.print(F(" 0x"));
      Serial.print(addr, HEX);
    }
  }
  if (!any) Serial.print(F(" пусто"));
  Serial.println();
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
  return valid && mm >= 20 && mm <= 4000;
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
  Serial.println(F("  win 50        размер окна, 10…200"));
  Serial.println();
  Serial.println(F("Систематическая ошибка — среднее минус эталон. СКО — дрожание."));
  Serial.println();
}

void printChipRegs(const __FlashStringHelper *tag) {
  uint8_t model = sensor.readReg(0xC0);
  uint8_t revA = sensor.readReg(0xC1);
  uint8_t revB = sensor.readReg(0xC2);
  uint8_t sequence = sensor.readReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG);
  uint8_t start = sensor.readReg(VL53L0X::SYSRANGE_START);
  uint8_t hv = sensor.readReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV);
  Serial.print(tag);
  Serial.print(F(" модель=0x"));
  Serial.print(model, HEX);
  Serial.print(F(" пит=0x"));
  Serial.print(hv, HEX);
  Serial.print(F(" C1=0x"));
  Serial.print(revA, HEX);
  Serial.print(F(" C2=0x"));
  Serial.print(revB, HEX);
  Serial.print(F(" цикл=0x"));
  Serial.print(sequence, HEX);
  Serial.print(F(" пуск=0x"));
  Serial.println(start, HEX);
}

void forcePublicPage() {
  sensor.writeReg(0x80, 0x00);
  sensor.writeReg(0xFF, 0x00);
}

void loadStopVariable() {
  sensor.writeReg(0x80, 0x01);
  sensor.writeReg(0xFF, 0x01);
  sensor.writeReg(0x00, 0x00);
  stopVariable = sensor.readReg(0x91);
  sensor.writeReg(0x00, 0x01);
  sensor.writeReg(0xFF, 0x00);
  sensor.writeReg(0x80, 0x00);
}

void waitSensorPower() {
  // SHUT сидит на 3.3 В постоянно, отдельного сброса с пина нет.
  delay(100);
}

bool startSensor() {
  sensorReady = false;
  clearWindow();

  waitSensorPower();
  beginSoftI2C();
  sensor.setXfer(vl53Write, vl53Read);
  sensor.setTimeout(1000);
  uint8_t id = sensor.readReg(VL53L0X::IDENTIFICATION_MODEL_ID);
  if (sensor.last_status != 0 || id != kModelId) {
    Serial.println(F("TOF200C не отвечает (VL53L0X, адрес 0x29)."));
    Serial.print(F("  модель 0x"));
    Serial.print(id, HEX);
    Serial.print(F(", шина "));
    Serial.println(sensor.last_status);
    printBusScan();
    Serial.println(F("VIN->3.3V  GND->GND  SDA->пин 20  SCL->пин 21  SHUT->3.3V"));
    return false;
  }

  // Уровень линий 3.3 В. Без этого бита чип ждёт 1.8 В на входах.
  uint8_t hv = sensor.readReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV);
  sensor.writeReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, hv | 0x01);

  uint16_t mm = 0;
  uint8_t status = 255;
  Serial.println(F("Настройка по программным линиям."));
  bool inited = sensor.init();
  if (!inited) Serial.println(F("Калибровка не завершилась."));
  forcePublicPage();
  sensor.writeReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, 0x01);
  sensor.writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0xE8);
  printChipRegs(F("Сразу после записи цикла."));
  sensor.setMeasurementTimingBudget(33000);
  uint32_t budgetUs = sensor.getMeasurementTimingBudget();

  // Служебная запись перед пуском уводит чип со страницы результата.
  // Здесь пуск один: только бит в регистре 0x00, без смены страницы.
  Serial.println(F("Пуск без смены страницы."));
  bool fullOk = measureOnce(false, &mm, &status) && rangeOk(status, mm);

  if (!fullOk) {
    Serial.println(F("Повтор: аппаратные линии Due, пуск без смены страницы."));
    beginHardI2C();
    useHardwareI2c = true;
    uint8_t id2 = sensor.readReg(VL53L0X::IDENTIFICATION_MODEL_ID);
    if (sensor.last_status != 0 || id2 != kModelId) {
      Serial.print(F("  аппаратный обмен не видит чип, модель 0x"));
      Serial.println(id2, HEX);
      beginSoftI2C();
      sensor.setXfer(vl53Write, vl53Read);
      useHardwareI2c = false;
    } else {
      inited = sensor.init();
      if (!inited) Serial.println(F("Калибровка не завершилась."));
      forcePublicPage();
      sensor.writeReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, 0x01);
      sensor.writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0xE8);
      printChipRegs(F("Аппаратно, сразу после записи цикла."));
      sensor.setMeasurementTimingBudget(33000);
      budgetUs = sensor.getMeasurementTimingBudget();
      fullOk = measureOnce(false, &mm, &status) && rangeOk(status, mm);
      if (!fullOk) {
        beginSoftI2C();
        sensor.setXfer(vl53Write, vl53Read);
        useHardwareI2c = false;
      }
    }
  }

  sensorReady = true;
  failStreak = 0;
  Serial.print(F("TOF200C готов, бюджет "));
  Serial.print(budgetUs / 1000);
  Serial.print(F(" мс, stop=0x"));
  Serial.print(stopVariable, HEX);
  Serial.println(rangeWithPreamble ? F(", со служебной записью") : F(", прямой пуск"));
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

void writeRangeStart(bool withPreamble) {
  if (withPreamble) {
    sensor.writeReg(0x80, 0x01);
    sensor.writeReg(0xFF, 0x01);
    sensor.writeReg(0x00, 0x00);
    sensor.writeReg(0x91, stopVariable);
    sensor.writeReg(0x00, 0x01);
    sensor.writeReg(0xFF, 0x00);
    sensor.writeReg(0x80, 0x00);
  }
  sensor.writeReg(VL53L0X::SYSRANGE_START, 0x01);
}

// Ждём, пока чип сам закончит замер, и один раз читаем блок результата.
// true — блок прочитан. Готово ли расстояние, решает rangeOk.
bool readRangeBlock(uint16_t *mm, uint8_t *status) {
  delay(80);

  uint8_t model = sensor.readReg(VL53L0X::IDENTIFICATION_MODEL_ID);
  uint8_t hv = sensor.readReg(VL53L0X::VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV);
  uint8_t sequence = sensor.readReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG);
  uint8_t start = sensor.readReg(VL53L0X::SYSRANGE_START);
  uint8_t startErr = sensor.last_status;
  uint8_t irq = sensor.readReg(VL53L0X::RESULT_INTERRUPT_STATUS);
  uint8_t raw[12];
  for (uint8_t i = 0; i < 12; i++) raw[i] = 0;
  sensor.readMulti(VL53L0X::RESULT_RANGE_STATUS, raw, 12);
  uint8_t readErr = sensor.last_status;

  *mm = ((uint16_t)raw[10] << 8) | raw[11];
  *status = (raw[0] & 0x78) >> 3;
  bool finished = readErr == 0 &&
                  (((startErr == 0) && (start & 0x01) == 0) ||
                   (raw[0] & 0x01) != 0 ||
                   (irq & 0x07) != 0);
  sensor.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);

  static uint8_t shown = 0;
  if ((!finished || !rangeOk(*status, *mm)) && shown < 4) {
    shown++;
    Serial.print(F("  модель=0x"));
    if (model < 16) Serial.print('0');
    Serial.print(model, HEX);
    Serial.print(F(" пит=0x"));
    if (hv < 16) Serial.print('0');
    Serial.print(hv, HEX);
    Serial.print(F(" цикл=0x"));
    if (sequence < 16) Serial.print('0');
    Serial.print(sequence, HEX);
    Serial.print(F(" пуск=0x"));
    if (start < 16) Serial.print('0');
    Serial.print(start, HEX);
    Serial.print(startErr == 0 ? F(" ответ есть") : F(" нет ответа"));
    Serial.print(F(" irq=0x"));
    if (irq < 16) Serial.print('0');
    Serial.print(irq, HEX);
    Serial.print(F(" блок"));
    for (uint8_t i = 0; i < 12; i++) {
      Serial.print(' ');
      if (raw[i] < 16) Serial.print('0');
      Serial.print(raw[i], HEX);
    }
    Serial.print(F(" мм="));
    Serial.println(*mm);
  }
  return finished;
}

bool measureOnce(bool withPreamble, uint16_t *mm, uint8_t *status) {
  *mm = 0;
  *status = 255;
  writeRangeStart(withPreamble);
  if (sensor.last_status != 0) {
    Serial.println(F("  пуск не записался"));
    return false;
  }
  // Чип считает расстояние сам. Пока идёт замер, шину не трогаем.
  return readRangeBlock(mm, status);
}

bool readSample(uint16_t *mm, uint8_t *status, bool *timedOut) {
  *timedOut = false;
  bool done = measureOnce(rangeWithPreamble, mm, status);
  if (!done) *timedOut = true;
  return done && rangeOk(*status, *mm);
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
    if (v <= 0 || v > 2500) {
      Serial.println(F("Эталон от 1 до 2500 мм. Пример: ref 400"));
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
  Serial.println(F("Arduino Due, SDA=20 SCL=21, питание 3.3V"));
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
