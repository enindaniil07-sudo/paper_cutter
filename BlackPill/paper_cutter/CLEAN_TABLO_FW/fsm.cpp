#include "fsm.h"
#include "config.h"
#include "dwin.h"
#include "dwin_buzz.h"
#include "enc_tim2.h"
#include "enc_path.h"
#include "settings_store.h"
#include "brake_relay.h"
#include "brake_logic.h"
#include "plant_ctx.h"
#include "plant_ui.h"

/*
  FSM + motion tick. Encoder path → enc_path, brake → brake_logic,
  DWIN plant/settings → plant_ui, shared state → plant_ctx.
*/

uint16_t fsmLedPeriodMs() {
  if (g_q == FsmState::Error) return 60u;
  return (g_q == FsmState::Run) ? 100u : 450u;
}

static uint16_t pageForError(FsmError e) {
  switch (e) {
    case FsmError::Reverse: return PAGE_ERR_REVERSE;
    case FsmError::NoEncoder: return PAGE_ERR_NO_ENC;
    case FsmError::BrakeIneffective: return PAGE_ERR_BRAKE;
    default: return PAGE_ERR_REVERSE;
  }
}

static void actShowError(FsmError kind) {
  plantUiForceSpeedZero();
  encPathClearWindow();
  g_err = kind;
  dwinBuzzError();
  plantUiSetPageRetry(pageForError(kind), 20);
}

static void actDismissError() {
  encPathClearReverse();
  g_err = FsmError::None;
  brakeEffReset();
  encPathClearWindow();
  dwinWriteU16(VP_ERR_ACK, 0);
  dwinSetPage(PAGE_MAIN);
  delay(20);
  plantUiForceRemainProgress();
  plantUiPushSpeed();
}

static void actClearPlant() {
  g_plant.targetM = 0;
  g_plant.travelM = 0;
  g_plant.speedCms = 0;
  g_plant.progressPct = 0;
  g_plant.kbBuf = 0;
  g_plant.kbFresh = true;
  encPathClear();
  g_speedEma = 0;
  g_speedShown = true;
  plantUiForceSpeedZero();
  brakeLogicClearLatch();
  brakeEffReset();
  plantInvalidateCaches();

  g_tgtGate = TargetGate::Resetting;
  g_tgtResetStartMs = millis();
  plantUiWriteAllZeros();
  dwinWriteU16(VP_RESET, 0);
  plantUiPushKb();

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

static void actPrepareRun() {
  if (g_plant.targetM > 0 && g_plant.travelM >= plantTargetCm()) {
    g_plant.travelM = 0;
    encPathClear();
    plantUiForceRemainProgress();
  }
  encPathClearWindow();
  plantUiForceSpeedZero();
  brakeEffReset();
  const uint32_t t = millis();
  encPathSetLastPulseMs(t);
  encPathSyncCntWatch();
  encPathClearReverse();
}

static void actKbOpen() {
  g_plant.kbBuf = g_plant.targetM;
  g_plant.kbFresh = true;
  plantUiSetPageRetry(PAGE_KEYPAD, 50);
  delay(30);
  plantUiPushKb();
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
  plantUiPushKb();
}

static void actKbDel() {
  if (g_plant.kbFresh) {
    g_plant.kbBuf = 0;
    g_plant.kbFresh = false;
  } else {
    g_plant.kbBuf /= 10u;
  }
  plantUiPushKb();
}

static void leaveKeypadToMain(bool targetTwice) {
  dwinSetPage(PAGE_MAIN);
  delay(20);
  plantInvalidateCaches();
  if (targetTwice) {
    plantUiDwinWriteU32x2(VP_TARGET, g_plant.targetM);
  } else {
    dwinWriteU32(VP_TARGET, g_plant.targetM);
  }
  g_cacheTarget = g_plant.targetM;
  plantUiForceRemainProgress();
  plantUiPushSpeed();
}

static void actKbCommit() {
  if (g_plant.kbBuf > MAX_METERS) g_plant.kbBuf = MAX_METERS;
  g_plant.targetM = g_plant.kbBuf;
  g_plant.kbFresh = true;
  g_tgtGate = TargetGate::Normal;
  leaveKeypadToMain(true);
}

static void actKbCancel() {
  g_plant.kbBuf = g_plant.targetM;
  g_plant.kbFresh = true;
  leaveKeypadToMain(false);
}

static void actSettingsOpen() {
  plantUiForceSpeedZero();
  // Pic_Next=17 already switched — do not dwinSetPage (layer fight).
  delay(30);
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  g_cacheEncInvert = 0xFFFF;
  plantUiPushSettings();
}

static void actSettingsBack() {
  plantUiPullSettings(200, fsmOnDwinVp);
  plantInvalidateCaches();
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  g_cacheTarget = g_plant.targetM;
  plantUiForceRemainProgress();
  plantUiPushSpeed();
}

static void actTargetClamp() {
  g_plant.travelM = plantTargetCm();
  plantUiForceRemainProgress();
}

static bool tryArmRunFromMotion() {
  if (g_plant.targetM == 0) return false;
  if (g_plant.travelM >= plantTargetCm()) return false;
  const uint8_t pendingRev = encPathTakeReverseHit();
  actPrepareRun();
  if (pendingRev) encPathSetReverseHit();
  g_q = FsmState::Run;
  return true;
}

static void fsmDispatch(const FsmEventData& ev) {
  if (ev.type == FsmEvent::Stop || ev.type == FsmEvent::TargetDone) {
    dwinBuzzStopTriple();
  }

  // Sensor / brake faults before UI transitions.
  if (ev.type == FsmEvent::BrakeIneffectiveDetect) {
    if (g_q == FsmState::Run || g_q == FsmState::Stopped) {
      actShowError(FsmError::BrakeIneffective);
      g_q = FsmState::Error;
    }
    return;
  }
  if (ev.type == FsmEvent::ReverseDetect) {
    if (g_q == FsmState::Run) {
      actShowError(FsmError::Reverse);
      g_q = FsmState::Error;
    }
    return;
  }
  if (ev.type == FsmEvent::EncLoss) {
    if (g_q == FsmState::Run) {
      actShowError(FsmError::NoEncoder);
      g_q = FsmState::Error;
    }
    return;
  }

  // Digit/Del/Ok/Cancel from main → open keypad first.
  const bool isKb = ev.type == FsmEvent::KbDigit || ev.type == FsmEvent::KbDel ||
                    ev.type == FsmEvent::KbOk || ev.type == FsmEvent::KbCancel;
  if (isKb && g_q != FsmState::Keypad && g_q != FsmState::Error &&
      g_q != FsmState::Settings) {
    actKbOpen();
    g_q = FsmState::Keypad;
  }

  FsmState qn = g_q;

  switch (ev.type) {
    case FsmEvent::Stop:
      if (g_q == FsmState::Settings) {
        plantUiPullSettings(200, fsmOnDwinVp);
        dwinSetPage(PAGE_MAIN);
        plantUiForceSpeedZero();
        qn = FsmState::Idle;
      } else if (g_q == FsmState::Error) {
        plantUiForceSpeedZero();
        qn = FsmState::Error;
      } else if (g_q == FsmState::Run || g_q == FsmState::Stopped) {
        if (!brakeLogicIsLatched()) plantUiForceSpeedZero();
        qn = FsmState::Stopped;
      } else if (g_q == FsmState::Idle || g_q == FsmState::Keypad) {
        plantUiForceSpeedZero();
        qn = FsmState::Stopped;
      }
      break;

    case FsmEvent::Reset:
      if (g_q == FsmState::Settings) {
        plantUiPullSettings(100, fsmOnDwinVp);
        actClearPlant();
        dwinSetPage(PAGE_MAIN);
      } else if (g_q == FsmState::Error) {
        actClearPlant();
        actDismissError();
      } else {
        actClearPlant();
      }
      qn = FsmState::Idle;
      break;

    case FsmEvent::KbOpen:
      if (g_q == FsmState::Error || g_q == FsmState::Settings) break;
      actKbOpen();
      qn = FsmState::Keypad;
      break;

    case FsmEvent::SettingsOpen:
      if (g_q == FsmState::Error) break;
      if (g_q == FsmState::Settings) {
        qn = FsmState::Settings;
        break;
      }
      if (g_q == FsmState::Run && !brakeLogicIsLatched()) {
        plantUiForceSpeedZero();
      }
      actSettingsOpen();
      qn = FsmState::Settings;
      break;

    case FsmEvent::SettingsBack:
      if (g_q != FsmState::Settings) break;  // ignore stale VP in Idle
      actSettingsBack();
      qn = FsmState::Idle;
      break;

    case FsmEvent::KbDigit:
      if (g_q != FsmState::Keypad) break;
      actKbDigit(ev.digit);
      qn = FsmState::Keypad;
      break;

    case FsmEvent::KbDel:
      if (g_q != FsmState::Keypad) break;
      actKbDel();
      qn = FsmState::Keypad;
      break;

    case FsmEvent::KbOk:
      if (g_q != FsmState::Keypad) break;
      actKbCommit();
      qn = FsmState::Idle;
      break;

    case FsmEvent::KbCancel:
      if (g_q != FsmState::Keypad) break;
      actKbCancel();
      qn = FsmState::Idle;
      break;

    case FsmEvent::TargetDone:
      if (g_q != FsmState::Run) break;
      actTargetClamp();
      qn = FsmState::Stopped;
      break;

    case FsmEvent::ErrAck:
      if (g_q != FsmState::Error) break;
      actDismissError();
      qn = FsmState::Idle;
      break;

    default:
      break;
  }

  g_q = qn;
}

static bool mapVpToEvent(uint16_t vp, FsmEventData& ev) {
  ev.digit = 0;
  if (vp >= VP_KB_1 && vp <= VP_KB_9) {
    ev.type = FsmEvent::KbDigit;
    ev.digit = (uint8_t)(vp - VP_KB_1 + 1u);
    return true;
  }
  if (vp == VP_KB_0) {
    ev.type = FsmEvent::KbDigit;
    ev.digit = 0;
    return true;
  }
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
        plantUiForceRemainProgress();
      } else {
        plantUiWriteTargetZero();
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
      plantUiForceRemainProgress();
      return;
    }

    if (value == g_plant.targetM && value == g_cacheTarget) return;
    g_plant.targetM = value;
    g_cacheTarget = value;
    plantUiForceRemainProgress();
    return;
  }

  if (vp == VP_BRAKE || vp == (uint16_t)(VP_BRAKE + 1u) || vp == VP_BRAKE_ON_MS ||
      vp == VP_BRAKE_OFF_MS || vp == VP_ENC_INVERT) {
    plantUiOnSettingsVp(vp, value);
    return;
  }

  if (vp == VP_RESET || vp == VP_ERR_ACK || vp == VP_SETTINGS ||
      vp == VP_SETTINGS_BACK) {
    const uint32_t now = millis();
    if (!acceptPress(vp, (uint16_t)value, now)) return;
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

  if (g_tgtGate == TargetGate::Resetting) {
    if (nowMs - tResetHold >= 80u) {
      tResetHold = nowMs;
      plantUiWriteAllZeros();
      dwinRequestReadU32(VP_TARGET);
    }
    if (nowMs - g_tgtResetStartMs >= RESET_TARGET_LOCK_MS) {
      g_tgtGate = TargetGate::Armed;
      plantUiWriteAllZeros();
    }
    return;
  }

  if (nowMs - tTarget >= TARGET_POLL_MS) {
    tTarget = nowMs;
    if (g_q == FsmState::Settings) {
      plantUiRequestSettingsReads();
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

  encPathPoll(nowMs);

  if (lastSpeedMs == 0) {
    lastSpeedMs = nowMs;
    lastTravelMs = nowMs;
    lastSpeedUs = micros();
    encPathSetLastPulseMs(nowMs);
    return;
  }

  if (g_q == FsmState::Error) {
    plantUiForceSpeedZero();
    if (g_err == FsmError::Reverse || g_err == FsmError::BrakeIneffective) {
      brakeLogicUpdateRelay(nowMs);
    } else {
      brakeRelayOff();
    }
    return;
  }

  if ((g_q == FsmState::Idle || g_q == FsmState::Stopped) && encPathActivity()) {
    tryArmRunFromMotion();
  }

  if (encPathTakeReverseHit() && g_q == FsmState::Run) {
    FsmEventData ev{FsmEvent::ReverseDetect, 0};
    fsmDispatch(ev);
    if (g_q == FsmState::Error) {
      brakeLogicUpdateRelay(nowMs);
      return;
    }
  }

  if (g_q == FsmState::Run || brakeLogicIsLatched()) {
    encPathNoteCntPulse(nowMs);
  }

  if (ENC_NO_SIGNAL_ENABLE && g_q == FsmState::Run) {
    const int32_t quietMs = (int32_t)(nowMs - encPathLastPulseMs());
    if (quietMs >= (int32_t)ENC_NO_SIGNAL_MS) {
      FsmEventData ev{FsmEvent::EncLoss, 0};
      fsmDispatch(ev);
      brakeRelayOff();
      return;
    }
  }

  if (nowMs - lastSpeedMs >= SPEED_PERIOD_MS) {
    const uint32_t nowUs = micros();
    uint32_t dtUs = nowUs - lastSpeedUs;
    lastSpeedUs = nowUs;
    lastSpeedMs = nowMs;
    if (dtUs == 0) dtUs = (uint32_t)SPEED_PERIOD_MS * 1000u;

    const uint32_t win = encPathTakePulseWin();
    if (win > 0) encPathSetLastPulseMs(nowMs);

    const bool onKeypad = (g_q == FsmState::Keypad || g_q == FsmState::Settings);
    const bool live = plantUiSpeedLive();
    const int32_t sincePulse = (int32_t)(nowMs - encPathLastPulseMs());
    const bool idleStop = (sincePulse >= (int32_t)SPEED_IDLE_ZERO_MS);

    if (!live || onKeypad || idleStop) {
      plantUiForceSpeedZero();
    } else if (win > 0) {
      const uint32_t speedInst =
          (uint32_t)((win * (uint64_t)NM_PER_COUNT) / ((uint64_t)dtUs * 10ULL));

      g_speedEma =
          (speedInst + (uint32_t)(SPEED_EMA_N - 1u) * g_speedEma) / SPEED_EMA_N;

      g_plant.speedCms = (uint16_t)min(g_speedEma, (uint32_t)MAX_SPEED_CMS);
      g_speedShown = true;
      plantUiPushSpeed();
    }
  }

  if (nowMs - lastTravelMs >= TRAVEL_PERIOD_MS) {
    lastTravelMs = nowMs;

    g_plant.travelM = encPathTravelCm();
    if (g_q != FsmState::Keypad && g_q != FsmState::Settings) plantUiPushTravel();

    if (g_q == FsmState::Run && g_plant.targetM > 0 &&
        g_plant.travelM >= plantTargetCm()) {
      FsmEventData ev{FsmEvent::TargetDone, 0};
      fsmDispatch(ev);
    }
  }

  (void)brakeLogicShouldArm(nowMs);
  if (BRAKE_EFF_ENABLE && brakeEffPoll(nowMs)) {
    FsmEventData ev{FsmEvent::BrakeIneffectiveDetect, 0};
    fsmDispatch(ev);
    if (g_q == FsmState::Error) {
      brakeLogicUpdateRelay(nowMs);
      return;
    }
  }

  brakeLogicUpdateRelay(nowMs);
}

void fsmBegin() {
  encTim2Begin();
  brakeRelayBegin();
  dwinBuzzBegin();
  g_q = FsmState::Idle;
  g_tgtGate = TargetGate::Normal;
  plantInvalidateCaches();
  g_plant = {};
  g_plant.kbFresh = true;
  g_plant.brakeOnMs = 50;
  g_plant.brakeOffMs = 50;
  if (!settingsLoad(g_plant)) {
    settingsSave(g_plant);
  }
  encPathSetInvert(g_plant.encInvert ? 1u : 0u);
  encPathClear();
  plantUiForceSpeedZero();
  plantUiWriteAllZeros();
  g_cacheTarget = g_cacheRemain = 0;
  g_cacheProgress = g_cacheSpeed = 0;
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  g_cacheEncInvert = 0xFFFF;
  plantUiPushSettings();
  delay(40);
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = 0xFFFF;
  g_cacheEncInvert = 0xFFFF;
  plantUiPushSettings();
  dwinSetPage(PAGE_MAIN);
}
