#include "brake_logic.h"
#include "config.h"
#include "plant_ctx.h"
#include "enc_path.h"
#include "brake_relay.h"

void brakeLogicClearLatch() { g_brakeLatched = false; }
bool brakeLogicIsLatched() { return g_brakeLatched; }

static bool brakeShaftMoving(uint32_t nowMs) {
  return (int32_t)(nowMs - encPathLastPulseMs()) < (int32_t)BRAKE_HOLD_IDLE_MS;
}

static bool brakeSpeedIsZero() {
  return g_speedEma == 0 && g_plant.speedCms == 0;
}

bool brakeLogicShouldArm(uint32_t nowMs) {
  if (g_plant.brakeM == 0) {
    g_brakeLatched = false;
    return false;
  }

  if (g_q == FsmState::Run && g_plant.targetM > 0 &&
      plantRemainMeters() <= g_plant.brakeM) {
    g_brakeLatched = true;
  }

  if (g_q == FsmState::Error &&
      (g_err == FsmError::Reverse || g_err == FsmError::BrakeIneffective)) {
    g_brakeLatched = true;
  }

  if (!g_brakeLatched) return false;

  if (brakeSpeedIsZero()) {
    g_brakeLatched = false;
    return false;
  }

  if (!brakeShaftMoving(nowMs)) {
    g_brakeLatched = false;
    return false;
  }
  return true;
}

void brakeLogicUpdateRelay(uint32_t nowMs) {
  brakeRelayTick(nowMs, brakeLogicShouldArm(nowMs), g_plant.brakeOnMs,
                 g_plant.brakeOffMs);
}

static bool g_brakeEffArmed = false;
static uint32_t g_brakeEffLastMs = 0;
static uint32_t g_brakeEffLastCms = 0;

void brakeEffReset() {
  g_brakeEffArmed = false;
  g_brakeEffLastMs = 0;
  g_brakeEffLastCms = 0;
}

static uint32_t brakeEffSpeedCms() {
  uint32_t v = g_speedEma;
  if ((uint32_t)g_plant.speedCms > v) v = g_plant.speedCms;
  return v;
}

static bool brakeEffZoneActive() {
  if (g_plant.brakeM == 0 || g_plant.targetM == 0) return false;
  if (g_brakeLatched) return true;
  if (g_q == FsmState::Run && plantRemainMeters() <= g_plant.brakeM) return true;
  if (g_q == FsmState::Error &&
      (g_err == FsmError::Reverse || g_err == FsmError::BrakeIneffective)) {
    return true;
  }
  return false;
}

bool brakeEffPoll(uint32_t nowMs) {
  if (!BRAKE_EFF_ENABLE) {
    brakeEffReset();
    return false;
  }
  if (!brakeEffZoneActive()) {
    brakeEffReset();
    return false;
  }

  const uint32_t v = brakeEffSpeedCms();

  if (v < (uint32_t)BRAKE_EFF_MIN_CMS) {
    g_brakeEffArmed = false;
    g_brakeEffLastCms = 0;
    g_brakeEffLastMs = nowMs;
    return false;
  }

  if (!g_brakeEffArmed) {
    g_brakeEffArmed = true;
    g_brakeEffLastMs = nowMs;
    g_brakeEffLastCms = v;
    return false;
  }

  if ((int32_t)(nowMs - g_brakeEffLastMs) < (int32_t)BRAKE_EFF_PERIOD_MS) {
    return false;
  }

  g_brakeEffLastMs = nowMs;

  if (v < g_brakeEffLastCms) {
    g_brakeEffLastCms = v;
    return false;
  }

  return true;
}
