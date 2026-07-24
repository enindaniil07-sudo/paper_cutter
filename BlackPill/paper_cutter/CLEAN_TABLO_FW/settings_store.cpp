#include "settings_store.h"
#include "config.h"

#include <EEPROM.h>
#include <string.h>

#if !defined(DATA_EEPROM_BASE)
extern "C" {
void eeprom_buffer_fill(void);
void eeprom_buffer_flush(void);
void eeprom_buffered_write_byte(uint32_t pos, uint8_t value);
uint8_t eeprom_buffered_read_byte(uint32_t pos);
}
#endif

// Bump when blob layout changes.
static constexpr uint32_t kMagic = 0xC7AB1703u;
static constexpr uint32_t kAddr = 0;

struct SettingsBlob {
  uint32_t magic;
  uint32_t brakeM;
  uint16_t brakeOnMs;
  uint16_t brakeOffMs;
  uint32_t csum;  // simple checksum of preceding fields
};

static uint32_t blobCsum(const SettingsBlob& b) {
  return b.magic ^ b.brakeM ^
         ((uint32_t)b.brakeOnMs << 16) ^ (uint32_t)b.brakeOffMs ^ 0xA5A5A5A5u;
}

static void clampInto(PlantData& plant, const SettingsBlob& b) {
  plant.brakeM = (b.brakeM > MAX_METERS) ? MAX_METERS : b.brakeM;
  plant.brakeOnMs = (b.brakeOnMs > 9999u) ? 9999u : b.brakeOnMs;
  plant.brakeOffMs = (b.brakeOffMs > 9999u) ? 9999u : b.brakeOffMs;
}

static void readBlob(SettingsBlob& b) {
#if !defined(DATA_EEPROM_BASE)
  eeprom_buffer_fill();
  uint8_t* raw = reinterpret_cast<uint8_t*>(&b);
  for (uint32_t i = 0; i < sizeof(b); ++i) {
    raw[i] = eeprom_buffered_read_byte(kAddr + i);
  }
#else
  EEPROM.get((int)kAddr, b);
#endif
}

static bool writeBlob(const SettingsBlob& b) {
#if !defined(DATA_EEPROM_BASE)
  // One fill → patch buffer → one sector erase/program (not per-byte flush).
  noInterrupts();
  eeprom_buffer_fill();
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&b);
  for (uint32_t i = 0; i < sizeof(b); ++i) {
    eeprom_buffered_write_byte(kAddr + i, raw[i]);
  }
  eeprom_buffer_flush();
  interrupts();

  SettingsBlob check{};
  readBlob(check);
  return memcmp(&check, &b, sizeof(b)) == 0;
#else
  EEPROM.put((int)kAddr, b);
  SettingsBlob check{};
  EEPROM.get((int)kAddr, check);
  return memcmp(&check, &b, sizeof(b)) == 0;
#endif
}

bool settingsLoad(PlantData& plant) {
  SettingsBlob b{};
  readBlob(b);
  if (b.magic != kMagic) return false;
  if (b.csum != blobCsum(b)) return false;
  clampInto(plant, b);
  return true;
}

void settingsSave(const PlantData& plant) {
  SettingsBlob b{};
  b.magic = kMagic;
  b.brakeM = plant.brakeM;
  b.brakeOnMs = plant.brakeOnMs;
  b.brakeOffMs = plant.brakeOffMs;
  b.csum = blobCsum(b);

  // Skip erase/program when flash already matches (avoids PC13 freeze from
  // noInterrupts() sector write on every settings VP poll).
  SettingsBlob cur{};
  readBlob(cur);
  if (memcmp(&cur, &b, sizeof(b)) == 0) return;

  if (!writeBlob(b)) {
    // Retry once after failed verify.
    writeBlob(b);
  }
}
