#include "plant_ctx.h"
#include "config.h"

PlantData g_plant = {};
FsmState g_q = FsmState::Idle;
FsmError g_err = FsmError::None;
uint32_t g_speedEma = 0;
bool g_speedShown = false;
bool g_brakeLatched = false;
bool g_jobComplete = false;

TargetGate g_tgtGate = TargetGate::Normal;
uint32_t g_tgtResetStartMs = 0;

uint32_t g_cacheTarget = 0xFFFFFFFFu;
uint32_t g_cacheRemain = 0xFFFFFFFFu;
uint16_t g_cacheSpeed = 0xFFFF;
uint16_t g_cacheProgress = 0xFFFF;
uint16_t g_cacheKb = 0xFFFF;
uint32_t g_cacheBrake = 0xFFFFFFFFu;
uint16_t g_cacheBrakeOn = 0xFFFF;
uint16_t g_cacheBrakeOff = 0xFFFF;
uint16_t g_cacheEncInvert = 0xFFFF;

uint32_t plantTargetCm() { return g_plant.targetM * 100u; }

uint32_t plantRemainMeters() {
  const uint32_t doneM = g_plant.travelM / 100u;
  if (g_plant.targetM <= doneM) return 0;
  return g_plant.targetM - doneM;
}

void plantInvalidateCaches() {
  g_cacheTarget = g_cacheRemain = 0xFFFFFFFFu;
  g_cacheSpeed = g_cacheProgress = g_cacheKb = 0xFFFF;
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  g_cacheEncInvert = 0xFFFF;
}
