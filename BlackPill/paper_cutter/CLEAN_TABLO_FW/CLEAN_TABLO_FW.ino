/*
  CLEAN_TABLO_FW — Black Pill F411CE ↔ CLEAN_TABLO (DWIN)

  Autonics E40S6-1000-3-T-24: 1000 P/R на A, колесо Ø 8 см.
  Энкодер: TIM2 encoder mode, A=PA0, B=PA1.
  Ошибки: стр. 11 реверс, 12 нет сигнала, 13 нет ЗАДАНО,
  14 скачок скорости, 15 обрыв A/B. ИСПРАВИТЬ = VP 6054.
*/

#include <Arduino.h>
#include "config.h"
#include "dwin.h"
#include "fsm.h"

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  dwinBegin(115200);
  delay(150);
  fsmBegin();
}

void loop() {
  dwinPoll(fsmOnDwinVp);

  const uint32_t now = millis();
  fsmPollButtons(now);
  fsmMotionTick(now);

  static uint32_t tBlink = 0;
  if (now - tBlink >= fsmLedPeriodMs()) {
    tBlink = now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }

}
