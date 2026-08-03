#include "brake_logic.h"
#include "config.h"
#include "plant_ctx.h"
#include "enc_path.h"
#include "brake_relay.h"

static bool g_brakeSawMotion = false;
static bool g_stopCompletePending = false;
static bool g_faultsMuted = false;

void brakeLogicClearLatch() {
  g_brakeLatched = false;
  g_brakeSawMotion = false;
}

bool brakeLogicIsLatched() { return g_brakeLatched; }

void brakeLogicArmLatch() {
  g_brakeLatched = true;
  // If already stopped, don't treat as a completed braking cycle.
  g_brakeSawMotion = !(g_speedEma == 0 && g_plant.speedCms == 0);
}

bool brakeLogicTakeStopComplete() {
  const bool v = g_stopCompletePending;
  g_stopCompletePending = false;
  return v;
}

bool brakeLogicFaultsMuted() { return g_faultsMuted; }

void brakeLogicMuteFaults() { g_faultsMuted = true; }

void brakeLogicClearFaultMute() { g_faultsMuted = false; }

static bool brakeShaftMoving(uint32_t nowMs) {
  return (int32_t)(nowMs - encPathLastPulseMs()) < (int32_t)BRAKE_HOLD_IDLE_MS;
}

static bool brakeSpeedIsZero() {
  return g_speedEma == 0 && g_plant.speedCms == 0;
}

static void releaseLatchAfterStop() {
  const bool complete = g_brakeSawMotion;
  g_brakeLatched = false;
  g_brakeSawMotion = false;
  if (complete) {
    g_stopCompletePending = true;
    g_faultsMuted = true;
    brakeEffReset();
  }
}

bool brakeLogicShouldArm(uint32_t nowMs) {
  // Distance zone (настройки «торм. м») — только в Run.
  if (g_plant.brakeM > 0 && g_q == FsmState::Run && g_plant.targetM > 0 &&
      plantRemainMeters() <= g_plant.brakeM) {
    g_brakeLatched = true;
  }

  // Реверс / отказ тормоза — держим ШИМ, пока вал крутится.
  if (g_q == FsmState::Error &&
      (g_err == FsmError::Reverse || g_err == FsmError::BrakeIneffective)) {
    g_brakeLatched = true;
  }

  if (!g_brakeLatched) return false;

  if (!brakeSpeedIsZero() || brakeShaftMoving(nowMs)) {
    g_brakeSawMotion = true;
  }

  // Полная остановка → отпустить реле (+ сигнал завершения, если тормозили).
  if (brakeSpeedIsZero()) {
    releaseLatchAfterStop();
    return false;
  }

  if (!brakeShaftMoving(nowMs)) {
    releaseLatchAfterStop();
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
  if (g_faultsMuted) return false;
  if (g_brakeLatched) return true;
  if (g_plant.brakeM == 0 || g_plant.targetM == 0) return false;
  if (g_q == FsmState::Run && plantRemainMeters() <= g_plant.brakeM) return true;
  if (g_q == FsmState::Error &&
      (g_err == FsmError::Reverse || g_err == FsmError::BrakeIneffective)) {
    return true;
  }
  return false;
}

bool brakeEffPoll(uint32_t nowMs) {
  if (!BRAKE_EFF_ENABLE || g_faultsMuted) {
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
