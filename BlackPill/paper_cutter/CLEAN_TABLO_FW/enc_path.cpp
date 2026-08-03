#include "enc_path.h"
#include "config.h"
#include "enc_tim2.h"

static uint64_t g_nm = 0;
static uint32_t g_pulseWin = 0;
static uint8_t g_reverseHit = 0;
static uint8_t g_revStreak = 0;
static uint32_t g_lastRevUs = 0;
static uint32_t g_lastPulseMs = 0;
static uint32_t g_encCntWatch = 0;
static bool g_encActivity = false;
static uint8_t g_encInvertFlag = 0;

void encPathSetInvert(uint8_t invert01) { g_encInvertFlag = invert01 ? 1u : 0u; }

void encPathClear() {
  encTim2Clear();
  g_nm = 0;
  g_pulseWin = 0;
  g_reverseHit = 0;
  g_revStreak = 0;
  g_lastRevUs = 0;
}

void encPathClearWindow() {
  g_pulseWin = 0;
  encTim2SyncBaseline();
}

void encPathPoll(uint32_t nowMs) {
  EncTim2Delta d = encTim2Poll();
  if (g_encInvertFlag) {
    const uint32_t tmp = d.forward;
    d.forward = d.reverse;
    d.reverse = tmp;
  }
  g_encActivity = false;

  if (d.forward > 0 || d.reverse > 0 || d.overflow) {
    g_encActivity = true;
    g_lastPulseMs = nowMs;
  }

  if (d.forward > 0) {
    g_revStreak = 0;
    g_pulseWin += d.forward;
    g_nm += (uint64_t)d.forward * (uint64_t)NM_PER_COUNT;
  }

  if (d.reverse > 0) {
    g_pulseWin += d.reverse;  // скорость — по |Δ|, путь только вперёд
    const uint32_t nowUs = micros();
    if (g_revStreak > 0 && (nowUs - g_lastRevUs) > ENC_REV_STREAK_GAP_US) {
      g_revStreak = 0;
    }
    g_lastRevUs = nowUs;
    uint32_t add = d.reverse;
    if (add > 255u) add = 255u;
    uint16_t next = (uint16_t)g_revStreak + (uint16_t)add;
    if (next > 255u) next = 255u;
    g_revStreak = (uint8_t)next;
    if (g_revStreak >= ENC_REV_CONFIRM) {
      g_reverseHit = 1;
      g_revStreak = 0;
    }
  }
}

uint32_t encPathTravelCm() {
  const uint64_t v = g_nm / 10000000ULL;
  const uint64_t cap = (uint64_t)MAX_METERS * 100ULL;
  if (v >= cap) return (uint32_t)cap;
  return (uint32_t)v;
}

uint32_t encPathTakePulseWin() {
  const uint32_t w = g_pulseWin;
  g_pulseWin = 0;
  return w;
}

uint32_t encPathLastPulseMs() { return g_lastPulseMs; }
void encPathSetLastPulseMs(uint32_t ms) { g_lastPulseMs = ms; }
bool encPathActivity() { return g_encActivity; }

uint8_t encPathTakeReverseHit() {
  const uint8_t v = g_reverseHit;
  g_reverseHit = 0;
  return v;
}

void encPathClearReverse() {
  g_reverseHit = 0;
  g_revStreak = 0;
}

void encPathSetReverseHit() { g_reverseHit = 1; }

void encPathSyncCntWatch() { g_encCntWatch = encTim2Cnt(); }

void encPathNoteCntPulse(uint32_t nowMs) {
  const uint32_t cnt = encTim2Cnt();
  if (cnt != g_encCntWatch) {
    g_encCntWatch = cnt;
    g_lastPulseMs = nowMs;
  }
}
