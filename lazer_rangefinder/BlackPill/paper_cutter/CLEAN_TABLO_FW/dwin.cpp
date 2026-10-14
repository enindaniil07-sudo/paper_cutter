#include "dwin.h"
#include "config.h"
#include <string.h>

#if USE_USART1
static HardwareSerial DwinSerial(PA10, PA9);
#else
static HardwareSerial DwinSerial(PA3, PA2);
#endif

static uint8_t s_rx[96];
static uint8_t s_rxLen = 0;

// DWIN UART CRC-16 (Modbus poly), over cmd+data only.
// Length byte includes the 2 CRC bytes. CRC is sent low-byte first.
static uint16_t dwinCrc16(const uint8_t* data, uint8_t n) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 1u) {
        crc = (uint16_t)((crc >> 1) ^ 0xA001u);
      } else {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }
  return crc;
}

// payload = cmd + data (no header, no length, no CRC)
static void dwinSend(const uint8_t* payload, uint8_t payloadLen) {
  const uint16_t crc = dwinCrc16(payload, payloadLen);
  uint8_t frame[32];
  const uint8_t len = (uint8_t)(payloadLen + 2u);  // includes CRC
  if ((uint16_t)3 + len > sizeof(frame)) return;
  frame[0] = 0x5A;
  frame[1] = 0xA5;
  frame[2] = len;
  memcpy(frame + 3, payload, payloadLen);
  frame[3 + payloadLen] = (uint8_t)(crc & 0xFF);
  frame[3 + payloadLen + 1] = (uint8_t)(crc >> 8);
  DwinSerial.write(frame, (size_t)(3 + len));
}

void dwinBegin(uint32_t baud) { DwinSerial.begin(baud); }

void dwinWriteU16(uint16_t vp, uint16_t value) {
  const uint8_t p[5] = {
      0x82,
      (uint8_t)(vp >> 8), (uint8_t)(vp & 0xFF),
      (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
  dwinSend(p, sizeof(p));
}

void dwinWriteU16IfChanged(uint16_t vp, uint16_t value, uint16_t& cache) {
  if (value == cache) return;
  cache = value;
  dwinWriteU16(vp, value);
}

void dwinWriteU32(uint16_t vp, uint32_t value) {
  const uint8_t p[7] = {
      0x82,
      (uint8_t)(vp >> 8), (uint8_t)(vp & 0xFF),
      (uint8_t)(value >> 24), (uint8_t)((value >> 16) & 0xFF),
      (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
  dwinSend(p, sizeof(p));
}

void dwinWriteU32IfChanged(uint16_t vp, uint32_t value, uint32_t& cache) {
  if (value == cache) return;
  cache = value;
  dwinWriteU32(vp, value);
}

void dwinClearCmd(uint16_t vp) { dwinWriteU16(vp, 0); }

void dwinRequestReadU16(uint16_t vp) {
  const uint8_t p[4] = {
      0x83,
      (uint8_t)(vp >> 8), (uint8_t)(vp & 0xFF), 0x01};
  dwinSend(p, sizeof(p));
}

void dwinRequestReadU32(uint16_t vp) {
  const uint8_t p[4] = {
      0x83,
      (uint8_t)(vp >> 8), (uint8_t)(vp & 0xFF), 0x02};
  dwinSend(p, sizeof(p));
}

void dwinSetPage(uint16_t page) {
  // DGUS PIC_SET: VP 0x0084 ← 0x5A01, page
  const uint8_t p[7] = {
      0x82, 0x00, 0x84, 0x5A, 0x01,
      (uint8_t)(page >> 8), (uint8_t)(page & 0xFF)};
  dwinSend(p, sizeof(p));
}

void dwinPoll(DwinVpHandler onVp) {
  while (DwinSerial.available() > 0) {
    const int ch = DwinSerial.read();
    if (ch < 0) break;
    const uint8_t b = (uint8_t)ch;

    if (s_rxLen == 0) {
      if (b == 0x5A) s_rx[s_rxLen++] = b;
      continue;
    }
    if (s_rxLen == 1) {
      if (b == 0xA5) {
        s_rx[s_rxLen++] = b;
      } else {
        s_rxLen = (b == 0x5A) ? 1 : 0;
        if (s_rxLen == 1) s_rx[0] = 0x5A;
      }
      continue;
    }

    if (s_rxLen < sizeof(s_rx)) {
      s_rx[s_rxLen++] = b;
    } else {
      s_rxLen = 0;
      continue;
    }

    if (s_rxLen < 3) continue;
    const uint8_t len = s_rx[2];
    const uint16_t need = (uint16_t)3 + len;
    if (need > sizeof(s_rx)) {
      s_rxLen = 0;
      continue;
    }
    if (s_rxLen < need) continue;

    // 0x83 reply: 5A A5 len 83 VP words data… [CRC_L CRC_H]
    if (s_rx[3] == 0x83 && len >= 5 && onVp) {
      const uint16_t vp = ((uint16_t)s_rx[4] << 8) | s_rx[5];
      const uint8_t words = s_rx[6];
      if (words >= 1 && s_rxLen >= 9) {
        const uint16_t w0 = ((uint16_t)s_rx[7] << 8) | s_rx[8];
        uint32_t val = w0;
        // Long (VP+VP+1): ЗАДАНО / тормоз. Prefer 2-word merge when present.
        if (words >= 2 && s_rxLen >= 11 && (vp == VP_TARGET || vp == VP_BRAKE)) {
          const uint16_t w1 = ((uint16_t)s_rx[9] << 8) | s_rx[10];
          val = ((uint32_t)w0 << 16) | w1;
        }
        onVp(vp, val);
      }
    }

    if (s_rxLen > need) {
      memmove(s_rx, s_rx + need, s_rxLen - need);
      s_rxLen = (uint8_t)(s_rxLen - need);
    } else {
      s_rxLen = 0;
    }
  }
}
