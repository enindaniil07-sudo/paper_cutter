/*
  Black Pill F411CE - strict FSM version

  Цель:
  - Чёткая архитектура на основе конечного автомата (FSM): явные состояния и переходы
  - Неблокирующие таймеры (millis), отсутствие delay()
  - Фильтрация дребезга дискретных входов
  - Упаковка входов/выходов в 32-битовые битовые поля (xBits / yBits)

  Ключевые идеи:
  - Слои: сбор входов -> переходы автомата -> расчёт выходов -> применить выходы
  - State (Moore) хранит высокий уровень логики; вычисление выходов опирается на state+inputs
  - Временная база берётся с аппаратного таймера (TIM2, 1мс тик)
  - STOP/SAFETY дополнительно подтверждаются внешними прерываниями
*/

// Логические индексы сигналов X/Y (читабельные имена вместо цифр)
#include "stm32_def.h"
#include "wiring_private.h"
enum XIdx : uint8_t { X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X_COUNT };
enum YIdx : uint8_t { Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8, Y9, Y10, Y11, Y12, Y13, Y14, Y15, Y16, Y17, Y_COUNT };

// Привязка логических X к физическим пинам MCU (−1 = не используется)
const int X_PIN[X_COUNT] = {
  PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7,
  -1, -1, PB0, PB1, PB10, PB12, PB13, PB14, PB15
};

// Привязка логических Y к пинам MCU (−1 = не используется)
const int Y_PIN[Y_COUNT] = {
  PC13, PA8, -1, -1, PA9, PA10, -1, -1, -1, -1, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PB11
};

// Полярность входов/выходов и время дебаунса
constexpr bool INPUT_ACTIVE_HIGH = true;
constexpr bool OUTPUT_ACTIVE_HIGH = true;
constexpr uint16_t DEBOUNCE_MS = 20;

// ---------------- Аппаратная база времени и прерывания ----------------
volatile uint32_t g_hwMs = 0;          // монотонный миллисекундный тик от TIM2
volatile bool g_estopIrqLatched = false;  // защелка события ESTOP (X16)
volatile bool g_safetyIrqLatched = false; // защелка события SAFETY (X14)
HardwareTimer *g_hwTimer = nullptr;

void onHardwareTick1ms() { g_hwMs++; }

void onEStopIrq() { g_estopIrqLatched = true; }
void onSafetyIrq() { g_safetyIrqLatched = true; }

uint32_t hwNowMs() {
  noInterrupts();
  const uint32_t t = g_hwMs;
  interrupts();
  return t;
}

// Фильтр дребезга дискретного входа с задержкой DEBOUNCE_MS
struct DebouncedInput {
  bool stable = false;
  bool rawPrev = false;
  uint32_t edgeAt = 0;

  void update(bool raw, uint32_t nowMs) {
    if (raw != rawPrev) {
      rawPrev = raw;
      edgeAt = nowMs;
    }
    if ((nowMs - edgeAt) >= DEBOUNCE_MS) stable = raw;
  }
};

// Неблокирующий генератор импульсов (включено onMs, затем offMs; цикл повторяется)
struct Pulse {
  uint32_t onMs = 0;
  uint32_t offMs = 0;
  bool sample(uint32_t nowMs) const {
    const uint32_t p = onMs + offMs;
    return (p != 0U) && ((nowMs % p) < onMs);
  }
};

// Набор состояний автомата (явные режимы работы)
enum class State : uint8_t {
  STOPPED,
  MANUAL,
  AUTO_IDLE,
  AUTO_STEP,
  SAFETY_LATCHED,
  ESTOP
};

class App {
public:
  void begin() {
    // Инициализация пинов ввода/вывода и гарантированное выключение выходов
    for (uint8_t i = 0; i < X_COUNT; ++i) {
      if (X_PIN[i] >= 0) pinMode(X_PIN[i], INPUT);
      // Кэш портов/масок для быстрого чтения GPIO
      if (X_PIN[i] >= 0) {
        X_PORT[i] = digitalPinToPort(X_PIN[i]);
        X_MASK[i] = digitalPinToBitMask(X_PIN[i]);
      } else {
        X_PORT[i] = nullptr;
        X_MASK[i] = 0;
      }
    }
    for (uint8_t i = 0; i < Y_COUNT; ++i) {
      if (Y_PIN[i] >= 0) {
        pinMode(Y_PIN[i], OUTPUT);
        // Кэш портов/масок для быстрой записи GPIO
        Y_PORT[i] = digitalPinToPort(Y_PIN[i]);
        Y_MASK[i] = digitalPinToBitMask(Y_PIN[i]);
        writeY(static_cast<YIdx>(i), false);
      } else {
        Y_PORT[i] = nullptr;
        Y_MASK[i] = 0;
      }
    }
    // Начальные значения задержек
    offDelayReq = false;
    offDelayUntil = 0;
  }

  // Немедленный аварийный сброс выходов (вызывается при IRQ-защелке ESTOP).
  // Делается в основном потоке, а не в ISR, чтобы ISR оставался коротким.
  void fastStopOutputs() {
    yBits = 0;
    state = State::ESTOP;
    for (uint8_t i = 0; i < Y_COUNT; ++i) {
      writeY(static_cast<YIdx>(i), false);
    }
  }

  void tick() {
    // Один управляющий цикл: входы → переходы → выходы → применить
    const uint32_t now = hwNowMs();
    sampleInputs(now);
    transition(now);
    buildOutputs(now);
    applyOutputs();
  }

private:
  DebouncedInput x[X_COUNT]; // фильтр дребезга для каждого входа
  uint32_t xBits = 0;        // упакованные стабильные входы (бит=1 → Xn=1)
  uint32_t yBits = 0;        // упакованные выходы (бит=1 → Yn=1)
  uint32_t lastYBits = 0;    // прошлое состояние выходов для дифференциальной записи

  // Быстрый слой прямого GPIO (кэш адресов портов и масок пинов)
  GPIO_TypeDef* X_PORT[X_COUNT];
  uint32_t      X_MASK[X_COUNT];
  GPIO_TypeDef* Y_PORT[Y_COUNT];
  uint32_t      Y_MASK[Y_COUNT];

  State state = State::STOPPED; // текущее состояние автомата
  bool safetyLatched = false;   // защёлкнутая "безопасность" (X14)
  bool pulse400 = false;        // текущее состояние 400мс импульса
  uint32_t pulse400Until = 0;   // момент времени окончания 400мс импульса
  uint32_t offDelayUntil = 0;   // отложенное отключение главного разрешения
  bool offDelayReq = false;     // флаг активного офф-делея
  uint32_t alarmHoldUntil = 0;  // удержание аларма до этого времени
  bool mainEnableCached = false; // кэш главного разрешения на текущий тик

  Pulse fastBlink{500, 500};    // аналог M1013: 0.5с вкл / 0.5с выкл
  Pulse slowBlink{1000, 1000};  // аналог M10: 1с вкл / 1с выкл

  // Читать сырой уровень пина с учетом полярности входа
  bool readRawX(XIdx idx) const {
    GPIO_TypeDef* port = X_PORT[idx];
    if (port == nullptr) return false;
    const bool raw = (port->IDR & X_MASK[idx]) != 0U;
    return INPUT_ACTIVE_HIGH ? raw : !raw;
  }

  // Прочитать стабильное (после фильтрации дребезга) состояние входа из битового поля
  bool isX(XIdx idx) const { return ((xBits >> idx) & 1U) != 0U; }

  // Установить/сбросить бит выхода
  void setY(YIdx idx, bool v) {
    const uint32_t m = (1UL << idx);
    if (v) yBits |= m; else yBits &= ~m;
  }

  // Фактическая запись уровня на GPIO с учетом полярности выхода
  void writeY(YIdx idx, bool v) const {
    GPIO_TypeDef* port = Y_PORT[idx];
    if (port == nullptr) return;
    const bool lvl = OUTPUT_ACTIVE_HIGH ? v : !v;
    const uint32_t mask = Y_MASK[idx];
    if (lvl) {
      port->BSRR = mask;           // set bit
    } else {
      port->BSRR = (mask << 16);   // reset bit
    }
  }

  // Главный запрос разрешения работы установки (учитывает X0, X5, X6 и отсутствие safety)
  bool isMainEnableRequested() const {
    return ((isX(X0) && isX(X5)) || isX(X6)) && !safetyLatched;
  }

  // Главный enable с отложенным выключением (имитация задержки отпускания)
  bool mainEnableWithOffDelay(uint32_t now) {
    if (isMainEnableRequested()) {
      offDelayReq = true;
      offDelayUntil = now + 500UL;
      return true;
    }
    if (offDelayReq && now < offDelayUntil) return true;
    offDelayReq = false;
    return false;
  }

  // Сэмплинг входов: фильтрация дребезга + упаковка в xBits
  void sampleInputs(uint32_t now) {
    xBits = 0;
    for (uint8_t i = 0; i < X_COUNT; ++i) {
      x[i].update(readRawX(static_cast<XIdx>(i)), now);
      if (x[i].stable) xBits |= (1UL << i);
    }
  }

  // Переходы автомата состояний (State Machine)
  void transition(uint32_t now) {
    // Асинхронные защелки от внешних прерываний (быстрые сигналы STOP/SAFETY).
    if (g_safetyIrqLatched) safetyLatched = true;
    if (g_estopIrqLatched && isX(X16)) {
      state = State::ESTOP;
      return;
    }
    // Разрешаем снять IRQ-защелку ESTOP после отпускания X16.
    if (!isX(X16)) g_estopIrqLatched = false;

    // Защелка безопасности (safetyLatched): включаем X14, снимаем только если всё спокойно
    if (isX(X14)) safetyLatched = true;
    if (!isX(X14) && !isX(X16) && !isX(X5) && !isX(X6)) safetyLatched = false;

    // Аварийный стоп имеет высший приоритет
    if (isX(X16)) {
      state = State::ESTOP;
      return;
    }

    // При активной защите — отдельное состояние
    if (safetyLatched) {
      state = State::SAFETY_LATCHED;
      return;
    }

    // Нет ни авто, ни ручного → останов
    if (!isX(X5) && !isX(X6)) {
      state = State::STOPPED;
      return;
    }

    // Ручной режим имеет приоритет над авто при X6=1
    if (isX(X6)) {
      state = State::MANUAL;
      return;
    }

    // Ветка автоматического режима: определяем, “ожидание” или “шаг”
    mainEnableCached = mainEnableWithOffDelay(now); // вычисляем 1 раз за тик
    const bool stepArm = isX(X0) && isX(X5) && isX(X15) && isX(X13) && mainEnableCached;
    state = stepArm ? State::AUTO_STEP : State::AUTO_IDLE;
  }

  // Формирование выходов на основании состояния и входов
  void buildOutputs(uint32_t now) {
    yBits = 0;

    const bool autoReq = isX(X5);
    const bool manReq = isX(X6);
    const bool estop = (state == State::ESTOP);
    const bool mainEnable = mainEnableCached && !estop;
    const bool inStep = (state == State::AUTO_STEP);

    // 400мс импульс по событию X12 в режиме шага
    if (inStep && isX(X12)) pulse400Until = now + 400UL;
    pulse400 = now < pulse400Until;

    // Две ветки маршрутов с условиями режима
    const bool routeA = (isX(X1) && manReq) || (autoReq && inStep);
    const bool routeB = (isX(X2) && manReq) || (autoReq && !inStep);

    // Основные технологические выходы с блокировками безопасности
    const bool y4 = routeA && !isX(X11) && !estop;
    const bool y5 = routeB && !isX(X10) && !isX(X15);
    const bool y10 = ((autoReq || manReq) && isX(X3)) || pulse400;
    const bool y11 = (manReq && isX(X4)) || (inStep && !estop);

    // Прямые выходы
    setY(Y0, mainEnable);
    setY(Y1, isX(X7) && (state != State::STOPPED) && (state != State::ESTOP));
    setY(Y4, y4);
    setY(Y5, y5);
    setY(Y10, y10);
    setY(Y11, y11);

    // Комбинированные статусы (аналог M150/M21) и индикаторы
    const bool m150Like = routeA && isX(X11) && !estop;
    const bool m21Like = routeB && isX(X10) && !isX(X15);
    setY(Y16, m150Like || m21Like);
    setY(Y17, m150Like || y4);
    setY(Y13, inStep);
    setY(Y14, safetyLatched);

    // Индикация Y15: устойчивый ON при нормальной работе, быстрая/медленная мигающая при условиях
    const bool condSteady = (state == State::MANUAL || state == State::AUTO_IDLE || state == State::AUTO_STEP) && !safetyLatched;
    const bool condFast = fastBlink.sample(now) && !isX(X0) && autoReq;
    const bool condSlow = slowBlink.sample(now) && safetyLatched;
    setY(Y15, condSteady || condFast || condSlow);

    // Аларм Y12: включён, пока в последние 5с был хотя бы один “ядровой” выход
    const bool anyCoreOn = y4 || y5 || y10 || y11;
    if (anyCoreOn) {
      alarmHoldUntil = now + 5000UL;
      setY(Y12, true);
    } else {
      setY(Y12, now < alarmHoldUntil);
    }
  }

  // Применение битов выходов к GPIO
  void applyOutputs() {
    uint32_t diff = lastYBits ^ yBits;
    if (diff == 0) return;
    // Пишем только изменившиеся биты, уменьшаем количество вызовов GPIO
    while (diff) {
      uint8_t idx = __builtin_ctz(diff);
      diff &= (diff - 1);
      writeY(static_cast<YIdx>(idx), ((yBits >> idx) & 1U) != 0U);
    }
    lastYBits = yBits;
  }
};

App app;

void setup() {
  // Аппаратный таймер TIM2: 1 МГц, прерывание раз в 1000 тиков => 1 мс.
  g_hwTimer = new HardwareTimer(TIM2);
  g_hwTimer->setPrescaleFactor(84); // 84MHz / 84 = 1MHz (для STM32F411)
  g_hwTimer->setOverflow(1000, TICK_FORMAT);
  g_hwTimer->attachInterrupt(onHardwareTick1ms);
  g_hwTimer->resume();

  app.begin();

  // Внешние прерывания для быстрого обнаружения safety/estop.
  if (X_PIN[X16] >= 0) attachInterrupt(digitalPinToInterrupt(X_PIN[X16]), onEStopIrq, CHANGE);
  if (X_PIN[X14] >= 0) attachInterrupt(digitalPinToInterrupt(X_PIN[X14]), onSafetyIrq, CHANGE);
}

void loop() {
  // Приоритетная реакция: если поймали ESTOP IRQ, мгновенно гасим выходы.
  if (g_estopIrqLatched) app.fastStopOutputs();
  app.tick();
}
