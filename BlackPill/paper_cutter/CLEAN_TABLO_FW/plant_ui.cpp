#include "plant_ui.h"
#include "config.h"
#include "plant_ctx.h"
#include "enc_path.h"
#include "settings_store.h"

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
  dwinWriteU16IfChanged(VP_ENC_INVERT, g_plant.encInvert ? 1u : 0u,
                        g_cacheEncInvert);
}

static uint16_t g_brakeHi = 0;
static bool g_brakeHiFresh = false;

static const uint16_t kSettingsReadVp[] = {
    VP_BRAKE,
    (uint16_t)(VP_BRAKE + 1u),
    VP_BRAKE_ON_MS,
    VP_BRAKE_OFF_MS,
    VP_ENC_INVERT,
};
static constexpr uint8_t kSettingsReadVpN =
    (uint8_t)(sizeof(kSettingsReadVp) / sizeof(kSettingsReadVp[0]));

void plantUiRequestSettingsReads() {
  for (uint8_t i = 0; i < kSettingsReadVpN; ++i) {
    dwinRequestReadU16(kSettingsReadVp[i]);
  }
}

void plantUiPullSettings(uint32_t waitMs, DwinVpHandler onVp) {
  g_brakeHiFresh = false;
  for (uint8_t i = 0; i < kSettingsReadVpN; ++i) {
    dwinRequestReadU16(kSettingsReadVp[i]);
    if (i + 1u < kSettingsReadVpN) {
      delay(15);
      dwinPoll(onVp);
    }
  }
  const uint32_t t0 = millis();
  while ((millis() - t0) < waitMs) {
    dwinPoll(onVp);
  }
  settingsSave(g_plant);
}

static void applySettingsVp(uint16_t vp, uint32_t value) {
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
  } else if (vp == VP_ENC_INVERT) {
    const uint16_t bit = (value != 0) ? 1u : 0u;
    g_plant.encInvert = (uint8_t)bit;
    encPathSetInvert((uint8_t)bit);
    if (g_cacheEncInvert != bit) {
      g_cacheEncInvert = bit;
      dwinWriteU16(VP_ENC_INVERT, bit);
    }
  }
}

void plantUiOnSettingsVp(uint16_t vp, uint32_t value) {
  if (vp == VP_BRAKE) {
    if (value > 0xFFFFu) {
      g_brakeHiFresh = false;
      applySettingsVp(VP_BRAKE, value);
      return;
    }
    g_brakeHi = (uint16_t)value;
    g_brakeHiFresh = true;
    return;
  }
  if (vp == (uint16_t)(VP_BRAKE + 1u) && g_brakeHiFresh) {
    const uint32_t full = ((uint32_t)g_brakeHi << 16) | (value & 0xFFFFu);
    g_brakeHiFresh = false;
    applySettingsVp(VP_BRAKE, full);
    return;
  }
  if (vp == VP_BRAKE_ON_MS || vp == VP_BRAKE_OFF_MS || vp == VP_ENC_INVERT) {
    applySettingsVp(vp, value);
  }
}

uint16_t plantUiProgressPct() {
  const uint32_t t = plantTargetCm();
  if (t == 0) return 0;
  uint64_t p = ((uint64_t)g_plant.travelM * 100ULL) / t;
  if (p > 100ULL) p = 100ULL;
  return (uint16_t)p;
}

void plantUiPushTravel() {
  dwinWriteU32IfChanged(VP_TRAVEL, plantRemainMeters(), g_cacheRemain);
  g_plant.progressPct = plantUiProgressPct();
  dwinWriteU16IfChanged(VP_PROGRESS, g_plant.progressPct, g_cacheProgress);
}

void plantUiForceRemainProgress() {
  const uint32_t r = plantRemainMeters();
  const uint16_t p = plantUiProgressPct();
  g_plant.progressPct = p;
  dwinWriteU32(VP_TRAVEL, r);
  dwinWriteU16(VP_PROGRESS, p);
  g_cacheRemain = r;
  g_cacheProgress = p;
}

void plantUiPushSpeed() {
  dwinWriteU16IfChanged(VP_SPEED, g_plant.speedCms, g_cacheSpeed);
}

void plantUiForceSpeedZero() {
  g_plant.speedCms = 0;
  g_speedEma = 0;
  if (g_speedShown || g_cacheSpeed != 0) {
    dwinWriteU16(VP_SPEED, 0);
    g_cacheSpeed = 0;
    g_speedShown = false;
  }
}

bool plantUiSpeedLive() {
  return g_q == FsmState::Idle || g_q == FsmState::Run || g_brakeLatched;
}
