#include "fsm.h"
#include "dwin.h"
#include "enc_tim2.h"

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
static void encoderPollHw() {
  const EncTim2Delta d = encTim2Poll();
  // d.overflow: UIF handled inside encTim2Poll (CNT reset). Soft g_nm untouched by that.

  if (d.forward > 0) {
    g_revStreak = 0;
    g_pulseWin += d.forward;
    g_nm += (uint64_t)d.forward * (uint64_t)NM_PER_COUNT;
    g_lastPulseMs = millis();
  }

  if (d.reverse > 0) {
    g_lastPulseMs = millis();
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
static uint32_t g_rpmEma = 0;
static uint32_t g_speedEma = 0;

// Channel A/B integrity (GPIO sample while TIM2 AF active)
static int g_chALevel = -1;
static int g_chBLevel = -1;
static uint32_t g_chAEdgeMs = 0;
static uint32_t g_chBEdgeMs = 0;
static uint8_t g_chAEdges = 0;
static uint8_t g_chBEdges = 0;

// После СБРОС: не принимать старое ЗАДАНО, пока панель не отдаст 0,
// затем принимать только новое ненулевое (ввод пользователя).
enum class TargetGate : uint8_t { Normal = 0, Resetting, Armed };
static TargetGate g_tgtGate = TargetGate::Normal;
static uint32_t g_tgtResetStartMs = 0;

static uint32_t g_cacheTarget = 0xFFFFFFFFu;
static uint32_t g_cacheRemain = 0xFFFFFFFFu;
static uint16_t g_cacheSpeed = 0xFFFF;
static uint16_t g_cacheRpm = 0xFFFF;
static uint16_t g_cacheProgress = 0xFFFF;
static uint16_t g_cacheKb = 0xFFFF;

static void invalidateCaches() {
  g_cacheTarget = g_cacheRemain = 0xFFFFFFFFu;
  g_cacheSpeed = g_cacheRpm = g_cacheProgress = g_cacheKb = 0xFFFF;
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
  dwinWriteU16(VP_RPM, 0);
}

static void pushKb() {
  const uint16_t v = (uint16_t)min(g_plant.kbBuf, (uint32_t)MAX_METERS);
  dwinWriteU16(VP_KB_BUF, v);
  dwinWriteU16(VP_KB_BUF, v);
  g_cacheKb = v;
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

static void pushSpeedRpm() {
  dwinWriteU16IfChanged(VP_SPEED, g_plant.speedCms, g_cacheSpeed);
  dwinWriteU16IfChanged(VP_RPM, g_plant.rpm, g_cacheRpm);
}

static void forceSpeedZero() {
  g_plant.speedCms = 0;
  g_plant.rpm = 0;
  g_rpmEma = 0;
  g_speedEma = 0;
  if (g_speedShown || g_cacheSpeed != 0 || g_cacheRpm != 0) {
    dwinWriteU16(VP_SPEED, 0);
    dwinWriteU16(VP_RPM, 0);
    g_cacheSpeed = 0;
    g_cacheRpm = 0;
    g_speedShown = false;
  }
}

static void pushMotion() {
  pushTravel();
  pushSpeedRpm();
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
  if (g_q == FsmState::Keypad) return;
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
  encoderClearWindow();
  dwinWriteU16(VP_ERR_ACK, 0);
  dwinSetPage(PAGE_MAIN);
  delay(20);
  forcePushRemainProgress();
  pushSpeedRpm();
}

static void channelReset(uint32_t nowMs) {
  g_chALevel = digitalRead(PIN_ENC_A);
  g_chBLevel = digitalRead(PIN_ENC_B);
  g_chAEdgeMs = nowMs;
  g_chBEdgeMs = nowMs;
  g_chAEdges = 0;
  g_chBEdges = 0;
}

/** Sample PA0/PA1 edges; if one side moves and the other is dead → ChannelFault. */
static bool channelPollFault(uint32_t nowMs) {
  const int a = digitalRead(PIN_ENC_A);
  const int b = digitalRead(PIN_ENC_B);
  if (g_chALevel < 0) {
    channelReset(nowMs);
    return false;
  }
  if (a != g_chALevel) {
    g_chALevel = a;
    g_chAEdgeMs = nowMs;
    if (g_chAEdges < 255) g_chAEdges++;
  }
  if (b != g_chBLevel) {
    g_chBLevel = b;
    g_chBEdgeMs = nowMs;
    if (g_chBEdges < 255) g_chBEdges++;
  }
  if (g_chAEdges >= ENC_CH_MIN_EDGES &&
      (nowMs - g_chBEdgeMs) >= ENC_CH_DEAD_MS) {
    return true;
  }
  if (g_chBEdges >= ENC_CH_MIN_EDGES &&
      (nowMs - g_chAEdgeMs) >= ENC_CH_DEAD_MS) {
    return true;
  }
  return false;
}

static void actClearPlant() {
  g_plant.targetM = 0;
  g_plant.travelM = 0;
  g_plant.speedCms = 0;
  g_plant.rpm = 0;
  g_plant.progressPct = 0;
  g_plant.kbBuf = 0;
  g_plant.kbFresh = true;
  encoderClear();
  g_rpmEma = 0;
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
  g_cacheRpm = 0;

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
  channelReset(millis());
  g_lastPulseMs = millis();  // grace window for ENC_NO_SIGNAL_MS
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
  pushSpeedRpm();
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
  pushSpeedRpm();
}

static void actTargetClamp() {
  g_plant.travelM = targetCm();
  forcePushRemainProgress();
}

static FsmState tryStartRun() {
  if (g_plant.targetM == 0) {
    actShowError(FsmError::NoTarget);
    return FsmState::Error;
  }
  actPrepareRun();
  return FsmState::Run;
}

void fsmDispatch(const FsmEventData& ev) {
  // Ошибки датчика — из любого режима (кроме уже на экране ошибки).
  if (g_q != FsmState::Error) {
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

  const bool isKb = ev.type == FsmEvent::KbDigit || ev.type == FsmEvent::KbDel ||
                    ev.type == FsmEvent::KbOk || ev.type == FsmEvent::KbCancel;
  if (isKb && g_q != FsmState::Keypad && g_q != FsmState::Error) {
    actKbOpen();
    g_q = FsmState::Keypad;
  }

  FsmState q = g_q;
  FsmState qn = q;

  switch (q) {
    case FsmState::Idle:
      switch (ev.type) {
        case FsmEvent::Start:
          qn = tryStartRun();
          break;
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
        case FsmEvent::Start:
          actKbCommit();
          qn = tryStartRun();
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
        case FsmEvent::Start:
          qn = tryStartRun();
          break;
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
        default:
          break;
      }
      break;

    case FsmState::Stopped:
      switch (ev.type) {
        case FsmEvent::Start:
          qn = tryStartRun();
          break;
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
        default:
          break;
      }
      break;

    case FsmState::Error:
      switch (ev.type) {
        case FsmEvent::ErrAck:
        case FsmEvent::Start:
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
  }

  g_q = qn;
}

static bool mapVpToEvent(uint16_t vp, FsmEventData& ev) {
  ev.digit = 0;
  switch (vp) {
    case VP_START: ev.type = FsmEvent::Start; return true;
    case VP_STOP: ev.type = FsmEvent::Stop; return true;
    case VP_RESET: ev.type = FsmEvent::Reset; return true;
    case VP_ERR_ACK: ev.type = FsmEvent::ErrAck; return true;
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

  if ((vp == VP_RESET || vp == VP_ERR_ACK) && value != 0) {
    dwinWriteU16(vp, 0);
    FsmEventData ev{};
    ev.type = (vp == VP_RESET) ? FsmEvent::Reset : FsmEvent::ErrAck;
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
  static const uint16_t kAlt[] = {VP_START, VP_STOP, VP_ERR_ACK};
  static uint8_t idx = 0;
  static uint32_t tLast = 0;
  static uint32_t tTarget = 0;
  static uint32_t tResetHold = 0;

  if (nowMs - tLast >= BUTTON_POLL_MS) {
    tLast = nowMs;
    dwinRequestReadU16(VP_RESET);
    dwinRequestReadU16(kAlt[idx % 3u]);
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
    if (g_q != FsmState::Keypad && g_q != FsmState::Error &&
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
  encoderPollHw();

  uint8_t rev = g_reverseHit;
  if (rev) g_reverseHit = 0;
  if (rev) {
    FsmEventData ev{FsmEvent::ReverseDetect, 0};
    fsmDispatch(ev);
  }

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

  // Channel A/B integrity while running
  if (g_q == FsmState::Run && channelPollFault(nowMs)) {
    FsmEventData ev{FsmEvent::ChannelFaultDetect, 0};
    fsmDispatch(ev);
    return;
  }

  // No TIM activity in Run
  if (g_q == FsmState::Run &&
      (nowMs - g_lastPulseMs) >= ENC_NO_SIGNAL_MS) {
    FsmEventData ev{FsmEvent::EncLoss, 0};
    fsmDispatch(ev);
    return;
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

    const bool onKeypad = (g_q == FsmState::Keypad);
    const bool live = fsmSpeedLive();
    const bool idleStop = (nowMs - g_lastPulseMs >= SPEED_IDLE_ZERO_MS);

    if (!live || onKeypad || idleStop) {
      forceSpeedZero();
    } else if (win > 0) {
      // TIM counts @ ENC_COUNTS_PER_REV per shaft revolution
      const uint32_t rpmInst = (uint32_t)((win * 60000000ULL) /
                                          ((uint64_t)ENC_COUNTS_PER_REV * (uint64_t)dtUs));
      const uint32_t speedInst =
          (uint32_t)((win * (uint64_t)NM_PER_COUNT) / ((uint64_t)dtUs * 10ULL));

      // Скачок скорости относительно EMA
      if (g_q == FsmState::Run && g_speedShown &&
          g_speedEma >= SPEED_JUMP_MIN_EMA_CMS) {
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

      g_rpmEma = (rpmInst + (uint32_t)(SPEED_EMA_N - 1u) * g_rpmEma) / SPEED_EMA_N;
      g_speedEma =
          (speedInst + (uint32_t)(SPEED_EMA_N - 1u) * g_speedEma) / SPEED_EMA_N;

      g_plant.rpm = (uint16_t)min(g_rpmEma, (uint32_t)MAX_RPM);
      g_plant.speedCms = (uint16_t)min(g_speedEma, (uint32_t)MAX_SPEED_CMS);
      g_speedShown = true;
      pushSpeedRpm();
    }
  }

  if (nowMs - lastTravelMs >= TRAVEL_PERIOD_MS) {
    lastTravelMs = nowMs;

    g_plant.travelM = nmToTravelCm(g_nm);
    if (g_q != FsmState::Keypad) pushTravel();

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
  encoderClear();
  forceSpeedZero();
  writeAllDisplayZeros();
  g_cacheTarget = g_cacheRemain = 0;
  g_cacheProgress = g_cacheSpeed = g_cacheRpm = 0;
  dwinSetPage(PAGE_MAIN);
}
