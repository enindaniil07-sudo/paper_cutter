#pragma once

#include <Arduino.h>

/** PB0 → реле (активный HIGH). init once in fsmBegin. */
void brakeRelayBegin();

/** Soft PWM: HIGH onMs / LOW offMs while armed; otherwise forced LOW. */
void brakeRelayTick(uint32_t nowMs, bool armed, uint16_t onMs, uint16_t offMs);

void brakeRelayOff();

/** Текущий уровень PB0 (HIGH = катушка/драйвер вкл.). */
bool brakeRelayIsHigh();
