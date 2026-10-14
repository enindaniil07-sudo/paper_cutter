#include "brake_logic.h"
#include "config.h"
#include "plant_ctx.h"
#include "enc_path.h"
#include "brake_relay.h"
#include "range_link.h"

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
    // После тормоза вал встал: реле дальше снаружи, MCU только считает.
    g_brakeHoldOff = true;
    g_stopCompletePending = true;
    g_faultsMuted = true;
    brakeEffReset();
    brakeRelayOff();
  }
}

/** Зона по метрам: «осталось» ≤ настройки «торм. м». */
static bool brakeZoneByMeters() {
  return g_plant.brakeM > 0 && g_plant.targetM > 0 &&
         plantRemainMeters() <= g_plant.brakeM;
}

/**
 * Зона по дальномеру («ПУСТОЙ РУЛОН»): рулон разматывается, расстояние от
 * датчика до бумаги растёт; дошло до настройки (мм) → тормоз.
 * 0 = выкл.; без свежих данных от C3 (нет линка / "NA") — не срабатывает.
 */
static bool brakeZoneByRange(uint32_t nowMs) {
  if (g_plant.rollMm == 0) return false;
  if (!rangeLinkValid(nowMs)) return false;
  return rangeLinkMm() >= g_plant.rollMm;
}

bool brakeLogicRangeZone(uint32_t nowMs) { return brakeZoneByRange(nowMs); }

/** Любая зона торможения — только в Run, не после СТОП/докрутки. */
static bool brakeZoneReached(uint32_t nowMs) {
  if (g_brakeHoldOff || g_q != FsmState::Run) return false;
  return brakeZoneByMeters() || brakeZoneByRange(nowMs);
}

bool brakeLogicShouldArm(uint32_t nowMs) {
  // Зона по метрам («торм. м») или по дальномеру («ПУСТОЙ РУЛОН»).
  if (brakeZoneReached(nowMs)) {
    g_brakeLatched = true;
  }

  // Отказ тормоза (бейдж ТОРМ.) — держим ШИМ, пока вал крутится.
  if (g_err == FsmError::BrakeIneffective) {
    g_brakeLatched = true;
  }

  if (!g_brakeLatched) return false;

  if (!brakeSpeedIsZero() || brakeShaftMoving(nowMs)) {
    g_brakeSawMotion = true;
  }

  // Полная остановка = тишина энкодера ≥ BRAKE_HOLD_IDLE_MS.
  // Не отпускать только по EMA/speed==0: пустые окна дают ложный job-complete.
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
static bool g_brakeEffRecovered = false;

void brakeEffReset() {
  g_brakeEffArmed = false;
  g_brakeEffLastMs = 0;
  g_brakeEffLastCms = 0;
  g_brakeEffRecovered = false;
}

bool brakeEffTakeRecovered() {
  const bool v = g_brakeEffRecovered;
  g_brakeEffRecovered = false;
  return v;
}

static uint32_t brakeEffSpeedCms() {
  uint32_t v = g_speedEma;
  if ((uint32_t)g_plant.speedCms > v) v = g_plant.speedCms;
  return v;
}

static bool brakeEffZoneActive(uint32_t nowMs) {
  // g_faultsMuted — только EncLoss после стопа/сброса; зону тормоза не глушим.
  if (g_brakeHoldOff) return false;
  if (g_brakeLatched) return true;
  if (brakeZoneReached(nowMs)) return true;
  if (g_err == FsmError::BrakeIneffective) return true;
  return false;
}

bool brakeEffPoll(uint32_t nowMs) {
  // Mute после успешной остановки — не глушим зону торможения (см. TargetDone).
  if (!BRAKE_EFF_ENABLE) {
    brakeEffReset();
    return false;
  }
  if (!brakeEffZoneActive(nowMs)) {
    brakeEffReset();
    return false;
  }

  const uint32_t v = brakeEffSpeedCms();

  if (v < (uint32_t)BRAKE_EFF_MIN_CMS) {
    // Почти стоп — тормоз сработал.
    g_brakeEffArmed = false;
    g_brakeEffLastCms = 0;
    g_brakeEffLastMs = nowMs;
    g_brakeEffRecovered = true;
    return false;
  }

  if (!g_brakeEffArmed) {
    g_brakeEffArmed = true;
    g_brakeEffLastMs = nowMs;
    g_brakeEffLastCms = v;  // база на входе в зону / после перезахвата
    return false;
  }

  if ((int32_t)(nowMs - g_brakeEffLastMs) < (int32_t)BRAKE_EFF_PERIOD_MS) {
    return false;
  }

  g_brakeEffLastMs = nowMs;

  // Нужно явное замедление: любой шум EMA «−1» раньше вечно откладывал стр.16.
  uint32_t needDrop = (uint32_t)BRAKE_EFF_MIN_DROP_CMS;
  const uint32_t pct = g_brakeEffLastCms / 10u;  // ≥10% от базы
  if (pct > needDrop) needDrop = pct;

  if (v + needDrop <= g_brakeEffLastCms) {
    g_brakeEffLastCms = v;  // тормоз работает — новая база, ещё период
    g_brakeEffRecovered = true;  // скорость пошла вниз → снять бейдж ТОРМ.
    return false;
  }

  return true;  // та же / выше / крошечный спад → отказ тормоза
}
