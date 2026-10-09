#include "plant_ctx.h"
#include "config.h"

PlantData g_plant = {};
FsmState g_q = FsmState::Idle;
FsmError g_err = FsmError::None;
uint32_t g_speedEma = 0;
uint32_t g_rpmEma = 0;
bool g_speedShown = false;
bool g_brakeLatched = false;
bool g_jobComplete = false;
bool g_brakeHoldOff = false;
bool g_settingsUi = false;

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

uint32_t plantTargetCm() { return g_plant.targetM * 100u; }

int32_t plantRemainMetersSigned() {
  const int32_t doneM = (int32_t)(g_plant.travelM / 100u);
  int32_t rem = (int32_t)g_plant.targetM - doneM;
  if (rem > (int32_t)MAX_METERS) rem = (int32_t)MAX_METERS;
  if (rem < -(int32_t)MAX_METERS) rem = -(int32_t)MAX_METERS;
  return rem;
}

uint32_t plantRemainMeters() {
  const int32_t rem = plantRemainMetersSigned();
  return rem > 0 ? (uint32_t)rem : 0;
}

void plantInvalidateCaches() {
  g_cacheTarget = g_cacheRemain = 0xFFFFFFFFu;
  g_cacheSpeed = g_cacheProgress = g_cacheKb = 0xFFFF;
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
}
