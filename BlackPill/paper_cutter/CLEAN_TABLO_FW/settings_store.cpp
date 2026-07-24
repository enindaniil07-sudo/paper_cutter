#include "settings_store.h"
#include "config.h"

#include <EEPROM.h>

// Bump when blob layout changes (speed-limit fields removed).
static constexpr uint32_t kMagic = 0xC7AB1702u;
static constexpr int kAddr = 0;

struct SettingsBlob {
  uint32_t magic;
  uint32_t brakeM;
  uint16_t brakeOnMs;
  uint16_t brakeOffMs;
};

static void clampInto(PlantData& plant, const SettingsBlob& b) {
  plant.brakeM = (b.brakeM > MAX_METERS) ? MAX_METERS : b.brakeM;
  plant.brakeOnMs = (b.brakeOnMs > 9999u) ? 9999u : b.brakeOnMs;
  plant.brakeOffMs = (b.brakeOffMs > 9999u) ? 9999u : b.brakeOffMs;
}

bool settingsLoad(PlantData& plant) {
  SettingsBlob b{};
  uint8_t* raw = reinterpret_cast<uint8_t*>(&b);
  for (unsigned i = 0; i < sizeof(b); ++i) {
    raw[i] = EEPROM.read(kAddr + (int)i);
  }
  if (b.magic != kMagic) return false;
  clampInto(plant, b);
  return true;
}

void settingsSave(const PlantData& plant) {
  SettingsBlob b{};
  b.magic = kMagic;
  b.brakeM = plant.brakeM;
  b.brakeOnMs = plant.brakeOnMs;
  b.brakeOffMs = plant.brakeOffMs;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&b);
  // EEPROM.update → eeprom_write_byte → flash flush (public API, core-safe).
  for (unsigned i = 0; i < sizeof(b); ++i) {
    EEPROM.update(kAddr + (int)i, raw[i]);
  }
}
