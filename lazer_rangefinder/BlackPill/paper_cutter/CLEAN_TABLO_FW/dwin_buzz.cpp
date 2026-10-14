#include "dwin_buzz.h"
#include "dwin.h"
#include "config.h"

// VP 0x00A0: value = duration / 8 ms (DGUS buzzer mode).
static void buzzWriteUnits(uint16_t units8ms) {
  if (units8ms == 0) return;
  dwinWriteU16(VP_DWIN_BUZZ, units8ms);
}

static void buzzMs(uint16_t ms) {
  uint16_t u = (uint16_t)((ms + 7u) / 8u);
  if (u == 0) u = 1;
  buzzWriteUnits(u);
}

enum class BuzzMode : uint8_t { Idle = 0, StopTriple };

static BuzzMode g_mode = BuzzMode::Idle;
static uint8_t g_stopLeft = 0;
static uint32_t g_stopNextMs = 0;
static uint32_t g_relayLastMs = 0;

void dwinBuzzBegin() {
  g_mode = BuzzMode::Idle;
  g_stopLeft = 0;
  g_stopNextMs = 0;
  g_relayLastMs = 0;
}

void dwinBuzzError() {
  // Недолгое включение (~200 ms).
  buzzMs(BUZZ_ERROR_MS);
}

void dwinBuzzStopTriple() {
  g_mode = BuzzMode::StopTriple;
  g_stopLeft = 3;
  g_stopNextMs = millis();  // первый пик сразу в tick
}

void dwinBuzzTick(uint32_t nowMs, bool relayIsHigh) {
  if (g_mode == BuzzMode::StopTriple) {
    if ((int32_t)(nowMs - g_stopNextMs) < 0) return;
    buzzMs(BUZZ_STOP_BEEP_MS);
    g_stopLeft--;
    if (g_stopLeft == 0) {
      g_mode = BuzzMode::Idle;
      return;
    }
    g_stopNextMs = nowMs + (uint32_t)BUZZ_STOP_BEEP_MS + (uint32_t)BUZZ_STOP_GAP_MS;
    return;
  }

  // Пока реле HIGH — периодически подпитываем зуммер (непрерывный писк).
  if (relayIsHigh) {
    if (g_relayLastMs == 0 ||
        (uint32_t)(nowMs - g_relayLastMs) >= (uint32_t)BUZZ_RELAY_PERIOD_MS) {
      g_relayLastMs = nowMs;
      buzzMs(BUZZ_RELAY_BEEP_MS);
    }
  } else {
    g_relayLastMs = 0;
  }
}
