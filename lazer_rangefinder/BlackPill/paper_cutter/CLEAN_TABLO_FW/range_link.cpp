#include "range_link.h"
#include "config.h"

// USART1: RX=PA10 (от C3 GPIO21), TX=PA9 (к C3 GPIO20, пока не используется).
static HardwareSerial RangeSerial(PIN_RANGE_RX, PIN_RANGE_TX);

static char s_line[16];
static uint8_t s_len = 0;
// Строка длиннее буфера — выбрасываем её целиком до '\n', иначе хвост
// («…45\n») распарсился бы как отдельное маленькое расстояние.
static bool s_discard = false;
static uint16_t s_mm = RANGE_NO_READING;
static bool s_haveOk = false;      // хотя бы один числовой кадр был
static uint32_t s_lastOkMs = 0;    // время последнего числового кадра

static void takeLine(uint32_t nowMs) {
  const uint8_t n = s_len;
  s_len = 0;
  if (n == 0) return;

  // "NA" — C3 жив, но у S3/датчика нет замера.
  if (n == 2 && s_line[0] == 'N' && s_line[1] == 'A') {
    s_mm = RANGE_NO_READING;
    return;
  }

  uint32_t v = 0;
  for (uint8_t i = 0; i < n; ++i) {
    const char c = s_line[i];
    if (c < '0' || c > '9') return;  // мусор — игнорируем строку
    v = v * 10u + (uint32_t)(c - '0');
    if (v > 0xFFFEu) return;
  }
  s_mm = (uint16_t)v;
  s_haveOk = true;
  s_lastOkMs = nowMs;
}

void rangeLinkBegin() {
  RangeSerial.begin(RANGE_BAUD);
  s_len = 0;
  s_discard = false;
  s_mm = RANGE_NO_READING;
  s_haveOk = false;
  s_lastOkMs = 0;
}

void rangeLinkPoll(uint32_t nowMs) {
  while (RangeSerial.available() > 0) {
    const int ch = RangeSerial.read();
    if (ch < 0) break;
    const char c = (char)ch;
    if (c == '\r') continue;
    if (c == '\n') {
      if (s_discard) {
        s_discard = false;
        s_len = 0;
      } else {
        takeLine(nowMs);
      }
      continue;
    }
    if (s_discard) continue;
    if (s_len < sizeof(s_line) - 1) {
      s_line[s_len++] = c;
    } else {
      s_discard = true;
      s_len = 0;
    }
  }
}

bool rangeLinkValid(uint32_t nowMs) {
  if (!s_haveOk || s_mm == RANGE_NO_READING) return false;
  return (uint32_t)(nowMs - s_lastOkMs) < RANGE_TIMEOUT_MS;
}

uint16_t rangeLinkMm() { return s_mm; }
