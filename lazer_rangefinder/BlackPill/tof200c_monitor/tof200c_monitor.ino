/*
  Монитор точности TOF200C на Black Pill STM32F411CE.

  TOF200C — модуль лазерного дальномера на чипе ST VL53L0X.
  Дальность по паспорту 30…2000 мм, шаг 1 мм, интерфейс I2C, адрес 0x29.

  Плата в Arduino IDE (STM32duino):
    Board:         Generic STM32F4 series
    Board part:    BlackPill F411CE
    Upload method: STM32CubeProgrammer (DFU)
    USB support:   CDC (generic 'Serial' supersede U(S)ART)
    U(S)ART:       Enabled (generic Serial)

  Библиотека: VL53L0X by Pololu (Менеджер библиотек).

  Проводка (питание модуля только от 3.3 В, не от 5 В — подтяжки I2C
  на многих платах сидят на VIN и при 5 В сожгут пины Black Pill):
    TOF200C VIN   -> 3.3V
    TOF200C GND   -> GND
    TOF200C SDA   -> PB7
    TOF200C SCL   -> PB6
    TOF200C XSHUT -> PB8   (если такой пин есть; иначе не подключать)
    GPIO1 / INT   не подключать

  Монитор порта: USB Black Pill, 115200.
  Прошивка по DFU затирает текущую программу платы.
*/

#include <Wire.h>
#include <VL53L0X.h>
#include <math.h>

static constexpr int kPinLed = PC13;    // светодиод платы, горит уровнем LOW
static constexpr int kPinXshut = PB8;   // необязательный сброс датчика
static constexpr uint32_t kSda = PB7;
static constexpr uint32_t kScl = PB6;

static constexpr uint32_t kBaud = 115200;
static constexpr uint16_t kWindowMax = 200;
static constexpr uint16_t kWindowDefault = 50;
static constexpr uint32_t kReadTimeoutMs = 500;
static constexpr uint16_t kOutOfRangeMm = 8190;

enum class Mode : uint8_t { Accuracy, Fast, Long, Default };

struct Stats {
  float mean;
  float sd;
  int32_t minV;
  int32_t maxV;
  float median;
};

VL53L0X sensor;
Mode mode = Mode::Accuracy;
bool sensorReady = false;
uint8_t failStreak = 0;

int32_t samples[kWindowMax];
uint16_t windowN = kWindowDefault;
uint16_t filled = 0;
uint16_t head = 0;
uint16_t sinceReport = 0;
uint16_t rejected = 0;

bool refOn = false;
int32_t refMm = 0;
int32_t offsetMm = 0;  // показ = raw - offsetMm

char cmd[48];
uint8_t cmdLen = 0;
bool bannerSent = false;

const char *modeName(Mode m) {
  switch (m) {
    case Mode::Accuracy: return "точность (бюджет 200 мс)";
    case Mode::Fast:     return "скорость (бюджет 20 мс)";
    case Mode::Long:     return "дальность до ~2 м";
    default:             return "обычный (бюджет 33 мс)";
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("Команды (в мониторе порта, перевод строки NL):"));
  Serial.println(F("  help          эта справка"));
  Serial.println(F("  acc           режим максимальной точности, ~5 Гц"));
  Serial.println(F("  def           обычный режим VL53L0X, ~30 Гц"));
  Serial.println(F("  fast          быстрый режим, шумнее"));
  Serial.println(F("  long          дальняя дистанция, до ~2 м, шумнее"));
  Serial.println(F("  ref 500       эталон с линейки, миллиметры"));
  Serial.println(F("  ref off       убрать эталон"));
  Serial.println(F("  cal           убрать постоянный сдвиг: среднее окна = эталон"));
  Serial.println(F("  uncal         убрать калибровку сдвига"));
  Serial.println(F("  clear         очистить окно статистики"));
  Serial.println(F("  win 50        размер окна, 10…200 замеров"));
  Serial.println();
  Serial.println(F("Как смотреть точность:"));
  Serial.println(F("  1. Белая или серая матовая мишень, перпендикулярно лучу."));
  Serial.println(F("  2. Линейкой измерить расстояние от крышки датчика до мишени."));
  Serial.println(F("  3. ref <это число в мм>, подождать полное окно."));
  Serial.println(F("  4. Систематическая ошибка — насколько врет в среднем."));
  Serial.println(F("  5. СКО — дрожание. Полоса 3*СКО — куда ложится почти каждый замер."));
  Serial.println(F("  6. cal убирает постоянный сдвиг крышки. После cal ошибка"));
  Serial.println(F("     должна гулять около нуля, а СКО останется мерой повторяемости."));
  Serial.println();
}

void clearWindow() {
  filled = 0;
  head = 0;
  sinceReport = 0;
  rejected = 0;
}

void scanI2C() {
  Serial.println(F("Поиск устройств на I2C..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  адрес 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("  никого нет. Проверьте 3.3V, GND, SDA=PB7, SCL=PB6."));
  }
}

bool applyMode(Mode next, bool scanOnFail) {
  mode = next;
  sensorReady = false;
  clearWindow();

  pinMode(kPinXshut, OUTPUT);
  digitalWrite(kPinXshut, LOW);
  delay(5);
  digitalWrite(kPinXshut, HIGH);
  delay(5);

  sensor.setTimeout(kReadTimeoutMs);
  if (!sensor.init()) {
    Serial.println(F("TOF200C не отвечает (VL53L0X, адрес 0x29)."));
    if (scanOnFail) scanI2C();
    return false;
  }

  bool cfgOk = true;
  switch (mode) {
    case Mode::Accuracy:
      cfgOk = sensor.setMeasurementTimingBudget(200000);
      break;
    case Mode::Fast:
      cfgOk = sensor.setMeasurementTimingBudget(20000);
      break;
    case Mode::Long:
      cfgOk = sensor.setSignalRateLimit(0.1f);
      cfgOk = sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18) && cfgOk;
      cfgOk = sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14) && cfgOk;
      break;
    case Mode::Default:
      break;
  }
  if (!cfgOk) {
    Serial.println(F("Датчик найден, но режим не применился. Оставляю как есть."));
  }

  sensor.startContinuous();
  sensorReady = true;
  failStreak = 0;
  Serial.print(F("Режим: "));
  Serial.println(modeName(mode));
  return true;
}

int32_t shownMm(int32_t raw) {
  return raw - offsetMm;
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
  if ((filled & 1u) == 1u) {
    out->median = (float)sorted[filled / 2];
  } else {
    out->median = 0.5f * (float)(sorted[filled / 2 - 1] + sorted[filled / 2]);
  }
}

void printStats(const char *title) {
  Stats st;
  computeStats(&st);

  Serial.println();
  Serial.print(F("---- "));
  Serial.print(title);
  Serial.print(F("  "));
  Serial.print(filled);
  Serial.print('/');
  Serial.print(windowN);
  Serial.print(F("  "));
  Serial.print(modeName(mode));
  Serial.println(F(" ----"));

  if (filled == 0) {
    Serial.println(F("В окне ещё нет годных замеров."));
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

  Serial.print(F("полоса шума около "));
  Serial.print(3.0f * st.sd, 1);
  Serial.println(F(" мм в обе стороны от среднего (3*СКО)"));

  if (offsetMm != 0) {
    Serial.print(F("калибровка сдвига: показ = raw "));
    if (offsetMm > 0) Serial.print(F("- "));
    else Serial.print(F("+ "));
    Serial.print(offsetMm > 0 ? offsetMm : -offsetMm);
    Serial.println(F(" мм"));
  }

  if (refOn) {
    float bias = st.mean - (float)refMm;
    Serial.print(F("эталон "));
    Serial.print(refMm);
    Serial.print(F(" мм   систематическая ошибка "));
    if (bias >= 0) Serial.print('+');
    Serial.print(bias, 1);
    Serial.print(F(" мм  ("));
    if (refMm != 0) {
      float pct = 100.0f * bias / (float)refMm;
      if (pct >= 0) Serial.print('+');
      Serial.print(pct, 2);
      Serial.println(F(" %)"));
    } else {
      Serial.println(F(")"));
    }
  } else {
    Serial.println(F("Эталон не задан. Пример: ref 400"));
  }

  Serial.print(F("отбраковано с прошлого отчёта: "));
  Serial.println(rejected);
  Serial.println();
}

const char *statusText(uint8_t deviceStatus) {
  switch (deviceStatus) {
    case 11: return "OK";
    case 4:  return "нет отражения";
    case 5:  return "слабый сигнал";
    case 6:  return "вне диапазона";
    case 7:  return "слишком шумно";
    case 10: return "ближе мёртвой зоны";
    case 1:
    case 2:
    case 3:  return "сбой датчика";
    default: return "негодный замер";
  }
}

bool readSample(uint16_t *rawMm, uint8_t *deviceStatus, bool *timedOut) {
  *timedOut = false;
  uint32_t t0 = millis();
  while ((sensor.readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
    if (millis() - t0 > kReadTimeoutMs) {
      *timedOut = true;
      return false;
    }
  }

  uint8_t buf[12];
  sensor.readMulti(VL53L0X::RESULT_RANGE_STATUS, buf, 12);
  sensor.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);

  *deviceStatus = (buf[0] & 0x78) >> 3;
  *rawMm = (uint16_t)((buf[10] << 8) | buf[11]);
  return *deviceStatus == 11 && *rawMm < kOutOfRangeMm;
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

void printLive(int32_t raw, uint8_t deviceStatus, bool ok) {
  if (!ok) {
    digitalWrite(kPinLed, HIGH);
    Serial.print(F("-- "));
    Serial.print(statusText(deviceStatus));
    Serial.print(F(" (код "));
    Serial.print(deviceStatus);
    Serial.println(')');
    return;
  }

  digitalWrite(kPinLed, LOW);
  int32_t shown = shownMm(raw);
  Stats st;
  computeStats(&st);

  Serial.print(shown);
  Serial.print(F(" мм"));
  if (offsetMm != 0) {
    Serial.print(F("  raw "));
    Serial.print(raw);
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
  if (raw < 40) {
    Serial.print(F("  мёртвая зона"));
  }
  Serial.println();
}

void handleCmd(char *line) {
  while (*line == ' ') line++;
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\r')) {
    line[--n] = 0;
  }
  if (n == 0) return;

  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0 || strcmp(line, "h") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "acc") == 0 || strcmp(line, "a") == 0) {
    applyMode(Mode::Accuracy, true);
    return;
  }
  if (strcmp(line, "def") == 0 || strcmp(line, "d") == 0) {
    applyMode(Mode::Default, true);
    return;
  }
  if (strcmp(line, "fast") == 0 || strcmp(line, "f") == 0) {
    applyMode(Mode::Fast, true);
    return;
  }
  if (strcmp(line, "long") == 0 || strcmp(line, "l") == 0) {
    applyMode(Mode::Long, true);
    return;
  }
  if (strcmp(line, "clear") == 0 || strcmp(line, "c") == 0) {
    clearWindow();
    Serial.println(F("Окно очищено."));
    return;
  }
  if (strcmp(line, "uncal") == 0) {
    offsetMm = 0;
    Serial.println(F("Сдвиг сброшен, показывается raw."));
    return;
  }
  if (strcmp(line, "cal") == 0) {
    if (!refOn) {
      Serial.println(F("Сначала эталон, например: ref 400"));
      return;
    }
    if (filled < windowN) {
      Serial.println(F("Подождите, пока окно заполнится, и повторите cal."));
      return;
    }
    double sum = 0;
    for (uint16_t i = 0; i < filled; i++) sum += samples[i];
    float meanRaw = (float)(sum / filled);
    offsetMm = (int32_t)(meanRaw >= (float)refMm
                             ? (meanRaw - (float)refMm) + 0.5f
                             : (meanRaw - (float)refMm) - 0.5f);
    Serial.print(F("Сдвиг записан: "));
    Serial.print(offsetMm);
    Serial.println(F(" мм. Дальше показ = raw - сдвиг."));
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
    if (*arg == 0) {
      Serial.println(F("Нужно число миллиметров: ref 400"));
      return;
    }
    long v = strtol(arg, nullptr, 10);
    if (v <= 0 || v > 4000) {
      Serial.println(F("Эталон должен быть от 1 до 4000 мм."));
      return;
    }
    refMm = (int32_t)v;
    refOn = true;
    Serial.print(F("Эталон "));
    Serial.print(refMm);
    Serial.println(F(" мм. Ошибка = показ - эталон."));
    return;
  }
  if (strncmp(line, "win", 3) == 0) {
    const char *arg = line + 3;
    while (*arg == ' ') arg++;
    long v = strtol(arg, nullptr, 10);
    if (v < 10 || v > kWindowMax) {
      Serial.print(F("Окно от 10 до "));
      Serial.println(kWindowMax);
      return;
    }
    windowN = (uint16_t)v;
    clearWindow();
    Serial.print(F("Окно "));
    Serial.print(windowN);
    Serial.println(F(" замеров, статистика сброшена."));
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
  digitalWrite(kPinLed, HIGH);

  Serial.begin(kBaud);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2500) {
    delay(10);
  }

  Wire.setSDA(kSda);
  Wire.setSCL(kScl);
  Wire.begin();
  Wire.setClock(100000);

  Serial.println();
  Serial.println(F("TOF200C / VL53L0X — монитор расстояния и точности"));
  Serial.println(F("Black Pill F411, I2C SDA=PB7 SCL=PB6, питание датчика 3.3V"));
  printHelp();
  bannerSent = true;

  if (!applyMode(Mode::Accuracy, true)) {
    Serial.println(F("Жду датчик, повтор инициализации каждую секунду."));
  }
}

void loop() {
  pollSerial();

  if (!sensorReady) {
    digitalWrite(kPinLed, (millis() / 150) & 1 ? LOW : HIGH);
    static uint32_t tRetry = 0;
    if (millis() - tRetry >= 1000) {
      tRetry = millis();
      applyMode(mode, false);
    }
    return;
  }

  uint16_t raw = 0;
  uint8_t st = 0;
  bool timedOut = false;
  bool ok = readSample(&raw, &st, &timedOut);

  if (timedOut) {
    failStreak++;
    digitalWrite(kPinLed, HIGH);
    Serial.println(F("Таймаут чтения."));
    if (failStreak >= 3) {
      Serial.println(F("Три таймаута подряд, перезапуск датчика."));
      applyMode(mode, true);
    }
    return;
  }
  failStreak = 0;

  if (ok) {
    pushRaw((int32_t)raw);
  } else {
    rejected++;
  }
  printLive((int32_t)raw, st, ok);

  if (ok && sinceReport >= windowN) {
    printStats("отчёт");
    sinceReport = 0;
    rejected = 0;
  }
}
