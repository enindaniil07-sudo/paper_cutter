#pragma once

#include <Arduino.h>

/** Soft path / speed window on top of TIM2 hardware encoder. */

void encPathClear();
void encPathClearWindow();
void encPathPoll(uint32_t nowMs);

uint32_t encPathTravelCm();
uint32_t encPathTakePulseWin();

uint32_t encPathLastPulseMs();
void encPathSetLastPulseMs(uint32_t ms);
bool encPathActivity();

void encPathSyncCntWatch();
void encPathNoteCntPulse(uint32_t nowMs);
