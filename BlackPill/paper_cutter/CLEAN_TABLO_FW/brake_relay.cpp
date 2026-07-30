#include "brake_relay.h"
#include "config.h"

static bool g_high = false;
static uint32_t g_phaseStartMs = 0;

void brakeRelayOff() {
  g_high = false;
  digitalWrite(PIN_RELAY, LOW);
}

bool brakeRelayIsHigh() { return g_high; }

void brakeRelayBegin() {
  pinMode(PIN_RELAY, OUTPUT);
  brakeRelayOff();
  g_phaseStartMs = millis();
}

void brakeRelayTick(uint32_t nowMs, bool armed, uint16_t onMs, uint16_t offMs) {
  if (!armed || onMs == 0) {
    if (g_high) brakeRelayOff();
    return;
  }

  // Continuous ON while in brake zone (no off phase).
  if (offMs == 0) {
    if (!g_high) {
      g_high = true;
      digitalWrite(PIN_RELAY, HIGH);
    }
    return;
  }

  const uint16_t phaseMs = g_high ? onMs : offMs;
  if ((uint32_t)(nowMs - g_phaseStartMs) < (uint32_t)phaseMs) return;

  g_phaseStartMs = nowMs;
  g_high = !g_high;
  digitalWrite(PIN_RELAY, g_high ? HIGH : LOW);
}
