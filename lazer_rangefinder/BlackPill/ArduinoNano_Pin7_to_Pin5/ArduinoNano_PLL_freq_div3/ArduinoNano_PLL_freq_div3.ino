/*
  Arduino Nano — ÷N output (default ÷3) using a digital PLL instead of a rolling period buffer.

  Same wiring as ArduinoNano_Pin7_to_Pin5.ino:
  - D3: 0–1 Hz square from pot A0 (jumper D3 → D2 for self-test)
  - D2: reference input (rising edges)
  - D9: f_ref / OUT_FREQ_DIV (PLL-locked half-period; software toggle when Timer1 cannot)
  - D8: optional monitor of D9

  Algorithm (see pll.h):
  - On each D2 rising edge: EWMA of T_in + phase error vs last D9 rising edge, PI loop sets
    locked half-period. This tracks frequency and pulls output phase toward a stable lock.

  Serial 115200, 250 ms: pllFreq_Hz, outFreq_Hz, d8Freq_Hz (same style as main sketch).
*/

#include "pll.h"

const uint8_t PIN_D3_GEN = 3;
const uint8_t PIN_D3_FREQ_ADC = A0;
const uint8_t PIN_IN = 2;
const uint8_t PIN_MON = 8;
const uint8_t PIN_PWM = 9;

static const uint32_t SERIAL_LOG_MS = 250;
static const uint32_t NO_INPUT_TIMEOUT_US = 1000000UL;
static const uint32_t NO_INPUT_WAIT_FIRST_PERIOD_US = 120000000UL;

static const uint8_t OUT_FREQ_DIV = 3;
static const uint16_t D3_ADC_FULL = 675;

static DivNPLL pll;

static uint32_t d3HalfPeriodUs = 0;

static uint32_t lastRiseRefUs = 0;
static bool prevHighIn = false;

static uint32_t lastOutRiseUs = 0;

static uint32_t lastRiseMonUs = 0;
static bool prevHighMon = false;
static uint32_t lastD8PeriodUs = 0;

static uint32_t lastAvgPeriodUs = 0;
static uint32_t lastAppliedAvgPeriodUs = 0xFFFFFFFFUL;
static uint32_t lastLogMs = 0;

static bool useSoftwareOut = false;
static uint32_t nextToggleUs = 0;
static bool softOutHigh = false;

static uint32_t nextD3ToggleUs = 0;
static bool d3GenHigh = false;

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

// Software D9 only so pll.h can use last D9 rising edge for phase error (Timer1 would break PD).
static void applyOutputFromAvgPeriodUs(uint32_t avgPeriodUs) {
  const uint32_t prevApplied = lastAppliedAvgPeriodUs;
  lastAvgPeriodUs = avgPeriodUs;
  lastAppliedAvgPeriodUs = avgPeriodUs;

  if (avgPeriodUs == 0) {
    lastD8PeriodUs = 0;
    lastRiseMonUs = 0;
    prevHighMon = digitalRead(PIN_MON) == HIGH;
    timer1Off();
    digitalWrite(PIN_PWM, LOW);
    useSoftwareOut = false;
    softOutHigh = false;
    lastOutRiseUs = 0;
    return;
  }

  timer1Off();
  useSoftwareOut = true;
  const bool first = (prevApplied == 0U || prevApplied == 0xFFFFFFFFUL);
  if (first) {
    softOutHigh = false;
    digitalWrite(PIN_PWM, LOW);
    nextToggleUs = micros();
  }
}

// Re-apply when PLL changes locked half-period (microseconds granularity)
static bool shouldApplyPLLHalf(uint32_t halfUs, uint32_t lastAppliedHalf) {
  if (halfUs == 0U) {
    return lastAppliedHalf != 0U;
  }
  if (lastAppliedHalf == 0U || lastAppliedHalf == 0xFFFFFFFFUL) {
    return true;
  }
  if (halfUs == lastAppliedHalf) {
    return false;
  }
  const uint32_t lo = (halfUs < lastAppliedHalf) ? halfUs : lastAppliedHalf;
  const uint32_t hi = (halfUs < lastAppliedHalf) ? lastAppliedHalf : halfUs;
  const uint32_t diff = hi - lo;
  const uint32_t thresh = (lo / 256U) + (lo / 512U) + 50U;
  return diff > thresh;
}

static uint32_t lastAppliedHalfUs = 0xFFFFFFFFUL;

void setup() {
  disableInternalPullupsAllPins();

  pll.reset();
  pll.setDiv(OUT_FREQ_DIV);

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
  Serial.println(F("PLL /3: D3->D2, pll.h phase+EWMA. Serial 115200."));
  prevHighIn = digitalRead(PIN_IN) == HIGH;
  prevHighMon = digitalRead(PIN_MON) == HIGH;
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

  const bool highIn = digitalRead(PIN_IN) == HIGH;
  const bool risingIn = highIn && !prevHighIn;
  prevHighIn = highIn;

  const bool highMon = digitalRead(PIN_MON) == HIGH;
  const bool risingMon = highMon && !prevHighMon;
  prevHighMon = highMon;

  if (risingMon) {
    if (lastRiseMonUs != 0) {
      lastD8PeriodUs = (uint32_t)(nowUs - lastRiseMonUs);
    }
    lastRiseMonUs = nowUs;
  }

  if (risingIn) {
    uint32_t periodUs = 0;
    if (lastRiseRefUs != 0) {
      periodUs = (uint32_t)(nowUs - lastRiseRefUs);
    }
    pll.onRefRise(nowUs, periodUs, lastOutRiseUs);
    lastRiseRefUs = nowUs;
  }

  const uint32_t pllTin = pll.inputPeriodEstimateUs();
  uint32_t stallUs = NO_INPUT_TIMEOUT_US;
  if (pllTin > 0) {
    const uint32_t dyn = pllTin * 3UL;
    if (dyn > stallUs) {
      stallUs = dyn;
    }
  } else if (lastRiseRefUs != 0) {
    stallUs = NO_INPUT_WAIT_FIRST_PERIOD_US;
  }

  if (lastRiseRefUs != 0 && (uint32_t)(nowUs - lastRiseRefUs) > stallUs) {
    lastRiseRefUs = 0;
    pll.reset();
    pll.setDiv(OUT_FREQ_DIV);
    lastAppliedHalfUs = 0xFFFFFFFFUL;
    if (lastAppliedAvgPeriodUs != 0) {
      applyOutputFromAvgPeriodUs(0);
    }
  }

  const uint32_t lockedHalf = pll.locked_half_us;
  if (pllTin > 0 && lockedHalf > 0) {
    if (shouldApplyPLLHalf(lockedHalf, lastAppliedHalfUs)) {
      lastAppliedHalfUs = lockedHalf;
      const uint32_t backTin = (uint32_t)(((uint64_t)lockedHalf * 2ULL) / (uint64_t)OUT_FREQ_DIV);
      applyOutputFromAvgPeriodUs(backTin > 0 ? backTin : pllTin);
    }
  }

  if (useSoftwareOut && pllTin >= 4 && lockedHalf > 0) {
    const uint32_t stepUs = lockedHalf;
    uint32_t t = micros();
    for (uint8_t k = 0; k < 24U && (uint32_t)(t - nextToggleUs) >= stepUs; k++) {
      const bool wasLow = !softOutHigh;
      softOutHigh = !softOutHigh;
      digitalWrite(PIN_PWM, softOutHigh ? HIGH : LOW);
      t = micros();
      if (wasLow && softOutHigh) {
        lastOutRiseUs = t;
      }
      nextToggleUs += stepUs;
    }
  }

  if ((uint32_t)(nowMs - lastLogMs) >= SERIAL_LOG_MS) {
    lastLogMs = nowMs;

    const uint32_t logUs = micros();
    const uint32_t tin = pll.inputPeriodEstimateUs();
    bool any = false;

    if (tin > 0) {
      const float f_in = 1000000.0f / (float)tin;
      Serial.print(F("pllFreq_Hz="));
      Serial.print(f_in, 3);
      Serial.print(F("  outFreq_Hz="));
      Serial.print(f_in / (float)OUT_FREQ_DIV, 3);
      Serial.print(F("  pe_us="));
      Serial.print(pll.lastPhaseErrUs());
      any = true;
    }
    if (lastD8PeriodUs > 0 && lastRiseMonUs != 0) {
      const uint32_t sinceRise = (uint32_t)(logUs - lastRiseMonUs);
      uint32_t refPeriodUs = lastD8PeriodUs;
      if (tin > 0) {
        const uint32_t expOut = tin * (uint32_t)OUT_FREQ_DIV;
        if (expOut > refPeriodUs) {
          refPeriodUs = expOut;
        }
      }
      const uint32_t staleLim = refPeriodUs * 3UL + 500000UL;
      if (sinceRise <= staleLim) {
        const float d8Hz = 1000000.0f / (float)lastD8PeriodUs;
        float refOutHz = 0.0f;
        if (tin > 0) {
          refOutHz = (1000000.0f / (float)tin) / (float)OUT_FREQ_DIV;
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
