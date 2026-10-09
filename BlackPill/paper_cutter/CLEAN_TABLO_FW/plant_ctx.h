#pragma once

#include "fsm.h"

/** Shared plant / FSM runtime (owned here, used by enc/brake/ui/fsm). */

extern PlantData g_plant;
extern FsmState g_q;
extern FsmError g_err;
extern uint32_t g_speedEma;
extern uint32_t g_rpmEma;  // об/мин на панель
extern bool g_speedShown;
extern bool g_brakeLatched;
/** Job finished: path/UI frozen until operator presses RESET. */
extern bool g_jobComplete;
/**
 * After СТОП: no auto-brake zone until СБРОС / новое задание.
 * Operator just rolls out the remaining length by hand.
 */
extern bool g_brakeHoldOff;
/** Settings pages 17/18 overlay; job FSM state (Run/Idle/…) stays as-is. */
extern bool g_settingsUi;

enum class TargetGate : uint8_t { Normal = 0, Resetting, Armed };
extern TargetGate g_tgtGate;
extern uint32_t g_tgtResetStartMs;

extern uint32_t g_cacheTarget;
extern uint32_t g_cacheRemain;
extern uint16_t g_cacheSpeed;
extern uint16_t g_cacheProgress;
extern uint16_t g_cacheKb;
extern uint32_t g_cacheBrake;
extern uint16_t g_cacheBrakeOn;
extern uint16_t g_cacheBrakeOff;

uint32_t plantTargetCm();
/** Осталось, м: ≥0 для зоны тормоза (после переезда — 0). */
uint32_t plantRemainMeters();
/** Осталось со знаком: после переезда задания — отрицательные метры. */
int32_t plantRemainMetersSigned();
void plantInvalidateCaches();
