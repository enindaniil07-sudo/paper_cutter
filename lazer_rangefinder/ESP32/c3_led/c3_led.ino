/*
  ESP32-C3FH4. Цветной светодиод платы — WS2812 на выводе 10.
  На части таких плат вместо него обычный светодиод на выводе 8.
  Здесь включены оба варианта.
*/

#include <Arduino.h>
#include "soc/gpio_struct.h"

static constexpr int kColorPin = 10;
static constexpr int kPlainPins[] = {8, 2};

static inline uint32_t IRAM_ATTR cycles() {
  uint32_t c;
  asm volatile("csrr %0, mcycle" : "=r"(c));
  return c;
}

static inline void IRAM_ATTR waitCycles(uint32_t n) {
  uint32_t start = cycles();
  while ((uint32_t)(cycles() - start) < n) {}
}

void IRAM_ATTR writeColor(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t order[3] = {r, g, b};
  const uint32_t mask = 1UL << kColorPin;
  const uint32_t mhz = 160;
  const uint32_t t1h = mhz * 8 / 10;
  const uint32_t t1l = mhz * 45 / 100;
  const uint32_t t0h = mhz * 35 / 100;
  const uint32_t t0l = mhz * 8 / 10;
  noInterrupts();
  for (int n = 0; n < 3; n++) {
    uint8_t value = order[n];
    for (int bit = 7; bit >= 0; bit--) {
      if (value & (1 << bit)) {
        GPIO.out_w1ts.val = mask;
        waitCycles(t1h);
        GPIO.out_w1tc.val = mask;
        waitCycles(t1l);
      } else {
        GPIO.out_w1ts.val = mask;
        waitCycles(t0h);
        GPIO.out_w1tc.val = mask;
        waitCycles(t0l);
      }
    }
  }
  interrupts();
}

void colorOn() {
  pinMode(kColorPin, OUTPUT);
  digitalWrite(kColorPin, LOW);
  delayMicroseconds(300);
  writeColor(255, 255, 255);
  delayMicroseconds(300);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  for (int pin : kPlainPins) pinMode(pin, OUTPUT);
  colorOn();
  Serial.println("led test");
}

void loop() {
  for (int pin : kPlainPins) {
    for (int level = 1; level >= 0; level--) {
      digitalWrite(pin, level);
      Serial.print("pin ");
      Serial.print(pin);
      Serial.println(level ? " high" : " low");
      uint32_t start = millis();
      while (millis() - start < 700) {
        colorOn();
        delay(20);
      }
    }
  }
}
