/*
  Black Pill F411CE - FSM with helper libraries
  - Debounce:       Bounce2
  - Scheduling:     TaskScheduler
  - FSM framework:  FiniteStateMachine
  - Logging:        ArduinoLog

  Notes:
  - Install libraries from Arduino Library Manager:
    * Bounce2
    * TaskScheduler
    * FiniteStateMachine
    * ArduinoLog
  - This sketch assumes the same logical I/O mapping as other files.
*/

#include <Bounce2.h>
#include <TaskScheduler.h>
#include <FiniteStateMachine.h>
#include <ArduinoLog.h>
#include "stm32_def.h"
#include "wiring_private.h"

// ---------------- Logical indexes ----------------
enum XIdx : uint8_t { X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X_COUNT };
enum YIdx : uint8_t { Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8, Y9, Y10, Y11, Y12, Y13, Y14, Y15, Y16, Y17, Y_COUNT };

// ---------------- Pin map (edit to your wiring) ----------------
const int X_PIN[X_COUNT] = {
  PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7,
  -1,  -1,  PB0, PB1, PB10, PB12, PB13, PB14, PB15
};

const int Y_PIN[Y_COUNT] = {
  PC13, PA8, -1, -1, PA9, PA10, -1, -1, -1, -1, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PB11
};

// ---------------- Electrical polarity and timings ----------------
constexpr bool INPUT_ACTIVE_HIGH = true;
constexpr bool OUTPUT_ACTIVE_HIGH = true;
constexpr uint16_t DEBOUNCE_MS = 20;

// ---------------- Bitfields ----------------
static uint32_t xBits = 0; // debounced inputs
static uint32_t yBits = 0; // computed outputs
static uint32_t lastYBits = 0; // for differential GPIO updates

// ---------------- Debouncers ----------------
static Bounce deb[X_COUNT];

// ---------------- Fast GPIO cache ----------------
static GPIO_TypeDef* X_PORT[X_COUNT];
static uint32_t      X_MASK[X_COUNT];
static GPIO_TypeDef* Y_PORT[Y_COUNT];
static uint32_t      Y_MASK[Y_COUNT];

// ---------------- Hardware time base and IRQ latches ----------------
volatile uint32_t g_hwMs = 0;
volatile bool g_estopIrqLatched = false;
volatile bool g_safetyIrqLatched = false;
HardwareTimer *g_hwTimer = nullptr;

void onHardwareTick1ms() { g_hwMs++; }
void onEStopIrq() { g_estopIrqLatched = true; }
void onSafetyIrq() { g_safetyIrqLatched = true; }

inline uint32_t hwNowMs() {
  noInterrupts();
  const uint32_t t = g_hwMs;
  interrupts();
  return t;
}

// ---------------- Helpers ----------------
inline bool bitGet(uint32_t bits, uint8_t idx) { return ((bits >> idx) & 1U) != 0U; }
inline void bitSetTo(uint32_t &bits, uint8_t idx, bool val) {
  const uint32_t m = (1UL << idx);
  if (val) bits |= m; else bits &= ~m;
}

inline bool readActiveLevel(int pin) {
  if (pin < 0) return false;
  GPIO_TypeDef* port = digitalPinToPort(pin);
  const uint32_t mask = digitalPinToBitMask(pin);
  const bool raw = (port->IDR & mask) != 0U;
  return INPUT_ACTIVE_HIGH ? raw : !raw;
}

inline void writeActiveLevel(int pin, bool value) {
  if (pin < 0) return;
  const bool level = OUTPUT_ACTIVE_HIGH ? value : !value;
  GPIO_TypeDef* port = digitalPinToPort(pin);
  const uint32_t mask = digitalPinToBitMask(pin);
  if (level) {
    port->BSRR = mask;
  } else {
    port->BSRR = (mask << 16);
  }
}

// ---------------- Pulses (non-blocking) ----------------
struct Pulse {
  uint32_t onMs = 0;
  uint32_t offMs = 0;
  bool sample(uint32_t nowMs) const {
    const uint32_t p = onMs + offMs;
    return (p != 0U) && ((nowMs % p) < onMs);
  }
};
static Pulse FAST_1S{500, 500};   // like M1013
static Pulse SLOW_2S{1000, 1000}; // like M10

// ---------------- Off-delay latch for main enable ----------------
static bool mainEnableReq = false;
static bool mainEnableOut = false;
static uint32_t mainEnableOffAt = 0;
static const uint32_t MAIN_OFF_DELAY_MS = 500;

static void updateMainEnable(bool request, uint32_t now) {
  if (request) {
    mainEnableOut = true;
    mainEnableReq = true;
    mainEnableOffAt = now + MAIN_OFF_DELAY_MS;
  } else {
    mainEnableReq = false;
    if (now >= mainEnableOffAt) mainEnableOut = false;
  }
}

// ---------------- FSM states ----------------
void onEnter_STOPPED();    void onUpdate_STOPPED();    void onExit_STOPPED();
void onEnter_MANUAL();     void onUpdate_MANUAL();     void onExit_MANUAL();
void onEnter_AUTO_IDLE();  void onUpdate_AUTO_IDLE();  void onExit_AUTO_IDLE();
void onEnter_AUTO_STEP();  void onUpdate_AUTO_STEP();  void onExit_AUTO_STEP();
void onEnter_SAFETY();     void onUpdate_SAFETY();     void onExit_SAFETY();
void onEnter_ESTOP();      void onUpdate_ESTOP();      void onExit_ESTOP();

State STOPPED(onEnter_STOPPED, onUpdate_STOPPED, onExit_STOPPED);
State MANUAL(onEnter_MANUAL, onUpdate_MANUAL, onExit_MANUAL);
State AUTO_IDLE(onEnter_AUTO_IDLE, onUpdate_AUTO_IDLE, onExit_AUTO_IDLE);
State AUTO_STEP(onEnter_AUTO_STEP, onUpdate_AUTO_STEP, onExit_AUTO_STEP);
State SAFETY_LATCHED(onEnter_SAFETY, onUpdate_SAFETY, onExit_SAFETY);
State ESTOP(onEnter_ESTOP, onUpdate_ESTOP, onExit_ESTOP);

FSM machine(STOPPED);

// ---------------- Internal flags ----------------
static bool safetyLatched = false;
static bool pulse400 = false;
static uint32_t pulse400Until = 0;
static uint32_t alarmHoldUntil = 0;

static inline void fastStopOutputsNow() {
  yBits = 0;
  // Force all configured outputs LOW via BSRR for minimal latency.
  for (uint8_t i = 0; i < Y_COUNT; ++i) {
    if (Y_PORT[i] != nullptr) {
      Y_PORT[i]->BSRR = (Y_MASK[i] << 16);
    }
  }
  lastYBits = 0;
}

// ---------------- Scheduling ----------------
Scheduler ts;

void taskScanInputsCB();
void taskLogicTickCB();
Task taskScanInputs(5, TASK_FOREVER, &taskScanInputsCB, &ts, true);
Task taskLogicTick(10, TASK_FOREVER, &taskLogicTickCB, &ts, true);

// ---------------- Input/Output core ----------------
static inline bool X(XIdx i) { return bitGet(xBits, i); }
static inline void Y(YIdx i, bool v) { bitSetTo(yBits, i, v); }

static void applyOutputs() {
  uint32_t diff = lastYBits ^ yBits;
  if (diff == 0) return;
  while (diff) {
    uint8_t idx = __builtin_ctz(diff);
    diff &= (diff - 1);
    if (Y_PORT[idx] == nullptr) continue;
    const bool value = bitGet(yBits, idx);
    const bool level = OUTPUT_ACTIVE_HIGH ? value : !value;
    if (level) {
      Y_PORT[idx]->BSRR = Y_MASK[idx];
    } else {
      Y_PORT[idx]->BSRR = (Y_MASK[idx] << 16);
    }
  }
  lastYBits = yBits;
}

// ---------------- Scan inputs (debounced via Bounce2) ----------------
void taskScanInputsCB() {
  uint32_t now = hwNowMs();
  xBits = 0;
  for (uint8_t i = 0; i < X_COUNT; ++i) {
    if (X_PIN[i] < 0) continue;
    deb[i].update();
    const bool st = INPUT_ACTIVE_HIGH ? deb[i].read() : !deb[i].read();
    if (st) xBits |= (1UL << i);
  }
  // Maintain main enable latch timing here to reduce jitter.
  const bool mainReq = ((X(X0) && X(X5)) || X(X6)) && !safetyLatched;
  updateMainEnable(mainReq, now);
}

// ---------------- Transitions (guarded) ----------------
static void evaluateTransitions(uint32_t now) {
  if (g_safetyIrqLatched) safetyLatched = true;
  if (g_estopIrqLatched && X(X16)) {
    if (machine.getCurrentState() != &ESTOP) machine.transitionTo(ESTOP);
    return;
  }
  if (!X(X16)) g_estopIrqLatched = false;

  // Safety latch logic: latch with X14, clear only when X14=0, X16=0 and no mode requested
  if (X(X14)) safetyLatched = true;
  if (!X(X14) && !X(X16) && !X(X5) && !X(X6)) safetyLatched = false;

  // Highest priority transitions
  if (X(X16)) {
    if (machine.getCurrentState() != &ESTOP) machine.transitionTo(ESTOP);
    return;
  }
  if (safetyLatched) {
    if (machine.getCurrentState() != &SAFETY_LATCHED) machine.transitionTo(SAFETY_LATCHED);
    return;
  }
  if (!X(X5) && !X(X6)) {
    if (machine.getCurrentState() != &STOPPED) machine.transitionTo(STOPPED);
    return;
  }
  if (X(X6)) {
    if (machine.getCurrentState() != &MANUAL) machine.transitionTo(MANUAL);
    return;
  }
  // Auto branch
  const bool stepArm = X(X0) && X(X5) && X(X15) && X(X13) && mainEnableOut;
  if (stepArm) {
    if (machine.getCurrentState() != &AUTO_STEP) machine.transitionTo(AUTO_STEP);
  } else {
    if (machine.getCurrentState() != &AUTO_IDLE) machine.transitionTo(AUTO_IDLE);
  }
}

// ---------------- Output computation (shared across states) ----------------
static void computeOutputs(uint32_t now) {
  yBits = 0;

  const bool autoReq = X(X5);
  const bool manReq = X(X6);
  const bool inEStop = (machine.getCurrentState() == &ESTOP);
  const bool inStep  = (machine.getCurrentState() == &AUTO_STEP);
  const bool mainOk  = mainEnableOut && !inEStop;

  // 400ms pulse from X12 in step
  if (inStep && X(X12)) pulse400Until = now + 400UL;
  pulse400 = now < pulse400Until;

  // Routes
  const bool routeA = (X(X1) && manReq) || (autoReq && inStep);
  const bool routeB = (X(X2) && manReq) || (autoReq && !inStep);

  const bool y4  = routeA && !X(X11) && !inEStop;
  const bool y5  = routeB && !X(X10) && !X(X15);
  const bool y10 = ((autoReq || manReq) && X(X3)) || pulse400;
  const bool y11 = (manReq && X(X4)) || (inStep && !inEStop);

  // Direct outputs
  Y(Y0, mainOk);
  Y(Y1, X(X7) && (machine.getCurrentState() != &STOPPED) && (machine.getCurrentState() != &ESTOP));
  Y(Y4, y4);
  Y(Y5, y5);
  Y(Y10, y10);
  Y(Y11, y11);

  // Combined status
  const bool m150Like = routeA && X(X11) && !inEStop;
  const bool m21Like  = routeB && X(X10) && !X(X15);
  Y(Y16, m150Like || m21Like);
  Y(Y17, m150Like || y4);
  Y(Y13, inStep);
  Y(Y14, safetyLatched);

  // Y15: steady in normal op, fast blink when auto & X0=0, slow blink when safety
  const bool condSteady = ((machine.getCurrentState() == &MANUAL) ||
                           (machine.getCurrentState() == &AUTO_IDLE) ||
                           (machine.getCurrentState() == &AUTO_STEP)) && !safetyLatched;
  const bool condFast = FAST_1S.sample(now) && !X(X0) && autoReq;
  const bool condSlow = SLOW_2S.sample(now) && safetyLatched;
  Y(Y15, condSteady || condFast || condSlow);

  // Alarm Y12: ON while any core out was ON in last 5s
  const bool anyCore = y4 || y5 || y10 || y11;
  if (anyCore) {
    alarmHoldUntil = now + 5000UL;
    Y(Y12, true);
  } else {
    Y(Y12, now < alarmHoldUntil);
  }
}

// ---------------- State callbacks (logging only; logic is in computeOutputs) ----------------
void onEnter_STOPPED()  { Log.notice(F("[FSM] Enter STOPPED" CR)); }
void onUpdate_STOPPED() {}
void onExit_STOPPED()   {}

void onEnter_MANUAL()   { Log.notice(F("[FSM] Enter MANUAL" CR)); }
void onUpdate_MANUAL()  {}
void onExit_MANUAL()    {}

void onEnter_AUTO_IDLE(){ Log.notice(F("[FSM] Enter AUTO_IDLE" CR)); }
void onUpdate_AUTO_IDLE(){}
void onExit_AUTO_IDLE() {}

void onEnter_AUTO_STEP(){ Log.notice(F("[FSM] Enter AUTO_STEP" CR)); }
void onUpdate_AUTO_STEP(){}
void onExit_AUTO_STEP() {}

void onEnter_SAFETY()   { Log.notice(F("[FSM] Enter SAFETY_LATCHED" CR)); }
void onUpdate_SAFETY()  {}
void onExit_SAFETY()    {}

void onEnter_ESTOP()    { Log.error(F("[FSM] Enter ESTOP" CR)); }
void onUpdate_ESTOP()   {}
void onExit_ESTOP()     {}

// ---------------- Logic tick (scheduled) ----------------
void taskLogicTickCB() {
  const uint32_t now = hwNowMs();
  machine.update();
  evaluateTransitions(now);
  computeOutputs(now);
  applyOutputs();
}

// ---------------- Arduino setup/loop ----------------
void setup() {
  // Hardware timer TIM2 -> 1ms system tick.
  g_hwTimer = new HardwareTimer(TIM2);
  g_hwTimer->setPrescaleFactor(84);
  g_hwTimer->setOverflow(1000, TICK_FORMAT);
  g_hwTimer->attachInterrupt(onHardwareTick1ms);
  g_hwTimer->resume();

  // Serial+Log
  Serial.begin(115200);
  while (!Serial) {}
  Log.begin(LOG_LEVEL_NOTICE, &Serial);
  Log.notice(F("BlackPill F411CE FSM + libs starting..." CR));

  // IO
  for (uint8_t i = 0; i < X_COUNT; ++i) {
    if (X_PIN[i] >= 0) {
      pinMode(X_PIN[i], INPUT);
      deb[i].attach(X_PIN[i], INPUT);
      deb[i].interval(DEBOUNCE_MS);
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
      Y_PORT[i] = digitalPinToPort(Y_PIN[i]);
      Y_MASK[i] = digitalPinToBitMask(Y_PIN[i]);
      writeActiveLevel(Y_PIN[i], false);
    } else {
      Y_PORT[i] = nullptr;
      Y_MASK[i] = 0;
    }
  }

  // Fast interrupts for STOP/SAFETY capture.
  if (X_PIN[X16] >= 0) attachInterrupt(digitalPinToInterrupt(X_PIN[X16]), onEStopIrq, CHANGE);
  if (X_PIN[X14] >= 0) attachInterrupt(digitalPinToInterrupt(X_PIN[X14]), onSafetyIrq, CHANGE);

  // FSM initial state
  machine.immediateTransitionTo(STOPPED);

  // Start scheduler (tasks already added with 'true' enable flag)
}

void loop() {
  if (g_estopIrqLatched) fastStopOutputsNow();
  ts.execute();
}
