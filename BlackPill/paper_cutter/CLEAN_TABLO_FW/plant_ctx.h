#pragma once

#include "fsm.h"

/** Shared plant / FSM runtime (owned here, used by enc/brake/ui/fsm). */

extern PlantData g_plant;
extern FsmState g_q;
extern FsmError g_err;
extern uint32_t g_speedEma;
extern bool g_speedShown;
extern bool g_brakeLatched;
/** Job finished (brake-to-zero / target done): no Run until Reset + new target. */
extern bool g_jobComplete;

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
extern uint16_t g_cacheEncInvert;

uint32_t plantTargetCm();
uint32_t plantRemainMeters();
void plantInvalidateCaches();
