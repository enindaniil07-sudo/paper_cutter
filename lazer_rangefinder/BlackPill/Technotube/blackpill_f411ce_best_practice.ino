/*
  Black Pill F411CE - MCU-native control program (best-practice style)

  This file intentionally does NOT mirror PLC rung-by-rung code.
  It implements the same machine intent with:
  - explicit state machine
  - debounced inputs
  - non-blocking timing
  - clear separation of input, logic, and output layers
*/

// ---------------- Pin map (edit for your wiring) ----------------
enum XIdx : uint8_t { X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X_COUNT };
enum YIdx : uint8_t { Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8, Y9, Y10, Y11, Y12, Y13, Y14, Y15, Y16, Y17, Y_COUNT };

const int X_PIN[X_COUNT] = {
  PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7,
  -1, -1, PB0, PB1, PB10, PB12, PB13, PB14, PB15
};

const int Y_PIN[Y_COUNT] = {
  PC13, PA8, -1, -1, PA9, PA10, -1, -1, -1, -1, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PB11
};

constexpr bool INPUT_ACTIVE_HIGH = true;
constexpr bool OUTPUT_ACTIVE_HIGH = true;
constexpr uint16_t DEBOUNCE_MS = 20;

// ---------------- Reusable helpers ----------------
struct DebouncedInput {
  bool stable = false;
  bool lastRaw = false;
  uint32_t lastEdgeMs = 0;
  bool rose = false;
  bool fell = false;

  void update(bool raw, uint32_t nowMs) {
    rose = false;
    fell = false;
    if (raw != lastRaw) {
      lastRaw = raw;
      lastEdgeMs = nowMs;
    }
    if ((nowMs - lastEdgeMs) >= DEBOUNCE_MS && stable != raw) {
      stable = raw;
      rose = stable;
      fell = !stable;
    }
  }
};

struct Pulse {
  uint32_t onMs = 0;
  uint32_t offMs = 0;
  bool sample(uint32_t nowMs) const {
    const uint32_t period = onMs + offMs;
    if (period == 0) return false;
    return (nowMs % period) < onMs;
  }
};

struct DelayOffLatch {
  bool out = false;
  bool timing = false;
  uint32_t startedAt = 0;
  uint32_t offDelayMs = 500;

  void update(bool requestOn, uint32_t nowMs) {
    if (requestOn) {
      out = true;
      timing = false;
      return;
    }
    if (!out) return;
    if (!timing) {
      timing = true;
      startedAt = nowMs;
    }
    if ((nowMs - startedAt) >= offDelayMs) {
      out = false;
      timing = false;
    }
  }
};

enum class Mode : uint8_t {
  Stopped,
  Manual,
  Auto
};

// ---------------- Controller ----------------
class Controller {
public:
  void begin() {
    for (uint8_t i = 0; i < X_COUNT; ++i) {
      if (X_PIN[i] >= 0) pinMode(X_PIN[i], INPUT);
    }
    for (uint8_t i = 0; i < Y_COUNT; ++i) {
      if (Y_PIN[i] >= 0) {
        pinMode(Y_PIN[i], OUTPUT);
        writeY(static_cast<YIdx>(i), false);
      }
    }
    mainEnable.offDelayMs = 500;
  }

  void tick() {
    const uint32_t now = millis();
    sampleInputs(now);
    runStateMachine(now);
    computeOutputs(now);
    applyOutputs();
  }

private:
  DebouncedInput x[X_COUNT];
  uint32_t xBits = 0;
  uint32_t yBits = 0;

  Mode mode = Mode::Stopped;
  bool safetyLatched = false;
  bool stepMode = false;
  uint32_t anyCoreOutOnAt = 0;
  bool heartbeat2s = false;
  bool pulse400ms = false;

  DelayOffLatch mainEnable;
  Pulse plcM1013{500, 500}; // Equivalent behavioral blink.
  Pulse plcM10{1000, 1000}; // Equivalent behavioral blink.

  bool readRawX(XIdx idx) const {
    const int pin = X_PIN[idx];
    if (pin < 0) return false;
    const bool raw = digitalRead(pin) == HIGH;
    return INPUT_ACTIVE_HIGH ? raw : !raw;
  }

  void writeY(YIdx idx, bool value) const {
    const int pin = Y_PIN[idx];
    if (pin < 0) return;
    const bool level = OUTPUT_ACTIVE_HIGH ? value : !value;
    digitalWrite(pin, level ? HIGH : LOW);
  }

  bool isX(XIdx idx) const { return ((xBits >> idx) & 1U) != 0U; }
  bool getY(YIdx idx) const { return ((yBits >> idx) & 1U) != 0U; }

  void setY(YIdx idx, bool value) {
    const uint32_t mask = (1UL << idx);
    if (value) {
      yBits |= mask;
    } else {
      yBits &= ~mask;
    }
  }

  void sampleInputs(uint32_t now) {
    xBits = 0;
    for (uint8_t i = 0; i < X_COUNT; ++i) {
      x[i].update(readRawX(static_cast<XIdx>(i)), now);
      if (x[i].stable) {
        xBits |= (1UL << i);
      }
    }
  }

  void runStateMachine(uint32_t now) {
    const bool autoReq = isX(X5);
    const bool manReq = isX(X6);
    const bool estop = isX(X16);

    // Clear safety latch only after explicit reset condition.
    if (!isX(X14) && !estop) safetyLatched = false;
    if (isX(X14)) safetyLatched = true;

    if (estop || (!autoReq && !manReq)) {
      mode = Mode::Stopped;
    } else if (manReq) {
      mode = Mode::Manual;
    } else {
      mode = Mode::Auto;
    }

    // Main enable with delayed drop-out.
    const bool mainRequest = ((isX(X0) && autoReq) || manReq) && !safetyLatched;
    mainEnable.update(mainRequest, now);

    // Auto step arming and cancellation.
    const bool armStep = isX(X0) && autoReq && isX(X15) && isX(X13) && mainEnable.out;
    if (armStep) stepMode = true;
    if (estop || !mainEnable.out || !autoReq) stepMode = false;

    // 400ms pulse train when step starts or command X12 in step mode.
    static bool prevMain = false;
    static bool prevSafety = false;
    const bool fallMain = prevMain && !mainEnable.out;
    const bool riseSafety = !prevSafety && safetyLatched;
    prevMain = mainEnable.out;
    prevSafety = safetyLatched;
    pulse400ms = (isX(X12) && stepMode) || fallMain || riseSafety;

    // 2s heartbeat equivalent of PLC T10/T11 sequence.
    heartbeat2s = plcM10.sample(now);
  }

  void computeOutputs(uint32_t now) {
    yBits = 0;
    const bool autoReq = isX(X5);
    const bool manReq = isX(X6);
    const bool estop = isX(X16);

    // Core process outputs.
    const bool routeA = (isX(X1) && manReq) || (autoReq && stepMode);
    const bool routeB = (isX(X2) && manReq) || (autoReq && !stepMode);

    const bool y4 = routeA && !isX(X11) && !estop;
    const bool y5 = routeB && !isX(X10) && !isX(X15);
    const bool y10 = ((autoReq || manReq) && isX(X3)) || pulse400ms;
    const bool y11 = (manReq && isX(X4)) || (stepMode && !estop);

    setY(Y0, mainEnable.out);
    setY(Y1, isX(X7) && (mode != Mode::Stopped));
    setY(Y4, y4);
    setY(Y5, y5);
    setY(Y10, y10);
    setY(Y11, y11);

    // Status and combined indicators.
    const bool m150Like = routeA && isX(X11) && !estop;
    const bool m21Like = routeB && isX(X10) && !isX(X15);
    setY(Y16, m150Like || m21Like);
    setY(Y17, m150Like || y4);
    setY(Y13, stepMode);
    setY(Y14, safetyLatched);

    // Multi-condition status blink for Y15.
    const bool condSteady = (mode != Mode::Stopped) && !safetyLatched;
    const bool condFastBlink = plcM1013.sample(now) && !isX(X0) && autoReq;
    const bool condSlowBlink = heartbeat2s && safetyLatched;
    setY(Y15, condSteady || condFastBlink || condSlowBlink);

    // Alarm logic: if all core outputs OFF for 5s -> alarm OFF, else ON.
    const bool anyCoreOn = y4 || y5 || y10 || y11;
    if (anyCoreOn) {
      anyCoreOutOnAt = now;
      setY(Y12, true);
    } else if ((now - anyCoreOutOnAt) >= 5000UL) {
      setY(Y12, false);
    }
  }

  void applyOutputs() {
    for (uint8_t i = 0; i < Y_COUNT; ++i) {
      writeY(static_cast<YIdx>(i), ((yBits >> i) & 1U) != 0U);
    }
  }
};

Controller controller;

void setup() {
  controller.begin();
}

void loop() {
  controller.tick();
}
