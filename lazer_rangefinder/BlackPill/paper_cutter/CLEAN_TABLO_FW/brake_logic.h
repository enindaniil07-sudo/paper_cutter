#pragma once

#include <Arduino.h>

/** Brake zone latch + effectiveness monitor (бейдж ТОРМ.). Soft-PWM is brake_relay. */

void brakeLogicClearLatch();
bool brakeLogicIsLatched();
/** Force PWM latch (e.g. СТОП) until shaft fully stops. */
void brakeLogicArmLatch();

/** Update latch from zone/error; return whether PWM should run. */
bool brakeLogicShouldArm(uint32_t nowMs);
/** true — дальномер ≥ «ПУСТОЙ РУЛОН» (данные свежие, настройка > 0). */
bool brakeLogicRangeZone(uint32_t nowMs);
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
/** true → over BRAKE_EFF_PERIOD_MS speed did not drop enough while in zone. */
bool brakeEffPoll(uint32_t nowMs);
/** Edge: speed dropped enough (or shaft almost stopped) in zone — brake works again. */
bool brakeEffTakeRecovered();
