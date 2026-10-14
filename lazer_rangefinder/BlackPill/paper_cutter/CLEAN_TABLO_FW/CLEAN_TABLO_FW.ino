/*
  CLEAN_TABLO_FW — Black Pill F411CE ↔ CLEAN_TABLO (DWIN)

  Autonics E50S8-2500-3-T-24: 2500 P/R на A, колесо Ø 8 см.
  Энкодер: TIM2 encoder mode, A=PA0, B=PA1.
  Дальномер: ESP32-C3 (c3_receiver) → USART1 RX=PA10 (TX=PA9), строки «мм\n» / «NA\n».
    Настройка «ПУСТОЙ РУЛОН» (VP 6098, мм): расстояние ≥ порога в Run → тормоз
    тем же ШИМ-режимом реле, что и зона «торм. м». 0 = выкл.
  Ошибки: только бейдж в окошке под шестерёнкой (IconShow VP 6040):
    1 = ЭНК. (нет сигнала энкодера), 2 = ТОРМ. (тормоз не замедляет).
  Снимаются сами: ЭНК. — пошли импульсы; ТОРМ. — скорость пошла вниз.
  СТОП / СБРОС тоже снимают. Полноэкранных страниц ошибок и кнопки OK нет.
*/

#include <Arduino.h>
#include "config.h"
#include "dwin.h"
#include "dwin_buzz.h"
#include "brake_relay.h"
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
  dwinBuzzTick(now, brakeRelayIsHigh());

  static uint32_t tBlink = 0;
  if (now - tBlink >= fsmLedPeriodMs()) {
    tBlink = now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }

}
