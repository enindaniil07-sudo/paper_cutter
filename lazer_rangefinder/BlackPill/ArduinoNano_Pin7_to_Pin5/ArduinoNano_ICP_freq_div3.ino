/*
  Arduino Nano — Timer1 Input Capture (ICP1) on D4 for period measurement; D9 ÷3 via software.

  Why this file exists:
  - ICP latches TCNT1 on a hardware edge → period timing without loop() jitter on the edge.
  - Timer1 cannot simultaneously run this ICP (normal counter + capture) and OC1A CTC on D9,
    so D9 is always software-toggled (same math as the SW fallback in the other sketch).

  Wiring:
  - D3: 0–1 Hz square from pot A0 — turn pot up from zero or D3 stays LOW (no edges).
  - D4: INPUT — ICP1 (NOT D2). Jumper D3 → D4 for self-test. If no edges ~4 s, sketch
    falls back to polled D4 (same timing as main sketch) so you still get output.
  - D8: optional monitor of D9.
  - A0: frequency pot; D3_ADC_FULL = 675 for ~0–3.3 V on 5 V AREF (1023 for 0–5 V).

  Serial 115200, 250 ms: avgFreq_Hz, outFreq_Hz, effOutFreq_Hz (from applied ÷3 period), d8Freq_Hz.
*/

const uint8_t PIN_D3_GEN = 3;
const uint8_t PIN_D3_FREQ_ADC = A0;
// ICP1 is PD4 = Arduino D4 (not D2)
const uint8_t PIN_ICP_IN = 4;
const uint8_t PIN_MON = 8;
const uint8_t PIN_PWM = 9;

static const uint8_t PERIOD_BUF_LEN = 6;
static const uint32_t SERIAL_LOG_MS = 250;
static const uint32_t NO_INPUT_TIMEOUT_US = 1000000UL;
static const uint32_t NO_INPUT_WAIT_FIRST_PERIOD_US = 120000000UL;

static const uint8_t OUT_FREQ_DIV = 3;

static const uint16_t D3_ADC_FULL = 675;

// Timer1 prescaler for ICP (must match ISRs)
static const uint16_t T1_PRESCALE = 1024;
// Period in µs: delta_ticks * (T1_PRESCALE * 1e6) / F_CPU = delta_ticks * 1024 / 16 = delta_ticks * 64
static const uint32_t T1_TICK_TO_US_MUL = 64UL;

static uint32_t d3HalfPeriodUs = 0;

// ---- ICP state (ISR + main) ---------------------------------------------
volatile uint16_t t1_overflowCount = 0;
volatile uint32_t t1_lastCapTicks32 = 0;
volatile bool t1_haveLastCap = false;

static uint32_t periodBuf[PERIOD_BUF_LEN];
static volatile uint8_t bufCount = 0;
static volatile uint8_t bufNext = 0;  // next write index (same convention as polled sketch)

static uint32_t lastRiseMonUs = 0;
static bool prevHighMon = false;
static uint32_t lastD8PeriodUs = 0;

static uint32_t lastAvgPeriodUs = 0;
static uint32_t lastAppliedAvgPeriodUs = 0xFFFFFFFFUL;
static uint32_t lastLogMs = 0;

static uint32_t nextToggleUs = 0;
static bool softOutHigh = false;

static uint32_t nextD3ToggleUs = 0;
static bool d3GenHigh = false;

static volatile uint32_t lastRiseUs = 0;

// true: Timer1 ICP ISR fills periodBuf. false: polled D4 edges (fallback if ICP gets no edges)
static volatile bool useICPMeasure = true;
static bool pollFallbackAnnounced = false;
static uint32_t bootMs = 0;
static bool prevHighD4Poll = false;
static uint32_t lastRiseD4PollUs = 0;

// f in millihertz 0..1000 (0–1 Hz); half-period µs = 5e8 / f_mHz
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

static uint32_t ringPeriodSum(const uint32_t* buf, uint8_t next, uint8_t count) {
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
      s += buf[(uint8_t)((next + i) % PERIOD_BUF_LEN)];
    }
  }
  return s;
}

static uint32_t averagePeriodUsFromBuf() {
  uint8_t s = SREG;
  cli();
  const uint8_t c = bufCount;
  const uint8_t n = bufNext;
  const uint32_t sum = ringPeriodSum(periodBuf, n, c);
  SREG = s;
  if (c == 0) {
    return 0;
  }
  return sum / (uint32_t)c;
}

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
  const uint32_t thresh = (lo / 256U) + (lo / 512U) + 50U;
  return diff > thresh;
}

static uint32_t outputHalfPeriodUs(uint32_t avgPeriodUs) {
  if (avgPeriodUs == 0U) {
    return 0U;
  }
  return (uint32_t)(((uint64_t)avgPeriodUs * (uint64_t)OUT_FREQ_DIV + 1ULL) / 2ULL);
}

static void disableInternalPullupsAllPins() {
  for (uint8_t p = 0; p <= 19; p++) {
    pinMode(p, INPUT);
  }
}

// Effective D9 toggle frequency from last applied average (software path only)
static float effectiveOutToggleHzFromAvgUs(uint32_t avgUs) {
  if (avgUs == 0U) {
    return 0.0f;
  }
  const float tOutUs = (float)avgUs * (float)OUT_FREQ_DIV;
  return 1000000.0f / tOutUs;
}

static void applyOutputFromAvgPeriodUs(uint32_t avgPeriodUs) {
  lastAvgPeriodUs = avgPeriodUs;
  lastAppliedAvgPeriodUs = avgPeriodUs;

  if (avgPeriodUs == 0) {
    lastD8PeriodUs = 0;
    lastRiseMonUs = 0;
    prevHighMon = digitalRead(PIN_MON) == HIGH;
    digitalWrite(PIN_PWM, LOW);
    return;
  }

  nextToggleUs = micros();
  softOutHigh = false;
  digitalWrite(PIN_PWM, LOW);
}

// ---- Timer1: Input Capture + overflow -----------------------------------

static void timer1InitICP() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TIMSK1 = 0;
  TIFR1 = 0xFF;

  // Normal mode (0), rising edge on ICP1; no ICNC1 — slow edges are friendlier without 4-cycle filter
  TCCR1B = (1 << ICES1) | (1 << CS12) | (1 << CS10);  // prescaler 1024

  t1_overflowCount = 0;
  t1_haveLastCap = false;
  t1_lastCapTicks32 = 0;

  TIMSK1 = (1 << ICIE1) | (1 << TOIE1);
  sei();
}

static void timer1StopICP() {
  TIMSK1 = 0;
  TCCR1B = 0;
  TCCR1A = 0;
  TIFR1 = 0xFF;
}

ISR(TIMER1_OVF_vect) {
  if (useICPMeasure) {
    t1_overflowCount++;
  }
}

// Extended 32-bit tick time (Gammon-style TOV1 check after ICR1 read)
static uint32_t icpExtendedTicksNow() {
  const uint16_t icr = ICR1;
  const uint16_t ovfCopy = t1_overflowCount;
  if ((TIFR1 & (1 << TOV1)) && (icr < 0x8000U)) {
    return ((uint32_t)(ovfCopy + 1U) << 16) | (uint32_t)icr;
  }
  return ((uint32_t)ovfCopy << 16) | (uint32_t)icr;
}

ISR(TIMER1_CAPT_vect) {
  if (!useICPMeasure) {
    return;
  }
  const uint32_t nowTicks = icpExtendedTicksNow();

  if (!t1_haveLastCap) {
    t1_lastCapTicks32 = nowTicks;
    t1_haveLastCap = true;
    lastRiseUs = micros();
    return;
  }

  uint32_t deltaTicks = nowTicks - t1_lastCapTicks32;
  t1_lastCapTicks32 = nowTicks;

  if (deltaTicks == 0U) {
    lastRiseUs = micros();
    return;
  }

  const uint64_t periodUs64 = (uint64_t)deltaTicks * (uint64_t)T1_TICK_TO_US_MUL;
  if (periodUs64 > 0xFFFFFFFFULL) {
    lastRiseUs = micros();
    return;
  }
  const uint32_t periodUs = (uint32_t)periodUs64;

  const uint8_t next = (uint8_t)((bufNext + 1U) % PERIOD_BUF_LEN);
  periodBuf[bufNext] = periodUs;
  bufNext = next;
  if (bufCount < PERIOD_BUF_LEN) {
    bufCount++;
  }
  lastRiseUs = micros();
}

void setup() {
  disableInternalPullupsAllPins();

  pinMode(PIN_D3_GEN, OUTPUT);
  digitalWrite(PIN_D3_GEN, LOW);
  d3GenHigh = false;
  nextD3ToggleUs = micros();
  d3HalfPeriodUs = d3HalfPeriodUsFromAdc((uint16_t)analogRead(PIN_D3_FREQ_ADC));

  pinMode(PIN_ICP_IN, INPUT);
  pinMode(PIN_MON, INPUT);
  pinMode(PIN_PWM, OUTPUT);
  digitalWrite(PIN_PWM, LOW);

  timer1InitICP();

  Serial.begin(115200);
  bootMs = millis();
  Serial.println(F("ICP: wire D3 -> D4 (not D2). Pot > 0 for D3 square wave."));
  prevHighMon = digitalRead(PIN_MON) == HIGH;
  prevHighD4Poll = digitalRead(PIN_ICP_IN) == HIGH;
  lastLogMs = millis();
}

void loop() {
  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();

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

  // No ICP captures after boot: fall back to polled D4 (same as main sketch on D2)
  if (useICPMeasure && bufCount == 0 && (uint32_t)(nowMs - bootMs) >= 4000UL) {
    timer1StopICP();
    useICPMeasure = false;
    t1_haveLastCap = false;
    bufCount = 0;
    bufNext = 0;
    lastRiseUs = 0;
    lastRiseD4PollUs = 0;
    prevHighD4Poll = digitalRead(PIN_ICP_IN) == HIGH;
    if (!pollFallbackAnnounced) {
      pollFallbackAnnounced = true;
      Serial.println(F("ICP: no edges — using polled D4. Check D3->D4 and pot."));
    }
  }

  if (!useICPMeasure) {
    const bool highD4 = digitalRead(PIN_ICP_IN) == HIGH;
    const bool risingD4 = highD4 && !prevHighD4Poll;
    prevHighD4Poll = highD4;
    if (risingD4) {
      if (lastRiseD4PollUs != 0) {
        const uint32_t periodUs = (uint32_t)(nowUs - lastRiseD4PollUs);
        const uint8_t next = (uint8_t)((bufNext + 1U) % PERIOD_BUF_LEN);
        periodBuf[bufNext] = periodUs;
        bufNext = next;
        if (bufCount < PERIOD_BUF_LEN) {
          bufCount++;
        }
      }
      lastRiseD4PollUs = nowUs;
      lastRiseUs = nowUs;
    }
  }

  const bool highMon = digitalRead(PIN_MON) == HIGH;
  const bool risingMon = highMon && !prevHighMon;
  prevHighMon = highMon;

  if (risingMon) {
    if (lastRiseMonUs != 0) {
      lastD8PeriodUs = (uint32_t)(nowUs - lastRiseMonUs);
    }
    lastRiseMonUs = nowUs;
  }

  const uint32_t avgPeriodUs = averagePeriodUsFromBuf();

  uint32_t stallUs = NO_INPUT_TIMEOUT_US;
  if (avgPeriodUs > 0) {
    const uint32_t dyn = avgPeriodUs * 3UL;
    if (dyn > stallUs) {
      stallUs = dyn;
    }
  } else if (lastRiseUs != 0 && bufCount == 0) {
    stallUs = NO_INPUT_WAIT_FIRST_PERIOD_US;
  }

  if (lastRiseUs != 0 && (uint32_t)(nowUs - lastRiseUs) > stallUs) {
    bufCount = 0;
    bufNext = 0;
    lastRiseUs = 0;
    t1_haveLastCap = false;
    if (lastAppliedAvgPeriodUs != 0) {
      applyOutputFromAvgPeriodUs(0);
    }
  }

  const uint32_t avgNow = averagePeriodUsFromBuf();
  if (shouldApplyAvgPeriod(avgNow, lastAppliedAvgPeriodUs)) {
    applyOutputFromAvgPeriodUs(avgNow);
  }

  if (lastAvgPeriodUs >= 4) {
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
    const uint32_t ap2 = averagePeriodUsFromBuf();
    bool any = false;

    if (ap2 > 0) {
      const float f_in = 1000000.0f / (float)ap2;
      Serial.print(F("avgFreq_Hz="));
      Serial.print(f_in, 3);
      Serial.print(F("  outFreq_Hz="));
      Serial.print(f_in / (float)OUT_FREQ_DIV, 3);
      const float eff = effectiveOutToggleHzFromAvgUs(lastAvgPeriodUs);
      if (eff > 0.0f) {
        Serial.print(F("  effOutFreq_Hz="));
        Serial.print(eff, 3);
      }
      any = true;
    }

    if (lastD8PeriodUs > 0 && lastRiseMonUs != 0) {
      const uint32_t sinceRise = (uint32_t)(logUs - lastRiseMonUs);
      uint32_t refPeriodUs = lastD8PeriodUs;
      if (ap2 > 0) {
        const uint32_t expOut = ap2 * (uint32_t)OUT_FREQ_DIV;
        if (expOut > refPeriodUs) {
          refPeriodUs = expOut;
        }
      }
      const float effRef = effectiveOutToggleHzFromAvgUs(lastAvgPeriodUs);
      if (effRef > 0.0f) {
        const uint32_t tEffUs = (uint32_t)(1000000.0f / effRef + 0.5f);
        if (tEffUs > refPeriodUs) {
          refPeriodUs = tEffUs;
        }
      }
      const uint32_t staleLim = refPeriodUs * 3UL + 500000UL;
      if (sinceRise <= staleLim) {
        const float d8Hz = 1000000.0f / (float)lastD8PeriodUs;
        float refOutHz = 0.0f;
        if (effRef > 0.0f) {
          refOutHz = effRef;
        } else if (ap2 > 0) {
          refOutHz = (1000000.0f / (float)ap2) / (float)OUT_FREQ_DIV;
        }
        bool showD8 = true;
        if (refOutHz > 0.0f) {
          const float err = (d8Hz > refOutHz) ? (d8Hz - refOutHz) : (refOutHz - d8Hz);
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
