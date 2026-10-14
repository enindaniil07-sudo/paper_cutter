/*
  Arduino Nano — D3: square wave out (wire D3 → D2); frequency 0–1 Hz from pot on A0.
  D2: measure periods into a buffer; D9: output f_in / OUT_FREQ_DIV (÷3). D8: optional monitor of D9.

  Input sampling is decoupled from output:
  - D2: rising edges push into a rolling buffer; D8: only last interval kept.
  - Each loop() computes D2 average from the buffer and updates Timer1 / SW out
    when the average changes — output timing is not tied to D2 edge instants.
  - Serial 115200: Hz only — D2 uses buffer average; D8 uses last interval (no average).

  Stall: no D2 rising edge for longer than max(NO_INPUT_TIMEOUT_US, 3× current
  buffer average period) → clear buffer and stop D9 (stale timer off).

  D3 = generator out (A0 pot → 0–1 Hz), D2 = in (connect D3 to D2), D8 = monitor in, D9 = OC1A out.

  setup() first clears internal pull-ups on all digital pins (INPUT, not
  INPUT_PULLUP), then configures D2/D3/D8/D9.
*/

const uint8_t PIN_D3_GEN = 3;
const uint8_t PIN_D3_FREQ_ADC = A0;
const uint8_t PIN_IN = 2;
const uint8_t PIN_MON = 8;
const uint8_t PIN_PWM = 9;

// Fewer samples → faster lock-in at very low f_in (10 periods at 0.1 Hz = 100 s).
static const uint8_t PERIOD_BUF_LEN = 6;
static const uint32_t SERIAL_LOG_MS = 250;
// Silence on D2 before stall when a period average exists (3×T_avg usually dominates).
static const uint32_t NO_INPUT_TIMEOUT_US = 1000000UL;
// Only one D2 edge seen so far (no interval in buffer yet): allow long gaps for low f_in.
static const uint32_t NO_INPUT_WAIT_FIRST_PERIOD_US = 120000000UL; // 120 s → down to ~0.008 Hz edge pair

// f_out = f_in / OUT_FREQ_DIV (÷3 ⇒ D9 at one third of measured D2 frequency)
static const uint8_t OUT_FREQ_DIV = 3;

// Pot 0–3.3 V on default 5 V AREF → ~0..675 ADC; map that span to 0–1 Hz.
// Use 1023 here if the pot uses the full 0–5 V range to 1 Hz instead.
static const uint16_t D3_ADC_FULL = 675;

static uint32_t d3HalfPeriodUs = 0;

static uint32_t lastRiseUs = 0;
static bool prevHighIn = false;

static uint32_t periodBuf[PERIOD_BUF_LEN];
static uint8_t bufCount = 0;
static uint8_t bufNext = 0;

static uint32_t lastRiseMonUs = 0;
static bool prevHighMon = false;
// Last D8 rising-to-rising interval (µs); direct measurement, not averaged
static uint32_t lastD8PeriodUs = 0;

static uint32_t lastAvgPeriodUs = 0;
static uint32_t lastAppliedAvgPeriodUs = 0xFFFFFFFFUL;
static uint32_t lastLogMs = 0;

static bool useSoftwareOut = false;
static uint32_t nextToggleUs = 0;
static bool softOutHigh = false;

static uint32_t nextD3ToggleUs = 0;
static bool d3GenHigh = false;

// f in millihertz 0..1000 (0–1 Hz); half-period (µs) = 1e6/(2*f) = 5e8/f_mHz
static uint32_t d3HalfPeriodUsFromAdc(uint16_t adc) {
  if (adc == 0U) {
    return 0U;
  }
  uint32_t f_mHz = (uint32_t)adc * 1000UL / (uint32_t)D3_ADC_FULL;
  if (f_mHz > 1000UL) {
    f_mHz = 1000UL;
  }
  if (f_mHz == 0U) {
    return 0U;
  }
  return 500000000UL / f_mHz;
}

static uint32_t ringPeriodSum(const uint32_t* buf, uint8_t bufNext, uint8_t count) {
  uint32_t s = 0;
  if (count == 0) {
    return 0;
  }
  if (count < PERIOD_BUF_LEN) {
    for (uint8_t i = 0; i < count; i++) {
      s += buf[i];
    }
  } else {
    for (uint8_t i = 0; i < PERIOD_BUF_LEN; i++) {
      s += buf[(uint8_t)((bufNext + i) % PERIOD_BUF_LEN)];
    }
  }
  return s;
}

static uint32_t averageD2PeriodUs() {
  if (bufCount == 0) {
    return 0;
  }
  return ringPeriodSum(periodBuf, bufNext, bufCount) / (uint32_t)bufCount;
}

// Only reprogram output when average period moved enough; otherwise every D2 edge
// changes the rolling mean slightly → apply every edge → D8 resets before two rises.
static bool shouldApplyAvgPeriod(uint32_t avgUs, uint32_t lastAppliedUs) {
  if (avgUs == 0U) {
    return lastAppliedUs != 0U;
  }
  if (lastAppliedUs == 0U || lastAppliedUs == 0xFFFFFFFFUL) {
    return true;
  }
  if (avgUs == lastAppliedUs) {
    return false;
  }
  const uint32_t lo = (avgUs < lastAppliedUs) ? avgUs : lastAppliedUs;
  const uint32_t hi = (avgUs < lastAppliedUs) ? lastAppliedUs : avgUs;
  const uint32_t diff = hi - lo;
  // ~0.4% + 20 µs; for long periods add ~0.2% so pot/jitter does not re-apply every edge
  const uint32_t thresh = (lo / 256U) + (lo / 512U) + 50U;
  return diff > thresh;
}

// Half-period of D9 square wave (µs): T_out/2 = (T_in * OUT_FREQ_DIV) / 2
static uint32_t outputHalfPeriodUs(uint32_t avgPeriodUs) {
  if (avgPeriodUs == 0U) {
    return 0U;
  }
  return (uint32_t)(((uint64_t)avgPeriodUs * (uint64_t)OUT_FREQ_DIV + 1ULL) / 2ULL);
}

// AVR: INPUT = hi-Z, internal pull-up off (not INPUT_PULLUP)
static void disableInternalPullupsAllPins() {
  for (uint8_t p = 0; p <= 19; p++) {
    pinMode(p, INPUT);
  }
}

static void timer1Off() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 0;
  TIMSK1 = 0;
  TIFR1 = 0xFF;
  pinMode(PIN_PWM, OUTPUT);
}

// Actual OC1A toggle frequency from Timer1 (CTC): f = F_CPU / (2 * prescale * (OCR1A+1))
static float timer1HwToggleHz() {
  const uint8_t cs = TCCR1B & 0x07;
  if (cs == 0) {
    return 0.0f;
  }
  uint16_t prescale;
  switch (cs) {
    case 1:
      prescale = 1;
      break;
    case 2:
      prescale = 8;
      break;
    case 3:
      prescale = 64;
      break;
    case 4:
      prescale = 256;
      break;
    case 5:
      prescale = 1024;
      break;
    default:
      return 0.0f;
  }
  const uint32_t ocrp1 = (uint32_t)OCR1A + 1UL;
  return (float)F_CPU / (2.0f * (float)prescale * (float)ocrp1);
}

static void applyOutputFromAvgPeriodUs(uint32_t avgPeriodUs) {
  lastAvgPeriodUs = avgPeriodUs;
  lastAppliedAvgPeriodUs = avgPeriodUs;

  if (avgPeriodUs == 0) {
    // No output: drop D8 sample so Serial does not show a stale freq
    lastD8PeriodUs = 0;
    lastRiseMonUs = 0;
    prevHighMon = digitalRead(PIN_MON) == HIGH;
    timer1Off();
    digitalWrite(PIN_PWM, LOW);
    useSoftwareOut = false;
    return;
  }

  // Do not clear D8 here on every retune: at low f_out, D9 rises only every T_out
  // (seconds). apply() can run much more often (D2 edges / hysteresis), which
  // would reset D8 before two monitor edges and make d8Freq_Hz vanish.

  static const uint16_t kPrescale[] = { 1, 8, 64, 256, 1024 };
  static const uint8_t kCsBits[] = {
    (1 << CS10),
    (1 << CS11),
    (1 << CS11) | (1 << CS10),
    (1 << CS12),
    (1 << CS12) | (1 << CS10),
  };

  for (uint8_t i = 0; i < 5; i++) {
    const uint32_t ps = kPrescale[i];
    // OCR+1 ticks per half-period = F_CPU*T_in*OUT/(2*1e6*ps) — works for odd OUT (e.g. 3)
    uint64_t ocr1 = (uint64_t)F_CPU * (uint64_t)avgPeriodUs * (uint64_t)OUT_FREQ_DIV;
    ocr1 /= 2000000ULL;
    ocr1 /= (uint64_t)ps;
    if (ocr1 == 0 || ocr1 > 65536ULL) {
      continue;
    }

    const uint16_t ocr = (uint16_t)(ocr1 - 1ULL);

    timer1Off();
    TCNT1 = 0;
    OCR1A = ocr;
    TCCR1A = (1 << COM1A0);
    TCCR1B = (1 << WGM12) | kCsBits[i];
    useSoftwareOut = false;
    return;
  }

  timer1Off();
  useSoftwareOut = true;
  softOutHigh = false;
  nextToggleUs = micros();
  digitalWrite(PIN_PWM, LOW);
}

void setup() {
  disableInternalPullupsAllPins();

  pinMode(PIN_D3_GEN, OUTPUT);
  digitalWrite(PIN_D3_GEN, LOW);
  d3GenHigh = false;
  nextD3ToggleUs = micros();
  d3HalfPeriodUs = d3HalfPeriodUsFromAdc((uint16_t)analogRead(PIN_D3_FREQ_ADC));

  pinMode(PIN_IN, INPUT);
  pinMode(PIN_MON, INPUT);
  pinMode(PIN_PWM, OUTPUT);
  digitalWrite(PIN_PWM, LOW);
  Serial.begin(115200);
  prevHighIn = digitalRead(PIN_IN) == HIGH;
  prevHighMon = digitalRead(PIN_MON) == HIGH;
  lastLogMs = millis();
}

void loop() {
  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();

  // D3 → D2: frequency 0–1 Hz from A0 (0 Hz = D3 held LOW)
  const uint16_t d3Adc = (uint16_t)analogRead(PIN_D3_FREQ_ADC);
  const uint32_t newHalf = d3HalfPeriodUsFromAdc(d3Adc);
  if (newHalf != d3HalfPeriodUs) {
    d3HalfPeriodUs = newHalf;
    nextD3ToggleUs = nowUs;
    if (d3HalfPeriodUs == 0U) {
      d3GenHigh = false;
      digitalWrite(PIN_D3_GEN, LOW);
    }
  }
  if (d3HalfPeriodUs > 0U) {
    for (uint8_t k = 0; k < 24U && (uint32_t)(nowUs - nextD3ToggleUs) >= d3HalfPeriodUs; k++) {
      d3GenHigh = !d3GenHigh;
      digitalWrite(PIN_D3_GEN, d3GenHigh ? HIGH : LOW);
      nextD3ToggleUs += d3HalfPeriodUs;
    }
  }

  const bool highIn = digitalRead(PIN_IN) == HIGH;
  const bool risingIn = highIn && !prevHighIn;
  prevHighIn = highIn;

  const bool highMon = digitalRead(PIN_MON) == HIGH;
  const bool risingMon = highMon && !prevHighMon;
  prevHighMon = highMon;

  // D8: last rising-to-rising interval only (direct, for Serial d8Freq_Hz)
  if (risingMon) {
    if (lastRiseMonUs != 0) {
      lastD8PeriodUs = (uint32_t)(nowUs - lastRiseMonUs);
    }
    lastRiseMonUs = nowUs;
  }

  // D2: record periods only (does not trigger output update here)
  if (risingIn) {
    if (lastRiseUs != 0) {
      const uint32_t periodUs = (uint32_t)(nowUs - lastRiseUs);
      periodBuf[bufNext] = periodUs;
      bufNext = (uint8_t)((bufNext + 1U) % PERIOD_BUF_LEN);
      if (bufCount < PERIOD_BUF_LEN) {
        bufCount++;
      }
    }
    lastRiseUs = nowUs;
  }

  const uint32_t avgPeriodUs = averageD2PeriodUs();

  // No new D2 edges: input gone → drop stale average and silence output.
  uint32_t stallUs = NO_INPUT_TIMEOUT_US;
  if (avgPeriodUs > 0) {
    const uint32_t dyn = avgPeriodUs * 3UL;
    if (dyn > stallUs) {
      stallUs = dyn;
    }
  } else if (lastRiseUs != 0 && bufCount == 0) {
    // Only one rising edge so far — no measured period yet. Default 1 s would stall
    // before the second edge when f_in < ~1 Hz and clear D2 state forever.
    stallUs = NO_INPUT_WAIT_FIRST_PERIOD_US;
  }
  if (lastRiseUs != 0 && (uint32_t)(nowUs - lastRiseUs) > stallUs) {
    bufCount = 0;
    bufNext = 0;
    lastRiseUs = 0;
    if (lastAppliedAvgPeriodUs != 0) {
      applyOutputFromAvgPeriodUs(0);
    }
  }

  // Output from latest D2 average — re-read after stall may have cleared buffer
  const uint32_t avgNow = averageD2PeriodUs();
  if (shouldApplyAvgPeriod(avgNow, lastAppliedAvgPeriodUs)) {
    applyOutputFromAvgPeriodUs(avgNow);
  }

  if (useSoftwareOut && lastAvgPeriodUs >= 4) {
    const uint32_t stepUs = outputHalfPeriodUs(lastAvgPeriodUs);
    uint32_t t = micros();
    for (uint8_t k = 0; k < 24U && (uint32_t)(t - nextToggleUs) >= stepUs; k++) {
      softOutHigh = !softOutHigh;
      digitalWrite(PIN_PWM, softOutHigh ? HIGH : LOW);
      nextToggleUs += stepUs;
      t = micros();
    }
  }

  if ((uint32_t)(nowMs - lastLogMs) >= SERIAL_LOG_MS) {
    lastLogMs = nowMs;

    const uint32_t logUs = micros();
    const uint32_t ap2 = averageD2PeriodUs();
    bool any = false;
    float hwHz = 0.0f;

    if (ap2 > 0) {
      const float f_in = 1000000.0f / (float)ap2;
      Serial.print(F("avgFreq_Hz="));
      Serial.print(f_in, 3);
      Serial.print(F("  outFreq_Hz="));
      Serial.print(f_in / (float)OUT_FREQ_DIV, 3);
      // Measured from Timer1 registers (OC1A toggle); compare to d8Freq_Hz when D8→D9
      if (!useSoftwareOut) {
        hwHz = timer1HwToggleHz();
        if (hwHz > 0.0f) {
          Serial.print(F("  hwOutFreq_Hz="));
          Serial.print(hwHz, 3);
        }
      }
      any = true;
    }
    // Direct: last D8 interval; hide if no edge for ~3 ref periods. ref = max(measured,
    // expected T_out from buffer, timer) so d8 does not vanish when the pot slows D9 but
    // lastD8PeriodUs is still from a faster rate (hysteresis delayed apply).
    if (lastD8PeriodUs > 0 && lastRiseMonUs != 0) {
      const uint32_t sinceRise = (uint32_t)(logUs - lastRiseMonUs);
      uint32_t refPeriodUs = lastD8PeriodUs;
      if (ap2 > 0) {
        const uint32_t expOut = ap2 * (uint32_t)OUT_FREQ_DIV;
        if (expOut > refPeriodUs) {
          refPeriodUs = expOut;
        }
      }
      if (!useSoftwareOut && hwHz > 0.0f) {
        const uint32_t tHwUs = (uint32_t)(1000000.0f / hwHz + 0.5f);
        if (tHwUs > refPeriodUs) {
          refPeriodUs = tHwUs;
        }
      }
      const uint32_t staleLim = refPeriodUs * 3UL + 500000UL;
      if (sinceRise <= staleLim) {
        const float d8Hz = 1000000.0f / (float)lastD8PeriodUs;
        float refOutHz = 0.0f;
        if (!useSoftwareOut && hwHz > 0.0f) {
          refOutHz = hwHz;
        } else if (ap2 > 0) {
          refOutHz = (1000000.0f / (float)ap2) / (float)OUT_FREQ_DIV;
        }
        bool showD8 = true;
        if (refOutHz > 0.0f) {
          const float err = (d8Hz > refOutHz) ? (d8Hz - refOutHz) : (refOutHz - d8Hz);
          // Last D8 interval can lag a retuned D9 (e.g. d8 0.333 while hw 0.300) — hide until it tracks
          if (err > refOutHz * 0.10f) {
            showD8 = false;
          }
        }
        if (showD8) {
          if (any) {
            Serial.print(F("  "));
          }
          Serial.print(F("d8Freq_Hz="));
          Serial.print(d8Hz, 3);
          any = true;
        }
      }
    }
    if (any) {
      Serial.println();
    }
  }
}
