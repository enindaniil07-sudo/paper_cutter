#include "fsm.h"
#include "dwin.h"
#include "enc_tim2.h"
#include "settings_store.h"

/*
  Encoder Autonics E40S6-1000-3-T-24 + wheel Ø 8 cm
  -------------------------------------------------
  Path: TIM2 (PA0/PA1) hardware encoder; MCU polls CNT + UIF.
  On UIF: soft path kept, TIM CNT cleared — display NOT cleared.
  Speed (M-method): Δcounts / Δt_us, EMA-smoothed.
*/

static uint64_t g_nm = 0;
static uint32_t g_pulseWin = 0;  // forward TIM counts in current speed window
static uint8_t g_reverseHit = 0;
static uint8_t g_revStreak = 0;
static uint32_t g_lastRevUs = 0;
static uint32_t g_lastPulseMs = 0;
static bool g_encSeenThisRun = false;
static uint32_t g_encCntWatch = 0;
static bool g_encActivity = false;  // pulses this poll (for auto-Run)

static void encoderAttach() { encTim2Begin(); }

static void encoderClear() {
  encTim2Clear();
  g_nm = 0;
  g_pulseWin = 0;
  g_reverseHit = 0;
  g_revStreak = 0;
  g_lastRevUs = 0;
}

static void encoderClearWindow() {
  g_pulseWin = 0;
  encTim2SyncBaseline();
}

/** Poll TIM2: accumulate path, speed window, reverse streak. */
static void encoderPollHw(uint32_t nowMs) {
  const EncTim2Delta d = encTim2Poll();
  // d.overflow: UIF handled inside encTim2Poll (CNT reset). Soft g_nm untouched by that.
  g_encActivity = false;

  // Use loop nowMs (not millis()) so stamps never run ahead of the no-signal check.
  if (d.forward > 0 || d.reverse > 0 || d.overflow) {
    g_encActivity = true;
    g_lastPulseMs = nowMs;
    g_encSeenThisRun = true;
  }

  if (d.forward > 0) {
    g_revStreak = 0;
    g_pulseWin += d.forward;
    g_nm += (uint64_t)d.forward * (uint64_t)NM_PER_COUNT;
  }

  if (d.reverse > 0) {
    const uint32_t nowUs = micros();
    if (g_revStreak > 0 && (nowUs - g_lastRevUs) > ENC_REV_STREAK_GAP_US) {
      g_revStreak = 0;
    }
    g_lastRevUs = nowUs;
    uint32_t add = d.reverse;
    if (add > 255u) add = 255u;
    uint16_t next = (uint16_t)g_revStreak + (uint16_t)add;
    if (next > 255u) next = 255u;
    g_revStreak = (uint8_t)next;
    if (g_revStreak >= ENC_REV_CONFIRM) {
      g_reverseHit = 1;
      g_revStreak = 0;
    }
  }
}

static uint32_t nmToTravelCm(uint64_t nm) {
  const uint64_t v = nm / 10000000ULL;
  const uint64_t cap = (uint64_t)MAX_METERS * 100ULL;
  if (v >= cap) return (uint32_t)cap;
  return (uint32_t)v;
}

static FsmState g_q = FsmState::Idle;
static FsmError g_err = FsmError::None;
static PlantData g_plant = {};
static bool g_speedShown = false;
static uint32_t g_speedEma = 0;

// Channel A/B integrity — TIM CCxIF + IDR (PA0/PA1).
static uint8_t g_chALevel = 0;
static uint8_t g_chBLevel = 0;
static bool g_chHaveLevel = false;
static uint32_t g_chAEdgeMs = 0;
static uint32_t g_chBEdgeMs = 0;
static uint8_t g_chAEdges = 0;  // prove-alive this Run
static uint8_t g_chBEdges = 0;
static bool g_chASeen = false;
static bool g_chBSeen = false;
static uint32_t g_runStartMs = 0;

// После СБРОС: не принимать старое ЗАДАНО, пока панель не отдаст 0,
// затем принимать только новое ненулевое (ввод пользователя).
enum class TargetGate : uint8_t { Normal = 0, Resetting, Armed };
static TargetGate g_tgtGate = TargetGate::Normal;
static uint32_t g_tgtResetStartMs = 0;

static uint32_t g_cacheTarget = 0xFFFFFFFFu;
static uint32_t g_cacheRemain = 0xFFFFFFFFu;
static uint16_t g_cacheSpeed = 0xFFFF;
static uint16_t g_cacheProgress = 0xFFFF;
static uint16_t g_cacheKb = 0xFFFF;
static uint32_t g_cacheBrake = 0xFFFFFFFFu;
static uint16_t g_cacheBrakeOn = 0xFFFF;
static uint16_t g_cacheBrakeOff = 0xFFFF;

static void invalidateCaches() {
  g_cacheTarget = g_cacheRemain = 0xFFFFFFFFu;
  g_cacheSpeed = g_cacheProgress = g_cacheKb = 0xFFFF;
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
}

static uint32_t targetCm() { return g_plant.targetM * 100u; }

static uint32_t remainMeters() {
  const uint32_t doneM = g_plant.travelM / 100u;
  if (g_plant.targetM <= doneM) return 0;
  return g_plant.targetM - doneM;
}

static uint16_t calcProgressPct() {
  const uint32_t t = targetCm();
  if (t == 0) return 0;
  uint64_t p = ((uint64_t)g_plant.travelM * 100ULL) / t;
  if (p > 100ULL) p = 100ULL;
  return (uint16_t)p;
}

static void writeTargetZero() {
  dwinWriteU32(VP_TARGET, 0);
  dwinWriteU16(VP_TARGET, 0);
  dwinWriteU16((uint16_t)(VP_TARGET + 1u), 0);
  dwinWriteU32(VP_TARGET, 0);
}

static void writeAllDisplayZeros() {
  writeTargetZero();
  dwinWriteU32(VP_TRAVEL, 0);
  dwinWriteU32(VP_TRAVEL, 0);
  dwinWriteU16(VP_PROGRESS, 0);
  dwinWriteU16(VP_PROGRESS, 0);
  dwinWriteU16(VP_SPEED, 0);
}

static void pushKb() {
  const uint16_t v = (uint16_t)min(g_plant.kbBuf, (uint32_t)MAX_METERS);
  dwinWriteU16(VP_KB_BUF, v);
  dwinWriteU16(VP_KB_BUF, v);
  g_cacheKb = v;
}

static void pushSettings() {
  dwinWriteU32IfChanged(VP_BRAKE, min(g_plant.brakeM, (uint32_t)MAX_METERS), g_cacheBrake);
  dwinWriteU16IfChanged(VP_BRAKE_ON_MS, g_plant.brakeOnMs, g_cacheBrakeOn);
  dwinWriteU16IfChanged(VP_BRAKE_OFF_MS, g_plant.brakeOffMs, g_cacheBrakeOff);
}

/** Ask panel for all settings VPs (OK may have written them moments ago). */
static void requestSettingsReads() {
  dwinRequestReadU32(VP_BRAKE);
  dwinRequestReadU16(VP_BRAKE_ON_MS);
  dwinRequestReadU16(VP_BRAKE_OFF_MS);
}

/** Pump UART until replies land in applySettingsVp / EEPROM. */
static void pullSettingsFromPanel(uint32_t waitMs) {
  requestSettingsReads();
  const uint32_t t0 = millis();
  while ((millis() - t0) < waitMs) {
    dwinPoll(fsmOnDwinVp);
  }
}

static void applySettingsVp(uint16_t vp, uint32_t value) {
  bool changed = false;
  if (vp == VP_BRAKE) {
    if (value > MAX_METERS) value = MAX_METERS;
    if (g_plant.brakeM != value) {
      g_plant.brakeM = value;
      changed = true;
    }
    g_cacheBrake = value;
  } else if (vp == VP_BRAKE_ON_MS) {
    if (value > 9999u) value = 9999u;
    if (g_plant.brakeOnMs != (uint16_t)value) {
      g_plant.brakeOnMs = (uint16_t)value;
      changed = true;
    }
    g_cacheBrakeOn = (uint16_t)value;
  } else if (vp == VP_BRAKE_OFF_MS) {
    if (value > 9999u) value = 9999u;
    if (g_plant.brakeOffMs != (uint16_t)value) {
      g_plant.brakeOffMs = (uint16_t)value;
      changed = true;
    }
    g_cacheBrakeOff = (uint16_t)value;
  }
  if (changed) settingsSave(g_plant);
}

static void pushRemain() {
  dwinWriteU32IfChanged(VP_TRAVEL, remainMeters(), g_cacheRemain);
}

static void pushProgress() {
  g_plant.progressPct = calcProgressPct();
  dwinWriteU16IfChanged(VP_PROGRESS, g_plant.progressPct, g_cacheProgress);
}

static void pushTravel() {
  pushRemain();
  pushProgress();
}

static void forcePushRemainProgress() {
  const uint32_t r = remainMeters();
  const uint16_t p = calcProgressPct();
  g_plant.progressPct = p;
  dwinWriteU32(VP_TRAVEL, r);
  dwinWriteU16(VP_PROGRESS, p);
  g_cacheRemain = r;
  g_cacheProgress = p;
}

static void pushSpeed() {
  dwinWriteU16IfChanged(VP_SPEED, g_plant.speedCms, g_cacheSpeed);
}

static void forceSpeedZero() {
  g_plant.speedCms = 0;
  g_speedEma = 0;
  if (g_speedShown || g_cacheSpeed != 0) {
    dwinWriteU16(VP_SPEED, 0);
    g_cacheSpeed = 0;
    g_speedShown = false;
  }
}

static void pushMotion() {
  pushTravel();
  pushSpeed();
}

static void pushTarget() {
  dwinWriteU32IfChanged(VP_TARGET, (uint32_t)g_plant.targetM, g_cacheTarget);
}

static void pushAll() {
  pushTarget();
  pushMotion();
  pushKb();
}

void fsmPushPlant() { pushAll(); }
void fsmPushTarget() {
  if (g_q == FsmState::Keypad || g_q == FsmState::Settings) return;
  if (g_tgtGate != TargetGate::Normal) return;
  dwinRequestReadU32(VP_TARGET);
}

FsmState fsmState() { return g_q; }
const PlantData& fsmPlant() { return g_plant; }

bool fsmSpeedLive() {
  return g_q == FsmState::Idle || g_q == FsmState::Run;
}

uint16_t fsmLedPeriodMs() {
  if (g_q == FsmState::Error) return 60u;
  return (g_q == FsmState::Run) ? 100u : 450u;
}

static uint16_t pageForError(FsmError e) {
  switch (e) {
    case FsmError::Reverse: return PAGE_ERR_REVERSE;
    case FsmError::NoEncoder: return PAGE_ERR_NO_ENC;
    case FsmError::NoTarget: return PAGE_ERR_NO_TARGET;
    case FsmError::SpeedJump: return PAGE_ERR_SPEED_JUMP;
    case FsmError::ChannelFault: return PAGE_ERR_CHANNEL;
    default: return PAGE_ERR_REVERSE;
  }
}

static void actShowError(FsmError kind) {
  forceSpeedZero();
  encoderClearWindow();
  g_err = kind;
  const uint16_t page = pageForError(kind);
  dwinSetPage(page);
  delay(20);
  dwinSetPage(page);
}

static void actDismissError() {
  g_reverseHit = 0;
  g_revStreak = 0;
  g_err = FsmError::None;
  g_chAEdges = 0;
  g_chBEdges = 0;
  g_chASeen = false;
  g_chBSeen = false;
  g_chHaveLevel = false;
  encoderClearWindow();
  dwinWriteU16(VP_ERR_ACK, 0);
  dwinSetPage(PAGE_MAIN);
  delay(20);
  forcePushRemainProgress();
  pushSpeed();
}

static void channelReset(uint32_t nowMs) {
  uint8_t a = 0, b = 0;
  encTim2ReadAb(a, b);
  bool capA = false, capB = false;
  encTim2TakeCaptureEdges(capA, capB);  // clear stale CCxIF
  g_chALevel = a;
  g_chBLevel = b;
  g_chHaveLevel = true;
  g_chAEdgeMs = nowMs;
  g_chBEdgeMs = nowMs;
  g_chAEdges = 0;
  g_chBEdges = 0;
  g_chASeen = false;
  g_chBSeen = false;
}

/**
 * Обрыв A/B → стр. 15.
 * Фронты: TIM CC1IF/CC2IF (без пропуска между опросами) + смена IDR.
 * После prove-alive: один молчит ≥ DEAD_MS, второй дал фронт недавно.
 * Если при живом TIM один канал так и не ожил — тоже обрыв.
 */
static bool channelPollFault(uint32_t nowMs) {
  if ((int32_t)(nowMs - g_runStartMs) < (int32_t)ENC_CH_ARM_MS) return false;

  uint8_t a = 0, b = 0;
  encTim2ReadAb(a, b);
  bool capA = false, capB = false;
  encTim2TakeCaptureEdges(capA, capB);

  if (!g_chHaveLevel) {
    channelReset(nowMs);
    return false;
  }

  const bool edgeA = capA || (a != g_chALevel);
  const bool edgeB = capB || (b != g_chBLevel);
  g_chALevel = a;
  g_chBLevel = b;

  if (edgeA) {
    g_chAEdgeMs = nowMs;
    if (g_chAEdges < 255) g_chAEdges++;
    if (g_chAEdges >= ENC_CH_MIN_EDGES) g_chASeen = true;
  }
  if (edgeB) {
    g_chBEdgeMs = nowMs;
    if (g_chBEdges < 255) g_chBEdges++;
    if (g_chBEdges >= ENC_CH_MIN_EDGES) g_chBSeen = true;
  }

  const int32_t ageA = (int32_t)(nowMs - g_chAEdgeMs);
  const int32_t ageB = (int32_t)(nowMs - g_chBEdgeMs);
  const int32_t sinceTim = (int32_t)(nowMs - g_lastPulseMs);
  const bool timLive = sinceTim <= (int32_t)ENC_CH_MOTION_MS;
  const bool pinLive = ageA <= (int32_t)ENC_CH_MOTION_MS ||
                       ageB <= (int32_t)ENC_CH_MOTION_MS;
  if (!timLive && !pinLive) return false;

  // Оба уже работали в этом Run — классическая асимметрия.
  if (g_chASeen && g_chBSeen) {
    if (ageB >= (int32_t)ENC_CH_DEAD_MS &&
        ageA <= (int32_t)ENC_CH_MOTION_MS) {
      return true;
    }
    if (ageA >= (int32_t)ENC_CH_DEAD_MS &&
        ageB <= (int32_t)ENC_CH_MOTION_MS) {
      return true;
    }
  }

  // Канал молчал с начала Run, а второй + TIM уже давно живые.
  const int32_t runAge = (int32_t)(nowMs - g_runStartMs);
  if (runAge >= (int32_t)(ENC_CH_ARM_MS + ENC_CH_DEAD_MS)) {
    if (g_chASeen && !g_chBSeen && timLive &&
        ageA <= (int32_t)ENC_CH_MOTION_MS) {
      return true;
    }
    if (g_chBSeen && !g_chASeen &&
        ageB <= (int32_t)ENC_CH_MOTION_MS) {
      // A мёртв с старта: TIM mode2 тоже молчит — хватает живого B.
      return true;
    }
  }

  return false;
}

static void actClearPlant() {
  g_plant.targetM = 0;
  g_plant.travelM = 0;
  g_plant.speedCms = 0;
  g_plant.progressPct = 0;
  g_plant.kbBuf = 0;
  g_plant.kbFresh = true;
  encoderClear();
  g_speedEma = 0;
  g_speedShown = true;
  forceSpeedZero();
  invalidateCaches();

  g_tgtGate = TargetGate::Resetting;
  g_tgtResetStartMs = millis();
  writeAllDisplayZeros();
  dwinWriteU16(VP_RESET, 0);
  pushKb();

  g_cacheTarget = 0;
  g_cacheRemain = 0;
  g_cacheProgress = 0;
  g_cacheSpeed = 0;

  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(PIN_LED, (i & 1u) ? LOW : HIGH);
    delay(35);
  }
  digitalWrite(PIN_LED, HIGH);
}

static void actFreezeSpeed() { forceSpeedZero(); }

static void actPrepareRun() {
  if (g_plant.targetM > 0 && g_plant.travelM >= targetCm()) {
    g_plant.travelM = 0;
    encoderClear();
    forcePushRemainProgress();
  }
  encoderClearWindow();
  forceSpeedZero();
  const uint32_t t = millis();
  channelReset(t);
  g_runStartMs = t;
  // Same stamp as run start; fsmMotionTick may still see an older nowMs this
  // loop — no-signal check must tolerate lastPulse >= nowMs (see below).
  g_lastPulseMs = t;
  g_encSeenThisRun = false;
  g_encCntWatch = encTim2Cnt();
  g_revStreak = 0;
  g_reverseHit = 0;
}

static void actKbOpen() {
  g_plant.kbBuf = g_plant.targetM;
  g_plant.kbFresh = true;
  dwinSetPage(PAGE_KEYPAD);
  delay(50);
  dwinSetPage(PAGE_KEYPAD);
  delay(30);
  pushKb();
}

static void actKbDigit(uint8_t d) {
  if (d > 9) return;
  if (g_plant.kbFresh) {
    g_plant.kbBuf = 0;
    g_plant.kbFresh = false;
  }
  const uint32_t next = g_plant.kbBuf * 10u + d;
  if (next > MAX_METERS) return;
  g_plant.kbBuf = next;
  pushKb();
}

static void actKbDel() {
  if (g_plant.kbFresh) {
    g_plant.kbBuf = 0;
    g_plant.kbFresh = false;
  } else {
    g_plant.kbBuf /= 10u;
  }
  pushKb();
}

static void actKbCommit() {
  if (g_plant.kbBuf > MAX_METERS) g_plant.kbBuf = MAX_METERS;
  g_plant.targetM = g_plant.kbBuf;
  g_plant.kbFresh = true;
  g_tgtGate = TargetGate::Normal;
  dwinSetPage(PAGE_MAIN);
  delay(20);
  invalidateCaches();
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  g_cacheTarget = g_plant.targetM;
  forcePushRemainProgress();
  pushSpeed();
}

static void actKbCancel() {
  g_plant.kbBuf = g_plant.targetM;
  g_plant.kbFresh = true;
  dwinSetPage(PAGE_MAIN);
  delay(20);
  invalidateCaches();
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  g_cacheTarget = g_plant.targetM;
  forcePushRemainProgress();
  pushSpeed();
}

static void actSettingsOpen() {
  forceSpeedZero();
  // Pic_Next=17 already switched the page. Do NOT call dwinSetPage here —
  // UART page switch fights DGUS compositing ("settings under main").
  delay(30);
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  pushSettings();
}

static void actSettingsBack() {
  // VarInput OK writes the panel VP immediately; MCU only sees it via 0x83.
  // Pull before leaving Settings so a quick НАЗАД cannot skip EEPROM save.
  pullSettingsFromPanel(250);
  dwinSetPage(PAGE_MAIN);
  delay(20);
  dwinSetPage(PAGE_MAIN);
  invalidateCaches();
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  g_cacheTarget = g_plant.targetM;
  forcePushRemainProgress();
  pushSpeed();
}

static void actTargetClamp() {
  g_plant.travelM = targetCm();
  forcePushRemainProgress();
}

/** Enter Run after operator starts the shaft (first encoder pulses). */
static bool tryArmRunFromMotion() {
  if (g_plant.targetM == 0) return false;
  if (g_plant.travelM >= targetCm()) return false;
  const uint8_t pendingRev = g_reverseHit;
  actPrepareRun();
  if (pendingRev) g_reverseHit = 1;
  g_q = FsmState::Run;
  return true;
}

void fsmDispatch(const FsmEventData& ev) {
  // Ошибки датчика — только в Run (вал уже крутится).
  if (g_q == FsmState::Run) {
    if (ev.type == FsmEvent::ReverseDetect) {
      actShowError(FsmError::Reverse);
      g_q = FsmState::Error;
      return;
    }
    if (ev.type == FsmEvent::EncLoss) {
      actShowError(FsmError::NoEncoder);
      g_q = FsmState::Error;
      return;
    }
    if (ev.type == FsmEvent::SpeedJumpDetect) {
      actShowError(FsmError::SpeedJump);
      g_q = FsmState::Error;
      return;
    }
    if (ev.type == FsmEvent::ChannelFaultDetect) {
      actShowError(FsmError::ChannelFault);
      g_q = FsmState::Error;
      return;
    }
  }

    // If panel already jumped to page 16 (Pic_Next) but MCU still Idle/Stopped,
    // НАЗАД / settings VP must land in Settings instead of being ignored.
    // (VarInput edits are applied via applySettingsVp, not keypad events.)

  const bool isKb = ev.type == FsmEvent::KbDigit || ev.type == FsmEvent::KbDel ||
                    ev.type == FsmEvent::KbOk || ev.type == FsmEvent::KbCancel;
  if (isKb && g_q != FsmState::Keypad && g_q != FsmState::Error &&
      g_q != FsmState::Settings) {
    actKbOpen();
    g_q = FsmState::Keypad;
  }

  FsmState q = g_q;
  FsmState qn = q;

  switch (q) {
    case FsmState::Idle:
      switch (ev.type) {
        case FsmEvent::Stop:
          actFreezeSpeed();
          qn = FsmState::Stopped;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          qn = FsmState::Idle;
          break;
        case FsmEvent::KbOpen:
          actKbOpen();
          qn = FsmState::Keypad;
          break;
        case FsmEvent::SettingsOpen:
          actSettingsOpen();
          qn = FsmState::Settings;
          break;
        case FsmEvent::SettingsBack:
          actSettingsBack();
          qn = FsmState::Idle;
          break;
        default:
          break;
      }
      break;

    case FsmState::Keypad:
      switch (ev.type) {
        case FsmEvent::KbDigit:
          actKbDigit(ev.digit);
          qn = FsmState::Keypad;
          break;
        case FsmEvent::KbDel:
          actKbDel();
          qn = FsmState::Keypad;
          break;
        case FsmEvent::KbOk:
          actKbCommit();
          qn = FsmState::Idle;
          break;
        case FsmEvent::KbCancel:
          actKbCancel();
          qn = FsmState::Idle;
          break;
        case FsmEvent::KbOpen:
          actKbOpen();
          qn = FsmState::Keypad;
          break;
        case FsmEvent::SettingsOpen:
          actSettingsOpen();
          qn = FsmState::Settings;
          break;
        case FsmEvent::Stop:
          actFreezeSpeed();
          qn = FsmState::Stopped;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          qn = FsmState::Idle;
          break;
        default:
          break;
      }
      break;

    case FsmState::Run:
      switch (ev.type) {
        case FsmEvent::Stop:
        case FsmEvent::TargetDone:
          if (ev.type == FsmEvent::TargetDone) actTargetClamp();
          actFreezeSpeed();
          qn = FsmState::Stopped;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          qn = FsmState::Idle;
          break;
        case FsmEvent::KbOpen:
          actKbOpen();
          qn = FsmState::Keypad;
          break;
        case FsmEvent::SettingsOpen:
          actFreezeSpeed();
          actSettingsOpen();
          qn = FsmState::Settings;
          break;
        default:
          break;
      }
      break;

    case FsmState::Stopped:
      switch (ev.type) {
        case FsmEvent::Stop:
          actFreezeSpeed();
          qn = FsmState::Stopped;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          qn = FsmState::Idle;
          break;
        case FsmEvent::KbOpen:
          actKbOpen();
          qn = FsmState::Keypad;
          break;
        case FsmEvent::SettingsOpen:
          actSettingsOpen();
          qn = FsmState::Settings;
          break;
        default:
          break;
      }
      break;

    case FsmState::Error:
      switch (ev.type) {
        case FsmEvent::ErrAck:
          actDismissError();
          qn = FsmState::Idle;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          actDismissError();
          qn = FsmState::Idle;
          break;
        case FsmEvent::Stop:
          actFreezeSpeed();
          qn = FsmState::Error;
          break;
        default:
          break;
      }
      break;

    case FsmState::Settings:
      switch (ev.type) {
        case FsmEvent::SettingsBack:
          actSettingsBack();
          qn = FsmState::Idle;
          break;
        case FsmEvent::Stop:
          actSettingsBack();
          actKbCancel();
          qn = FsmState::Idle;
          break;
        case FsmEvent::Reset:
          actClearPlant();
          actKbCancel();
          qn = FsmState::Idle;
          break;
        case FsmEvent::SettingsOpen:
          qn = FsmState::Settings;
          break;
        default:
          break;
      }
      break;
  }

  g_q = qn;
}

static bool mapVpToEvent(uint16_t vp, FsmEventData& ev) {
  ev.digit = 0;
  switch (vp) {
    case VP_STOP: ev.type = FsmEvent::Stop; return true;
    case VP_RESET: ev.type = FsmEvent::Reset; return true;
    case VP_ERR_ACK: ev.type = FsmEvent::ErrAck; return true;
    case VP_SETTINGS: ev.type = FsmEvent::SettingsOpen; return true;
    case VP_SETTINGS_BACK: ev.type = FsmEvent::SettingsBack; return true;
    case VP_KB_OPEN: ev.type = FsmEvent::KbOpen; return true;
    case VP_KB_OK: ev.type = FsmEvent::KbOk; return true;
    case VP_KB_CANCEL: ev.type = FsmEvent::KbCancel; return true;
    case VP_KB_DEL: ev.type = FsmEvent::KbDel; return true;
    case VP_KB_0: ev.type = FsmEvent::KbDigit; ev.digit = 0; return true;
    case VP_KB_1: ev.type = FsmEvent::KbDigit; ev.digit = 1; return true;
    case VP_KB_2: ev.type = FsmEvent::KbDigit; ev.digit = 2; return true;
    case VP_KB_3: ev.type = FsmEvent::KbDigit; ev.digit = 3; return true;
    case VP_KB_4: ev.type = FsmEvent::KbDigit; ev.digit = 4; return true;
    case VP_KB_5: ev.type = FsmEvent::KbDigit; ev.digit = 5; return true;
    case VP_KB_6: ev.type = FsmEvent::KbDigit; ev.digit = 6; return true;
    case VP_KB_7: ev.type = FsmEvent::KbDigit; ev.digit = 7; return true;
    case VP_KB_8: ev.type = FsmEvent::KbDigit; ev.digit = 8; return true;
    case VP_KB_9: ev.type = FsmEvent::KbDigit; ev.digit = 9; return true;
    default: return false;
  }
}

static bool acceptPress(uint16_t vp, uint16_t value, uint32_t nowMs) {
  static uint16_t heldVp[24];
  static uint8_t nHeld = 0;
  static uint16_t fireVp[24];
  static uint32_t fireMs[24];
  static uint8_t nFire = 0;

  int hi = -1;
  for (uint8_t i = 0; i < nHeld; ++i) {
    if (heldVp[i] == vp) {
      hi = (int)i;
      break;
    }
  }

  if (value == 0) {
    if (hi >= 0) {
      heldVp[hi] = heldVp[nHeld - 1];
      nHeld--;
    }
    return false;
  }
  if (hi >= 0) return false;

  for (uint8_t i = 0; i < nFire; ++i) {
    if (fireVp[i] == vp && (nowMs - fireMs[i]) < BUTTON_DEBOUNCE_MS) return false;
  }

  if (nHeld < 24) heldVp[nHeld++] = vp;

  bool updated = false;
  for (uint8_t i = 0; i < nFire; ++i) {
    if (fireVp[i] == vp) {
      fireMs[i] = nowMs;
      updated = true;
      break;
    }
  }
  if (!updated && nFire < 24) {
    fireVp[nFire] = vp;
    fireMs[nFire] = nowMs;
    nFire++;
  }
  return true;
}

void fsmOnDwinVp(uint16_t vp, uint32_t value) {
  if (vp == VP_TARGET) {
    if (value > MAX_METERS) value = MAX_METERS;

    if (g_tgtGate == TargetGate::Resetting) {
      if (value == 0) {
        g_tgtGate = TargetGate::Armed;
        g_plant.targetM = 0;
        g_cacheTarget = 0;
        forcePushRemainProgress();
      } else {
        writeTargetZero();
      }
      return;
    }

    if (g_tgtGate == TargetGate::Armed) {
      if (value == 0) {
        g_plant.targetM = 0;
        g_cacheTarget = 0;
        return;
      }
      g_tgtGate = TargetGate::Normal;
      g_plant.targetM = value;
      g_cacheTarget = value;
      forcePushRemainProgress();
      return;
    }

    if (value == g_plant.targetM && value == g_cacheTarget) return;
    g_plant.targetM = value;
    g_cacheTarget = value;
    forcePushRemainProgress();
    return;
  }

  if (vp == VP_BRAKE || vp == VP_BRAKE_ON_MS || vp == VP_BRAKE_OFF_MS) {
    applySettingsVp(vp, value);
    return;
  }

  if ((vp == VP_RESET || vp == VP_ERR_ACK || vp == VP_SETTINGS ||
       vp == VP_SETTINGS_BACK) &&
      value != 0) {
    dwinWriteU16(vp, 0);
    FsmEventData ev{};
    if (vp == VP_RESET) {
      ev.type = FsmEvent::Reset;
    } else if (vp == VP_ERR_ACK) {
      ev.type = FsmEvent::ErrAck;
    } else if (vp == VP_SETTINGS_BACK) {
      ev.type = FsmEvent::SettingsBack;
    } else {
      ev.type = FsmEvent::SettingsOpen;
    }
    ev.digit = 0;
    fsmDispatch(ev);
    return;
  }

  const uint32_t now = millis();
  if (!acceptPress(vp, (uint16_t)value, now)) return;

  FsmEventData ev{};
  if (!mapVpToEvent(vp, ev)) return;

  dwinClearCmd(vp);
  fsmDispatch(ev);
}

void fsmPollButtons(uint32_t nowMs) {
  static const uint16_t kAlt[] = {VP_STOP, VP_ERR_ACK, VP_SETTINGS, VP_SETTINGS_BACK};
  static uint8_t idx = 0;
  static uint32_t tLast = 0;
  static uint32_t tTarget = 0;
  static uint32_t tResetHold = 0;

  if (nowMs - tLast >= BUTTON_POLL_MS) {
    tLast = nowMs;
    dwinRequestReadU16(VP_RESET);
    dwinRequestReadU16(kAlt[idx % 4u]);
    idx = (uint8_t)(idx + 1u);
  }

  // Пока Resetting — постоянно пишем нули и читаем ЗАДАНО, пока не станет 0.
  if (g_tgtGate == TargetGate::Resetting) {
    if (nowMs - tResetHold >= 80u) {
      tResetHold = nowMs;
      writeAllDisplayZeros();
      dwinRequestReadU32(VP_TARGET);
    }
    if (nowMs - g_tgtResetStartMs >= 5000u) {
      g_tgtGate = TargetGate::Armed;
      writeAllDisplayZeros();
    }
    return;
  }

  if (nowMs - tTarget >= TARGET_POLL_MS) {
    tTarget = nowMs;
    if (g_q == FsmState::Settings) {
      // All four every tick — OK→VP must land in RAM/EEPROM before НАЗАД.
      requestSettingsReads();
    } else if (g_q != FsmState::Keypad && g_q != FsmState::Error &&
               g_tgtGate == TargetGate::Normal) {
      dwinRequestReadU32(VP_TARGET);
    }
  }
}

void fsmMotionTick(uint32_t nowMs) {
  static uint32_t lastSpeedMs = 0;
  static uint32_t lastSpeedUs = 0;
  static uint32_t lastTravelMs = 0;

  // Hardware TIM2 → soft path / reverse (no per-pulse ISR).
  encoderPollHw(nowMs);

  if (lastSpeedMs == 0) {
    lastSpeedMs = nowMs;
    lastTravelMs = nowMs;
    lastSpeedUs = micros();
    g_lastPulseMs = nowMs;
    channelReset(nowMs);
    return;
  }

  if (g_q == FsmState::Error) {
    forceSpeedZero();
    return;
  }

  // ЗАДАНО задано → вал крутят вручную → первый импульс = вход в Run.
  if ((g_q == FsmState::Idle || g_q == FsmState::Stopped) && g_encActivity) {
    tryArmRunFromMotion();
  }

  uint8_t rev = g_reverseHit;
  if (rev) g_reverseHit = 0;
  if (rev && g_q == FsmState::Run) {
    FsmEventData ev{FsmEvent::ReverseDetect, 0};
    fsmDispatch(ev);
    if (g_q == FsmState::Error) return;
  }

  // Обрыв A/B: IDR PA0/PA1 + асимметрия фронтов при живом TIM.
  if (ENC_CH_FAULT_ENABLE && g_q == FsmState::Run && channelPollFault(nowMs)) {
    FsmEventData ev{FsmEvent::ChannelFaultDetect, 0};
    fsmDispatch(ev);
    return;
  }

  // Смена CNT = импульсы есть
  if (g_q == FsmState::Run) {
    const uint32_t cnt = encTim2Cnt();
    if (cnt != g_encCntWatch) {
      g_encCntWatch = cnt;
      g_lastPulseMs = nowMs;
      g_encSeenThisRun = true;
    }
  }

  // СТАРТ (Run) + нет импульсов ENC_NO_SIGNAL_MS → ошибка энкодера.
  // Signed delta: if START set g_lastPulseMs via millis() after this loop's nowMs
  // was sampled, unsigned (nowMs - last) wraps to ~4e9 and falsely trips.
  if (ENC_NO_SIGNAL_ENABLE && g_q == FsmState::Run) {
    const int32_t quietMs = (int32_t)(nowMs - g_lastPulseMs);
    if (quietMs >= (int32_t)ENC_NO_SIGNAL_MS) {
      FsmEventData ev{FsmEvent::EncLoss, 0};
      fsmDispatch(ev);
      return;
    }
  }

  if (nowMs - lastSpeedMs >= SPEED_PERIOD_MS) {
    const uint32_t nowUs = micros();
    uint32_t dtUs = nowUs - lastSpeedUs;
    lastSpeedUs = nowUs;
    lastSpeedMs = nowMs;
    if (dtUs == 0) dtUs = (uint32_t)SPEED_PERIOD_MS * 1000u;

    const uint32_t win = g_pulseWin;
    g_pulseWin = 0;

    if (win > 0) g_lastPulseMs = nowMs;

    const bool onKeypad = (g_q == FsmState::Keypad || g_q == FsmState::Settings);
    const bool live = fsmSpeedLive();
    const int32_t sincePulse = (int32_t)(nowMs - g_lastPulseMs);
    const bool idleStop = (sincePulse >= (int32_t)SPEED_IDLE_ZERO_MS);

    if (!live || onKeypad || idleStop) {
      forceSpeedZero();
    } else if (win > 0) {
      const uint32_t speedInst =
          (uint32_t)((win * (uint64_t)NM_PER_COUNT) / ((uint64_t)dtUs * 10ULL));

      // Скачок скорости — выкл. по умолчанию
      if (SPEED_JUMP_ENABLE && g_q == FsmState::Run && g_speedShown &&
          g_speedEma >= SPEED_JUMP_MIN_EMA_CMS &&
          (nowMs - g_runStartMs) >= SPEED_JUMP_ARM_MS) {
        const uint32_t limRatio =
            (uint32_t)g_speedEma * (uint32_t)SPEED_JUMP_RATIO;
        const uint32_t limAbs = g_speedEma + (uint32_t)SPEED_JUMP_ABS_CMS;
        const uint32_t lim = (limRatio > limAbs) ? limRatio : limAbs;
        if (speedInst > lim) {
          FsmEventData ev{FsmEvent::SpeedJumpDetect, 0};
          fsmDispatch(ev);
          return;
        }
      }

      g_speedEma =
          (speedInst + (uint32_t)(SPEED_EMA_N - 1u) * g_speedEma) / SPEED_EMA_N;

      g_plant.speedCms = (uint16_t)min(g_speedEma, (uint32_t)MAX_SPEED_CMS);
      g_speedShown = true;
      pushSpeed();
    }
  }

  if (nowMs - lastTravelMs >= TRAVEL_PERIOD_MS) {
    lastTravelMs = nowMs;

    g_plant.travelM = nmToTravelCm(g_nm);
    if (g_q != FsmState::Keypad && g_q != FsmState::Settings) pushTravel();

    if (g_q == FsmState::Run && g_plant.targetM > 0 &&
        g_plant.travelM >= targetCm()) {
      FsmEventData ev{FsmEvent::TargetDone, 0};
      fsmDispatch(ev);
    }
  }
}

void fsmBegin() {
  encoderAttach();
  g_q = FsmState::Idle;
  g_tgtGate = TargetGate::Normal;
  invalidateCaches();
  g_plant = {};
  g_plant.kbFresh = true;
  g_plant.brakeOnMs = 50;
  g_plant.brakeOffMs = 50;
  if (!settingsLoad(g_plant)) {
    // First boot / empty flash — keep defaults and seed EEPROM.
    settingsSave(g_plant);
  }
  encoderClear();
  forceSpeedZero();
  writeAllDisplayZeros();
  g_cacheTarget = g_cacheRemain = 0;
  g_cacheProgress = g_cacheSpeed = 0;
  // Invalidate settings caches so first push always hits the panel.
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  pushSettings();
  delay(40);
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  pushSettings();
  dwinSetPage(PAGE_MAIN);
}
