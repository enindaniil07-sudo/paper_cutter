#include "plant_ui.h"
#include "config.h"
#include "plant_ctx.h"
#include "settings_store.h"
#include "range_link.h"

static void dwinWriteU16x2(uint16_t vp, uint16_t value) {
  for (uint8_t i = 0; i < 2; ++i) dwinWriteU16(vp, value);
}

void plantUiDwinWriteU32x2(uint16_t vp, uint32_t value) {
  for (uint8_t i = 0; i < 2; ++i) dwinWriteU32(vp, value);
}

void plantUiSetPageRetry(uint16_t page, uint16_t gapMs) {
  dwinSetPage(page);
  delay(gapMs);
  dwinSetPage(page);
}

void plantUiWriteTargetZero() {
  for (uint8_t pass = 0; pass < 2; ++pass) {
    dwinWriteU32(VP_TARGET, 0);
    if (pass == 0) {
      dwinWriteU16(VP_TARGET, 0);
      dwinWriteU16((uint16_t)(VP_TARGET + 1u), 0);
    }
  }
}

void plantUiWriteAllZeros() {
  plantUiWriteTargetZero();
  plantUiDwinWriteU32x2(VP_TRAVEL, 0);
  dwinWriteU16x2(VP_PROGRESS, 0);
  dwinWriteU16(VP_SPEED, 0);
  g_cacheSpeed = 0;
  g_speedShown = false;
}

void plantUiPushKb() {
  const uint16_t v = (uint16_t)min(g_plant.kbBuf, (uint32_t)MAX_METERS);
  dwinWriteU16x2(VP_KB_BUF, v);
  g_cacheKb = v;
}

void plantUiPushSettings() {
  dwinWriteU32IfChanged(VP_BRAKE, min(g_plant.brakeM, (uint32_t)MAX_METERS),
                        g_cacheBrake);
  dwinWriteU16IfChanged(VP_BRAKE_ON_MS, g_plant.brakeOnMs, g_cacheBrakeOn);
  dwinWriteU16IfChanged(VP_BRAKE_OFF_MS, g_plant.brakeOffMs, g_cacheBrakeOff);
  dwinWriteU16IfChanged(VP_ROLL_MM, g_plant.rollMm, g_cacheRollMm);
}

void plantUiRequestSettingsReads() {
  // 6090 = LONG: один ReadU32, без гонки HI/LO по двум U16.
  dwinRequestReadU32(VP_BRAKE);
  dwinRequestReadU16(VP_BRAKE_ON_MS);
  dwinRequestReadU16(VP_BRAKE_OFF_MS);
  dwinRequestReadU16(VP_ROLL_MM);
}

void plantUiPullSettings(uint32_t waitMs, DwinVpHandler onVp) {
  // Все Read сразу, без delay между ними — не стопаем loop на flash/UART.
  plantUiRequestSettingsReads();
  const uint32_t t0 = millis();
  while ((millis() - t0) < waitMs) {
    dwinPoll(onVp);
  }
  // Одна запись EEPROM на выход из UI (не на каждый OK ввода).
  settingsSave(g_plant);
}

static void applySettingsVp(uint16_t vp, uint32_t value) {
  // Только RAM + кэш VP. Persist — при выходе (plantUiPullSettings / SettingsBack).
  if (vp == VP_BRAKE) {
    if (value > MAX_METERS) value = MAX_METERS;
    g_plant.brakeM = value;
    g_cacheBrake = value;
  } else if (vp == VP_BRAKE_ON_MS) {
    if (value > 9999u) value = 9999u;
    g_plant.brakeOnMs = (uint16_t)value;
    g_cacheBrakeOn = (uint16_t)value;
  } else if (vp == VP_BRAKE_OFF_MS) {
    if (value > 9999u) value = 9999u;
    g_plant.brakeOffMs = (uint16_t)value;
    g_cacheBrakeOff = (uint16_t)value;
  } else if (vp == VP_ROLL_MM) {
    if (value > ROLL_MM_MAX) value = ROLL_MM_MAX;
    g_plant.rollMm = (uint16_t)value;
    g_cacheRollMm = (uint16_t)value;
  }
}

bool plantUiIsSettingsVp(uint16_t vp) {
  return vp == VP_BRAKE || vp == (uint16_t)(VP_BRAKE + 1u) || vp == VP_BRAKE_ON_MS ||
         vp == VP_BRAKE_OFF_MS || vp == VP_ROLL_MM;
}

void plantUiOnSettingsVp(uint16_t vp, uint32_t value) {
  if (vp == VP_BRAKE) {
    applySettingsVp(VP_BRAKE, value);
    return;
  }
  if (vp == (uint16_t)(VP_BRAKE + 1u)) return;
  if (vp == VP_BRAKE_ON_MS || vp == VP_BRAKE_OFF_MS || vp == VP_ROLL_MM) {
    applySettingsVp(vp, value);
  }
}

static uint16_t plantUiProgressPct() {
  const uint32_t t = plantTargetCm();
  if (t == 0) return 0;
  uint64_t p = ((uint64_t)g_plant.travelM * 100ULL) / t;
  if (p > 100ULL) p = 100ULL;
  return (uint16_t)p;
}

void plantUiPushTravel() {
  const uint32_t r = (uint32_t)plantRemainMetersSigned();
  dwinWriteU32IfChanged(VP_TRAVEL, r, g_cacheRemain);
  g_plant.progressPct = plantUiProgressPct();
  dwinWriteU16IfChanged(VP_PROGRESS, g_plant.progressPct, g_cacheProgress);
}

void plantUiForceRemainProgress() {
  const uint32_t r = (uint32_t)plantRemainMetersSigned();
  const uint16_t p = plantUiProgressPct();
  g_plant.progressPct = p;
  dwinWriteU32(VP_TRAVEL, r);
  dwinWriteU16(VP_PROGRESS, p);
  g_cacheRemain = r;
  g_cacheProgress = p;
}

void plantUiPushSpeed() {
  dwinWriteU16IfChanged(VP_SPEED, (uint16_t)min(g_rpmEma, (uint32_t)9999u),
                        g_cacheSpeed);
}

void plantUiPushRange(uint32_t nowMs) {
  if (!rangeLinkValid(nowMs)) return;  // обрыв линка — на экране остаётся последний замер
  uint16_t mm = rangeLinkMm();
  if (mm > ROLL_MM_MAX) mm = ROLL_MM_MAX;
  dwinWriteU16IfChanged(VP_RANGE, mm, g_cacheRange);
}

void plantUiForceSpeedZero() {
  g_plant.speedCms = 0;
  g_speedEma = 0;
  g_rpmEma = 0;
  if (g_speedShown || g_cacheSpeed != 0) {
    dwinWriteU16(VP_SPEED, 0);
    g_cacheSpeed = 0;
    g_speedShown = false;
  }
}
