#pragma once

#include <Arduino.h>

/** Brake zone latch + effectiveness monitor (стр. 16). Soft-PWM is brake_relay. */

void brakeLogicClearLatch();
bool brakeLogicIsLatched();
/** Force PWM latch (e.g. СТОП) until shaft fully stops. */
void brakeLogicArmLatch();

/** Update latch from zone/error; return whether PWM should run. */
bool brakeLogicShouldArm(uint32_t nowMs);
void brakeLogicUpdateRelay(uint32_t nowMs);

/**
 * Edge: brake was on, shaft reached full stop.
 * Caller should play stop-triple and may mute faults.
 */
bool brakeLogicTakeStopComplete();

/** After stop: ignore fault pages until real encoder motion resumes. */
bool brakeLogicFaultsMuted();
void brakeLogicMuteFaults();
void brakeLogicClearFaultMute();

void brakeEffReset();
/** true → speed did not fall over BRAKE_EFF_PERIOD_MS while in zone. */
bool brakeEffPoll(uint32_t nowMs);
