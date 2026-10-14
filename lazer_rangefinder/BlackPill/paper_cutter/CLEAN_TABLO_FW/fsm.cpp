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
#include "range_link.h"

/*
  FSM + motion tick. Encoder path → enc_path, brake → brake_logic,
  DWIN plant/settings → plant_ui, shared state → plant_ctx,
  дальномер (C3 → USART1) → range_link.
*/

uint16_t fsmLedPeriodMs() {
  if (g_err != FsmError::None) return 60u;
  return (g_q == FsmState::Run) ? 100u : 450u;
}

// «Нет энкодера» (бейдж ЭНК.): только после реальных импульсов уже в Run и до тормоза.
static bool g_encWatchOk = false;
// После СБРОС не принимать старое ЗАДАНО с панели, пока не прочитали 0.
static bool g_tgtSawZero = false;
// Когда поднят бейдж — чтобы не мигал, держим ≥ ERR_BADGE_MIN_MS.
static uint32_t g_errSinceMs = 0;

static void closeSettingsUi() {
  g_settingsUi = false;
}

/*
  Ошибки — только бейдж в окошке под шестерёнкой (IconShow VP_ERROR на стр. 0).
  Страницы не переключаем, FSM-состояние не трогаем, кнопки OK нет:
    ЭНК.  — снимается, когда снова пошли импульсы энкодера;
    ТОРМ. — снимается, когда в зоне тормоза скорость пошла вниз / вал встал;
    СТОП / СБРОС снимают любой бейдж.
*/
static uint16_t errBadgeCode(FsmError e) {
  switch (e) {
    case FsmError::NoEncoder: return ERR_BADGE_ENCODER;
    case FsmError::BrakeIneffective: return ERR_BADGE_BRAKE;
    default: return ERR_BADGE_NONE;
  }
}

static void errBadgePush() { dwinWriteU16(VP_ERROR, errBadgeCode(g_err)); }

static void actShowError(FsmError kind) {
  if (g_err == kind) return;  // детектор может срабатывать каждый период
  g_err = kind;
  g_errSinceMs = millis();
  if (kind == FsmError::NoEncoder) {
    plantUiForceSpeedZero();
    encPathClearWindow();
  } else {
    (void)brakeEffTakeRecovered();  // сбросить устаревший флаг «восстановился»
  }
  dwinBuzzError();
  errBadgePush();
}

static bool errBadgeHeldLongEnough(uint32_t nowMs) {
  return (int32_t)(nowMs - g_errSinceMs) >= (int32_t)ERR_BADGE_MIN_MS;
}

static void actClearError() {
  if (g_err == FsmError::None) return;
  g_err = FsmError::None;
  brakeEffReset();
  errBadgePush();
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
  brakeLogicMuteFaults();  // бейдж ЭНК. только после вращения в новом Run
  g_err = FsmError::None;
  errBadgePush();
  g_jobComplete = false;
  g_brakeHoldOff = false;
  g_encWatchOk = false;
  g_tgtSawZero = false;
  closeSettingsUi();
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
  encPathClearWindow();
  plantUiForceSpeedZero();
  brakeEffReset();
  g_encWatchOk = false;  // бейдж ЭНК. не раньше, чем пойдут импульсы уже в Run
  const uint32_t t = millis();
  encPathSetLastPulseMs(t);
  encPathSyncCntWatch();
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
  errBadgePush();
}

/** Подготовка счёта после СБРОС + новое ЗАДАНО (не вызывать при конце задания). */
static void actStartFreshJob() {
  g_jobComplete = false;
  g_brakeHoldOff = false;
  g_plant.travelM = 0;
  g_plant.progressPct = 0;
  encPathClear();
  encPathClearWindow();
  brakeLogicClearLatch();
  brakeEffReset();
  brakeLogicMuteFaults();
  g_encWatchOk = false;
  g_speedEma = 0;
  plantUiForceSpeedZero();
}

static void actKbCommit() {
  if (g_plant.kbBuf > MAX_METERS) g_plant.kbBuf = MAX_METERS;
  // Конец задания держим на экране до СБРОС — новое ЗАДАНО не сбрасывает прогон.
  if (g_jobComplete) {
    g_plant.kbBuf = g_plant.targetM;
    g_plant.kbFresh = true;
    leaveKeypadToMain(true);
    return;
  }
  g_plant.targetM = g_plant.kbBuf;
  g_plant.kbFresh = true;
  g_tgtGate = TargetGate::Normal;
  if (g_plant.targetM > 0) actStartFreshJob();
  leaveKeypadToMain(true);
}

static void actKbCancel() {
  g_plant.kbBuf = g_plant.targetM;
  g_plant.kbFresh = true;
  leaveKeypadToMain(false);
}

static void actSettingsOpen() {
  // Pic_Next=17 already switched — do not dwinSetPage (layer fight).
  // Только тормоз — Run/счёт/прогресс не трогаем.
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = g_cacheRollMm = 0xFFFF;
  plantUiPushSettings();
}

static void actSettingsBack() {
  plantUiPullSettings(50, fsmOnDwinVp);
  closeSettingsUi();
  plantInvalidateCaches();
  dwinWriteU32(VP_TARGET, g_plant.targetM);
  g_cacheTarget = g_plant.targetM;
  plantUiForceRemainProgress();
  plantUiPushSpeed();
  errBadgePush();
}

/** Конец задачи: счёт «осталось» живой (после переезда — минус). СБРОС обнуляет. */
static void actFinishJob() {
  g_jobComplete = true;
  g_encWatchOk = false;
  brakeLogicMuteFaults();
  g_speedEma = 0;
  plantUiForceSpeedZero();
  plantUiForceRemainProgress();
  if (g_settingsUi) {
    closeSettingsUi();
    dwinSetPage(PAGE_MAIN);
    delay(20);
  }
  g_q = FsmState::Stopped;
}

static bool tryArmRunFromMotion() {
  if (g_jobComplete) return false;  // только СБРОС снимает конец задания
  if (g_plant.targetM == 0) return false;
  if (g_tgtGate != TargetGate::Normal) return false;
  if (g_plant.travelM >= plantTargetCm()) return false;
  actPrepareRun();
  g_q = FsmState::Run;
  return true;
}

static void fsmDispatch(const FsmEventData& ev) {
  // EncLoss можно глушить после стопа; отказ тормоза — нет.
  if (ev.type == FsmEvent::EncLoss) {
    if (brakeLogicFaultsMuted()) return;
  }

  // Ошибки — бейдж поверх текущего состояния; Run/Stopped не меняем.
  if (ev.type == FsmEvent::BrakeIneffectiveDetect) {
    if (g_q == FsmState::Run || g_q == FsmState::Stopped) {
      actShowError(FsmError::BrakeIneffective);
    }
    return;
  }
  if (ev.type == FsmEvent::EncLoss) {
    if (g_q == FsmState::Run) {
      actShowError(FsmError::NoEncoder);
    }
    return;
  }

  // Digit/Del/Ok/Cancel from main → open keypad first.
  const bool isKb = ev.type == FsmEvent::KbDigit || ev.type == FsmEvent::KbDel ||
                    ev.type == FsmEvent::KbOk || ev.type == FsmEvent::KbCancel;
  if (isKb && g_q != FsmState::Keypad && !g_settingsUi) {
    actKbOpen();
    g_q = FsmState::Keypad;
  }

  FsmState qn = g_q;

  switch (ev.type) {
    case FsmEvent::Stop:
      // СТОП выключает тормоз / вал останавливают — оба бейджа неактуальны.
      actClearError();
      if (g_settingsUi) {
        plantUiPullSettings(50, fsmOnDwinVp);
        closeSettingsUi();
        dwinSetPage(PAGE_MAIN);
        plantUiForceRemainProgress();
        plantUiPushSpeed();
      }
      if (g_q == FsmState::Run || g_q == FsmState::Stopped) {
        // СТОП = пауза без тормоза; дальше докрутка вручную, зона тормоза выкл.
        if (!g_jobComplete) {
          g_brakeHoldOff = true;
          brakeLogicClearLatch();
          brakeEffReset();
          brakeRelayOff();
        }
        plantUiForceSpeedZero();
        qn = FsmState::Stopped;
      } else if (g_q == FsmState::Idle || g_q == FsmState::Keypad) {
        plantUiForceSpeedZero();
        qn = FsmState::Stopped;
      }
      break;

    case FsmEvent::Reset:
      if (g_settingsUi) {
        plantUiPullSettings(50, fsmOnDwinVp);
        closeSettingsUi();
        dwinSetPage(PAGE_MAIN);
      }
      actClearPlant();  // снимает и бейдж ошибки
      qn = FsmState::Idle;
      break;

    case FsmEvent::KbOpen:
      if (g_settingsUi) break;
      actKbOpen();
      qn = FsmState::Keypad;
      break;

    case FsmEvent::SettingsOpen:
      if (g_settingsUi) break;
      g_settingsUi = true;
      actSettingsOpen();
      qn = g_q;  // Run/Idle/Stopped не сбрасываем — счёт и тормоз продолжаются
      break;

    case FsmEvent::SettingsBack:
      if (!g_settingsUi) break;  // ignore stale VP
      actSettingsBack();
      qn = g_q;
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
      if (g_settingsUi) {
        closeSettingsUi();
        dwinSetPage(PAGE_MAIN);
        delay(20);
      }
      plantUiForceRemainProgress();
      qn = FsmState::Stopped;
      g_jobComplete = true;  // длина набрана; путь не сбрасываем — «осталось» уходит в минус
      // Не глушить BrakeIneffective: как раз во время дотормаживания нужен бейдж ТОРМ.
      // EncLoss и так закрыт (jobComplete / latch / не Run).
      // Нет зоны торможения → сразу тройной пик; иначе — когда вал встанет.
      // Путь энкодера не чистим: докрутка после тормоза должна продолжать счёт, а не начинать задание заново.
      if (!brakeLogicIsLatched()) {
        dwinBuzzStopTriple();
      }
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
        g_tgtSawZero = true;
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
        g_tgtSawZero = true;
        g_plant.targetM = 0;
        g_cacheTarget = 0;
        return;
      }
      // Старое ЗАДАНО, оставшееся в VP после СБРОС — не считать новым заданием.
      if (!g_tgtSawZero) {
        plantUiWriteTargetZero();
        return;
      }
      g_tgtGate = TargetGate::Normal;
      g_plant.targetM = value;
      g_cacheTarget = value;
      actStartFreshJob();
      plantUiForceRemainProgress();
      return;
    }

    // Конец работы: цифры на экране до СБРОС, новое ЗАДАНО не стартует прогон.
    if (g_jobComplete) {
      if (value != g_plant.targetM) {
        dwinWriteU32(VP_TARGET, g_plant.targetM);
        g_cacheTarget = g_plant.targetM;
      }
      return;
    }

    // В Normal не принимать ложный 0 с панели (гонка опроса после ввода).
    if (value == 0) {
      if (g_plant.targetM != 0) {
        dwinWriteU32(VP_TARGET, g_plant.targetM);
        g_cacheTarget = g_plant.targetM;
      }
      return;
    }

    if (value == g_plant.targetM && value == g_cacheTarget) return;

    g_plant.targetM = value;
    g_cacheTarget = value;
    actStartFreshJob();
    plantUiForceRemainProgress();
    return;
  }

  if (plantUiIsSettingsVp(vp)) {
    if (!g_settingsUi) return;  // правки только из экрана настроек
    plantUiOnSettingsVp(vp, value);
    return;
  }

  if (vp == VP_RESET || vp == VP_SETTINGS || vp == VP_SETTINGS_BACK) {
    const uint32_t now = millis();
    if (!acceptPress(vp, (uint16_t)value, now)) return;
    dwinWriteU16(vp, 0);
    FsmEventData ev{};
    if (vp == VP_RESET) {
      ev.type = FsmEvent::Reset;
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
  static const uint16_t kAlt[] = {VP_STOP, VP_SETTINGS, VP_SETTINGS_BACK};
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

  if (g_tgtGate == TargetGate::Resetting) {
    if (nowMs - tResetHold >= 80u) {
      tResetHold = nowMs;
      plantUiWriteAllZeros();
      dwinRequestReadU32(VP_TARGET);
    }
    if (nowMs - g_tgtResetStartMs >= RESET_TARGET_LOCK_MS) {
      g_tgtGate = TargetGate::Armed;
      plantUiWriteAllZeros();
      // Не переходить к приёму ЗАДАНО, пока панель не подтвердила 0.
    }
    return;
  }

  if (nowMs - tTarget >= TARGET_POLL_MS) {
    tTarget = nowMs;
    if (g_settingsUi) {
      plantUiRequestSettingsReads();
    } else if (g_q != FsmState::Keypad && g_tgtGate == TargetGate::Normal) {
      dwinRequestReadU32(VP_TARGET);
    }
  }
}

void fsmMotionTick(uint32_t nowMs) {
  static uint32_t lastSpeedMs = 0;
  static uint32_t lastSpeedUs = 0;
  static uint32_t lastTravelMs = 0;
  static uint32_t accWin = 0;
  static uint32_t accDtUs = 0;

  encPathPoll(nowMs);
  rangeLinkPoll(nowMs);
  plantUiPushRange(nowMs);

  if (lastSpeedMs == 0) {
    lastSpeedMs = nowMs;
    lastTravelMs = nowMs;
    lastSpeedUs = micros();
    encPathSetLastPulseMs(nowMs);
    return;
  }

  // Скорость — всегда, даже в Error / после конца задачи.
  if (nowMs - lastSpeedMs >= SPEED_PERIOD_MS) {
    const uint32_t nowUs = micros();
    uint32_t dtUs = nowUs - lastSpeedUs;
    lastSpeedUs = nowUs;
    lastSpeedMs = nowMs;
    if (dtUs == 0) dtUs = (uint32_t)SPEED_PERIOD_MS * 1000u;

    const uint32_t win = encPathTakePulseWin();
    if (win > 0) {
      encPathSetLastPulseMs(nowMs);
      // Бейдж ЭНК.: импульсы уже в Run и тормоз ещё не включён.
      if (g_q == FsmState::Run && !g_jobComplete && !brakeLogicIsLatched()) {
        g_encWatchOk = true;
        brakeLogicClearFaultMute();
      }
      // Сигнал с энкодера вернулся → бейдж ЭНК. снимаем сами.
      if (g_err == FsmError::NoEncoder && errBadgeHeldLongEnough(nowMs)) {
        actClearError();
      }
    }

    const int32_t sincePulse = (int32_t)(nowMs - encPathLastPulseMs());
    const bool idleStop = (sincePulse >= (int32_t)SPEED_IDLE_ZERO_MS);

    if (idleStop) {
      accWin = 0;
      accDtUs = 0;
      plantUiForceSpeedZero();
    } else if (win == 0) {
      // Пустое окно: не обнулять (регрессия → провалы 11→1.x). Ждём idleStop.
    } else {
      accWin += win;
      accDtUs += dtUs;
      if (accDtUs >= SPEED_AVG_US) {
        // об/мин = Δтики × 60e6 / (тиков/оборот × Δt_мкс)
        uint32_t rpmInst = (uint32_t)(
            (accWin * 60000000ULL +
             ((uint64_t)TIM_COUNTS_PER_REV * accDtUs) / 2u) /
            ((uint64_t)TIM_COUNTS_PER_REV * accDtUs));
        accWin = 0;
        accDtUs = 0;

        if (g_rpmEma > 20u && rpmInst * 3u < g_rpmEma) {
          rpmInst = g_rpmEma;
        }
        if (rpmInst > 9999u) {
          encPathClearWindow();
          rpmInst = g_rpmEma;
        }

        if (g_rpmEma == 0) {
          g_rpmEma = rpmInst;
        } else {
          g_rpmEma = (g_rpmEma * (uint32_t)(SPEED_EMA_N - 1u) + rpmInst) /
                     (uint32_t)SPEED_EMA_N;
        }

        g_speedEma = (uint32_t)(((uint64_t)g_rpmEma * WHEEL_CIRC_NM + 300000000ULL) /
                                600000000ULL);
        g_plant.speedCms = (uint16_t)min(g_speedEma, (uint32_t)MAX_SPEED_CMS);
        g_speedShown = true;
        plantUiPushSpeed();
      }
    }
  }

  if ((g_q == FsmState::Idle || g_q == FsmState::Stopped) &&
      encPathActivity() && !brakeLogicIsLatched()) {
    tryArmRunFromMotion();
  }

  // Импульсы для EncLoss / idle-zero — постоянно.
  encPathNoteCntPulse(nowMs);

  // Бейдж ЭНК. только: Run + уже было вращение + тормоз ещё не включён.
  // Idle / СБРОС / конец задания / зона тормоза — не ошибка.
  if (brakeLogicIsLatched() || g_jobComplete || g_q != FsmState::Run) {
    g_encWatchOk = false;
  }
  if (ENC_NO_SIGNAL_ENABLE && g_encWatchOk && g_q == FsmState::Run &&
      g_err != FsmError::NoEncoder && !brakeLogicIsLatched() &&
      !brakeLogicFaultsMuted() && !g_jobComplete) {
    const int32_t quietMs = (int32_t)(nowMs - encPathLastPulseMs());
    if (quietMs >= (int32_t)ENC_NO_SIGNAL_MS) {
      // Один раз поднимаем бейдж и гасим реле; дальше тик идёт как обычно,
      // бейдж снимется сам при первых импульсах.
      FsmEventData ev{FsmEvent::EncLoss, 0};
      fsmDispatch(ev);
      brakeLogicClearLatch();
      brakeRelayOff();
      return;
    }
  }

  if (nowMs - lastTravelMs >= TRAVEL_PERIOD_MS) {
    lastTravelMs = nowMs;

    // Путь энкодера — всегда, задание не нужно. «Осталось» = ЗАДАНО − пройдено:
    // без задания это минус (сколько проехал), с заданием уходит в минус после переезда.
    // Во время СБРОС экран держим на нулях.
    if (g_tgtGate != TargetGate::Resetting) {
      g_plant.travelM = encPathTravelCm();
      if (g_q != FsmState::Keypad) {
        plantUiPushTravel();
      }

      if (!g_jobComplete && g_q == FsmState::Run && g_plant.targetM > 0 &&
          g_plant.travelM >= plantTargetCm()) {
        FsmEventData ev{FsmEvent::TargetDone, 0};
        fsmDispatch(ev);
      }
    }
  }

  (void)brakeLogicShouldArm(nowMs);
  if (BRAKE_EFF_ENABLE && brakeEffPoll(nowMs)) {
    FsmEventData ev{FsmEvent::BrakeIneffectiveDetect, 0};
    fsmDispatch(ev);
  }
  // Тормоз снова замедляет (скорость пошла вниз / вал почти встал) → снять ТОРМ.
  if (g_err == FsmError::BrakeIneffective && brakeEffTakeRecovered() &&
      errBadgeHeldLongEnough(nowMs)) {
    actClearError();
  }

  brakeLogicUpdateRelay(nowMs);

  // Тормоз отпустил вал. Закрываем задание только если длина уже набрана;
  // после СТОП (пауза) — остаёмся в работе, можно докрутить оставшееся.
  if (brakeLogicTakeStopComplete()) {
    if (g_err == FsmError::BrakeIneffective) actClearError();  // вал встал — тормоз сработал
    const bool targetReached =
        g_jobComplete ||
        (g_plant.targetM > 0 && g_plant.travelM >= plantTargetCm());
    if (targetReached) {
      dwinBuzzStopTriple();
      actFinishJob();
    }
  }
}

void fsmBegin() {
  encTim2Begin();
  brakeRelayBegin();
  dwinBuzzBegin();
  rangeLinkBegin();
  g_q = FsmState::Idle;
  g_tgtGate = TargetGate::Normal;
  plantInvalidateCaches();
  g_plant = {};
  g_plant.kbFresh = true;
  g_plant.brakeOnMs = 50;
  g_plant.brakeOffMs = 50;
  g_plant.rollMm = 0;  // дальномер выкл., пока не задан
  if (!settingsLoad(g_plant)) {
    settingsSave(g_plant);
  }
  encPathClear();
  plantUiForceSpeedZero();
  plantUiWriteAllZeros();
  g_err = FsmError::None;
  errBadgePush();
  g_cacheTarget = g_cacheRemain = 0;
  g_cacheProgress = g_cacheSpeed = 0;
  dwinWriteU16(VP_RANGE, 0);
  g_cacheRange = 0;
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = g_cacheRollMm = 0xFFFF;
  plantUiPushSettings();
  delay(40);
  g_cacheBrake = 0xFFFFFFFFu;
  g_cacheBrakeOn = g_cacheBrakeOff = g_cacheRollMm = 0xFFFF;
  plantUiPushSettings();
  dwinSetPage(PAGE_MAIN);
}
