#pragma once

#include <Arduino.h>

/** Brake zone latch + effectiveness monitor (стр. 16). Soft-PWM is brake_relay. */

void brakeLogicClearLatch();
bool brakeLogicIsLatched();

/** Update latch from zone/error; return whether PWM should run. */
bool brakeLogicShouldArm(uint32_t nowMs);
void brakeLogicUpdateRelay(uint32_t nowMs);

void brakeEffReset();
/** true → speed did not fall over BRAKE_EFF_PERIOD_MS while in zone. */
bool brakeEffPoll(uint32_t nowMs);
