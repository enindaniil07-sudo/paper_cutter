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
void plantUiForceSpeedZero();
void plantUiDwinWriteU32x2(uint16_t vp, uint32_t value);
void plantUiSetPageRetry(uint16_t page, uint16_t gapMs);

void plantUiRequestSettingsReads();
void plantUiPullSettings(uint32_t waitMs, DwinVpHandler onVp);
void plantUiOnSettingsVp(uint16_t vp, uint32_t value);

bool plantUiSpeedLive();
uint16_t plantUiProgressPct();
