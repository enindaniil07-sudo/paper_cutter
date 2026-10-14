/*
  Black Pill F411CE port of the PLC logic in:
  - Technotube/plc_prog.csv
  - Technotube/techtube_dvp-32es_0/MAXIMUM_SIMPLIFIED.csv

  This sketch keeps PLC-style internal relays (M*) and timers (T*),
  then computes Y outputs each scan.
*/

// ---------- Pin map (edit for your wiring) ----------
// Set any unused signal to -1.
// X inputs used: X0..X7, X10..X16
const int X_PIN[17] = {
  PA0, // X0
  PA1, // X1
  PA2, // X2
  PA3, // X3
  PA4, // X4
  PA5, // X5
  PA6, // X6
  PA7, // X7
  -1,  // X8 (unused)
  -1,  // X9 (unused)
  PB0, // X10
  PB1, // X11
  PB10, // X12
  PB12, // X13
  PB13, // X14
  PB14, // X15
  PB15  // X16
};

// Y outputs used: Y0, Y1, Y4, Y5, Y10..Y17
const int Y_PIN[18] = {
  PC13, // Y0 (onboard LED on many Black Pill boards)
  PA8,  // Y1
  -1,   // Y2 (unused)
  -1,   // Y3 (unused)
  PA9,  // Y4
  PA10, // Y5
  -1,   // Y6 (unused)
  -1,   // Y7 (unused)
  -1,   // Y8 (unused)
  -1,   // Y9 (unused)
  PB3,  // Y10
  PB4,  // Y11
  PB5,  // Y12
  PB6,  // Y13
  PB7,  // Y14
  PB8,  // Y15
  PB9,  // Y16
  PB11  // Y17
};

// Adjust to match field wiring.
const bool INPUT_ACTIVE_HIGH = true;
const bool OUTPUT_ACTIVE_HIGH = true;

// ---------- PLC timing model ----------
// Delta DVP T0..T63 default base in this project is 100 ms.
const uint32_t PLC_TIMER_BASE_MS = 100;

// TON timer helper (done bit becomes true after preset while rung is true).
struct TonTimer {
  bool in = false;
  bool done = false;
  uint32_t presetMs = 0;
  uint32_t startMs = 0;

  void update(bool rung, uint32_t nowMs) {
    if (rung) {
      if (!in) {
        in = true;
        startMs = nowMs;
      }
      done = (nowMs - startMs) >= presetMs;
    } else {
      in = false;
      done = false;
      startMs = 0;
    }
  }

  void reset() {
    in = false;
    done = false;
    startMs = 0;
  }
};

// ---------- Inputs / outputs ----------
bool X[17] = {false};
bool Y[18] = {false};

// ---------- Internal relays ----------
bool M0 = false;
bool M5 = false;
bool M10 = false;
bool M21 = false;
bool M50 = false;
bool M51 = false;
bool M52 = false;
bool M53 = false;
bool M54 = false;
bool M100 = false;
bool M150 = false;
bool M200 = false;
bool M201 = false;

// Special relays.
bool M1000 = true; // Always ON while in RUN.
bool M1013 = false; // 0.5s ON / 0.5s OFF clock.

// Previous scan values for edge detection.
bool prevM0 = false;
bool prevM5 = false;

// Timers.
TonTimer T1;   // K5  = 500 ms
TonTimer T10;  // K10 = 1.0 s
TonTimer T11;  // K20 = 2.0 s
TonTimer T43;  // K50 = 5.0 s
TonTimer T44;  // K4  = 400 ms

uint32_t scanNowMs = 0;

bool readInput(int idx) {
  const int pin = X_PIN[idx];
  if (pin < 0) return false;
  const bool raw = (digitalRead(pin) == HIGH);
  return INPUT_ACTIVE_HIGH ? raw : !raw;
}

void writeOutput(int idx, bool state) {
  const int pin = Y_PIN[idx];
  if (pin < 0) return;
  const bool level = OUTPUT_ACTIVE_HIGH ? state : !state;
  digitalWrite(pin, level ? HIGH : LOW);
}

void updateM1013(uint32_t nowMs) {
  // 1.0s period, 50% duty: ON for first 500ms.
  M1013 = (nowMs % 1000UL) < 500UL;
}

void scanInputs() {
  for (int i = 0; i < 17; ++i) {
    X[i] = readInput(i);
  }
}

void computeLogic() {
  // Rung 1: M0 set/rst logic (placed first to support ORF usage this scan).
  if (X[5] || X[6]) M0 = true;
  if (!X[5] && !X[6]) M0 = false;

  // Rung 2: M5 set/rst logic.
  if (X[14]) M5 = true;
  if (!M0) M5 = false;

  // Edge helpers (PLC ORF/ORP style).
  const bool ORF_M0 = prevM0 && !M0; // Falling edge of M0.
  const bool ORP_M5 = !prevM5 && M5; // Rising edge of M5.

  // Rung 3: Main control M100, T1, Y0 self-hold.
  M100 = ((X[0] && X[5]) || X[6]) && !M5;
  T1.update(!M100, scanNowMs); // LDI M100 -> TMR T1
  Y[0] = (M100 || Y[0]) && !T1.done;

  // Rung 4: Safety monitoring -> M50 / Y12.
  T43.update(!Y[4] && !Y[5] && !Y[10] && !Y[11], scanNowMs);
  if (Y[4] || Y[5] || Y[10] || Y[11]) M50 = true; // SET
  if (T43.done) M50 = false;                       // RST
  Y[12] = M50;

  // Rung 5: Mode control M51.
  if (X[0] && X[5] && X[15] && X[13] && Y[0]) M51 = true; // SET
  if (X[16] || !Y[0] || !X[5]) M51 = false;                // RST

  // Rung 6: Pulse generation M52/M53/M54 with edge terms.
  M52 = (X[12] && M51) || ORF_M0 || ORP_M5 || (M52 && !T44.done);
  T44.update(M52, scanNowMs);
  M53 = M52 && !T44.done;
  if (M53) M54 = true;             // SET
  if (X[16] || !X[5]) M54 = false; // RST

  // Rung 7-10: Main process outputs.
  Y[11] = (X[6] && X[4]) || (M51 && !M54);
  Y[10] = (M0 && X[3]) || M53;

  M200 = (X[1] && X[6]) || (X[5] && M51);
  Y[4] = M200 && !X[11] && !X[16];
  M150 = M200 && X[11] && !X[16];

  M201 = (X[2] && X[6]) || (X[5] && !M51);
  Y[5] = M201 && !X[10] && !X[15];
  M21 = M201 && X[10] && !X[15];

  // Rung 11: Status outputs.
  Y[16] = M150 || M21;
  Y[17] = M150 || Y[4];
  Y[13] = M51;

  // Rung 12: Y14 and Y1.
  Y[14] = M5;
  Y[1] = X[7] && M0;

  // Rung 13: M1000/T10/T11/M10 pulse generator.
  T10.update(M1000, scanNowMs);
  T11.update(M1000, scanNowMs);
  M10 = M1000 && !T10.done;
  if (M1000 && T11.done) {
    // ZRST T10,T11
    T10.reset();
    T11.reset();
  }

  // Rung 14: Y15 complex status logic.
  Y[15] = (M0 && !M5) || (M1013 && !X[0] && X[5]) || (M10 && M5);

  // Save edges for next scan.
  prevM0 = M0;
  prevM5 = M5;
}

void applyOutputs() {
  for (int i = 0; i < 18; ++i) {
    writeOutput(i, Y[i]);
  }
}

void setup() {
  // Timer presets from PLC K constants.
  T1.presetMs = 5 * PLC_TIMER_BASE_MS;
  T10.presetMs = 10 * PLC_TIMER_BASE_MS;
  T11.presetMs = 20 * PLC_TIMER_BASE_MS;
  T43.presetMs = 50 * PLC_TIMER_BASE_MS;
  T44.presetMs = 4 * PLC_TIMER_BASE_MS;

  for (int i = 0; i < 17; ++i) {
    if (X_PIN[i] >= 0) pinMode(X_PIN[i], INPUT);
  }
  for (int i = 0; i < 18; ++i) {
    if (Y_PIN[i] >= 0) {
      pinMode(Y_PIN[i], OUTPUT);
      writeOutput(i, false);
    }
  }
}

void loop() {
  scanNowMs = millis();
  updateM1013(scanNowMs);
  scanInputs();
  computeLogic();
  applyOutputs();
}
