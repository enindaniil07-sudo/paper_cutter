#pragma once

#include <Arduino.h>

/** Soft path / reverse / speed window on top of TIM2 hardware encoder. */

void encPathClear();
void encPathClearWindow();
void encPathPoll(uint32_t nowMs);

void encPathSetInvert(uint8_t invert01);
uint32_t encPathTravelCm();
uint32_t encPathTakePulseWin();

uint32_t encPathLastPulseMs();
void encPathSetLastPulseMs(uint32_t ms);
bool encPathActivity();

uint8_t encPathTakeReverseHit();
void encPathClearReverse();
/** Force reverse-hit flag (e.g. restore pending across Run arm). */
void encPathSetReverseHit();

void encPathSyncCntWatch();
void encPathNoteCntPulse(uint32_t nowMs);
