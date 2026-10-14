#pragma once

#include <Arduino.h>

/**
 * Дальномер: ESP32-S3-Zero (GL-R01) → ESP-NOW → ESP32-C3 → USART1 → Black Pill.
 * C3 (c3_receiver.ino) шлёт на GPIO21 строки 115200 8N1: "345\n" (мм) или "NA\n".
 * Подключение: C3 GPIO21 (TX) → PA10 (USART1_RX); C3 GPIO20 (RX) → PA9 (не обязателен).
 */

void rangeLinkBegin();
void rangeLinkPoll(uint32_t nowMs);

/** true — есть свежее расстояние (пакет моложе RANGE_TIMEOUT_MS и не "NA"). */
bool rangeLinkValid(uint32_t nowMs);
/** Последнее расстояние, мм (RANGE_NO_READING если нет данных). */
uint16_t rangeLinkMm();
/** true, если по USART1 вообще приходили строки (линия к C3 жива). */
bool rangeLinkSeenAny();
