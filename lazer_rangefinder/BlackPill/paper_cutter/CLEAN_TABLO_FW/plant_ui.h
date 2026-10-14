#pragma once

#include <Arduino.h>
#include "dwin.h"

/** DWIN plant display + settings VP glue. */

void plantUiWriteTargetZero();
void plantUiWriteAllZeros();
void plantUiPushKb();
void plantUiPushSettings();
void plantUiPushTravel();
void plantUiForceRemainProgress();
void plantUiPushSpeed();
/** Живые мм дальномера в VP_RANGE. Без свежего замера число на экране не трогает. */
void plantUiPushRange(uint32_t nowMs);
void plantUiForceSpeedZero();
void plantUiDwinWriteU32x2(uint16_t vp, uint32_t value);
void plantUiSetPageRetry(uint16_t page, uint16_t gapMs);

void plantUiRequestSettingsReads();
void plantUiPullSettings(uint32_t waitMs, DwinVpHandler onVp);
/** VP принадлежит экрану настроек (6090/6091/6094/6096/6098). */
bool plantUiIsSettingsVp(uint16_t vp);
void plantUiOnSettingsVp(uint16_t vp, uint32_t value);

uint16_t plantUiProgressPct();
