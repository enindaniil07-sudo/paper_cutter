#include "enc_path.h"
#include "config.h"
#include "enc_tim2.h"

static uint64_t g_nm = 0;
static uint32_t g_pulseWin = 0;
static uint32_t g_lastPulseMs = 0;
static uint32_t g_encCntWatch = 0;
static bool g_encActivity = false;

void encPathClear() {
  encTim2Clear();
  g_nm = 0;
  g_pulseWin = 0;
}

void encPathClearWindow() {
  g_pulseWin = 0;
  encTim2SyncBaseline();
}

void encPathPoll(uint32_t nowMs) {
  EncTim2Delta d = encTim2Poll();
  g_encActivity = false;

  if (d.forward > 0 || d.reverse > 0 || d.overflow) {
    g_encActivity = true;
    g_lastPulseMs = nowMs;
  }

  // Путь и скорость — по |Δ|: вперёд и назад дают одну и ту же длину.
  const uint32_t ticks = d.forward + d.reverse;
  if (ticks > 0) {
    g_pulseWin += ticks;
    g_nm += (uint64_t)ticks * (uint64_t)NM_PER_COUNT;
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

void encPathSyncCntWatch() { g_encCntWatch = encTim2Cnt(); }

void encPathNoteCntPulse(uint32_t nowMs) {
  const uint32_t cnt = encTim2Cnt();
  if (cnt != g_encCntWatch) {
    g_encCntWatch = cnt;
    g_lastPulseMs = nowMs;
  }
}
