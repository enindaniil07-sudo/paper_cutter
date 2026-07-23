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
/** PA0/PA1 levels via GPIO IDR (valid while AF TIM2). a/b are 0 or 1. */
void encTim2ReadAb(uint8_t& a, uint8_t& b);
/**
 * Hardware edge flags since last call (CC1IF=A/TI1, CC2IF=B/TI2).
 * Cleared on read. Works alongside encoder SMS when CCxE enabled.
 */
void encTim2TakeCaptureEdges(bool& edgeA, bool& edgeB);
