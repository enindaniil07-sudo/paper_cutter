#pragma once

#include <Arduino.h>

/** TIM2 hardware encoder (PA0=A/CH1, PA1=B/CH2). MCU only polls CNT + UIF. */

struct EncTim2Delta {
  uint32_t forward;   // TIM counts forward since last poll
  uint32_t reverse;   // TIM counts reverse since last poll
  bool overflow;      // UIF was set; CNT cleared in hardware after soft account
};

void encTim2Begin();
/** Soft+hardware zero (СБРОС). Does not touch DWIN. */
void encTim2Clear();
/** Remember current CNT as baseline without adding path (after mode changes). */
void encTim2SyncBaseline();
/** Read CNT, handle UIF (reset TIM CNT on overflow), return deltas. */
EncTim2Delta encTim2Poll();
uint32_t encTim2Cnt();
