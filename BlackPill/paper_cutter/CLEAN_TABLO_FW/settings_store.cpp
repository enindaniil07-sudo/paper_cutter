#include "settings_store.h"
#include "config.h"

#include <EEPROM.h>

static constexpr uint32_t kMagic = 0xC7AB1701u;
static constexpr int kAddr = 0;

struct SettingsBlob {
  uint32_t magic;
  uint32_t brakeM;
  uint16_t brakeOnMs;
  uint16_t brakeOffMs;
  uint16_t speedLimitCms;
  uint8_t speedLimitEn;
  uint8_t reserved;
};

static void clampInto(PlantData& plant, const SettingsBlob& b) {
  plant.brakeM = (b.brakeM > MAX_METERS) ? MAX_METERS : b.brakeM;
  plant.brakeOnMs = (b.brakeOnMs > 9999u) ? 9999u : b.brakeOnMs;
  plant.brakeOffMs = (b.brakeOffMs > 9999u) ? 9999u : b.brakeOffMs;
  plant.speedLimitCms =
      (b.speedLimitCms > MAX_SPEED_CMS) ? MAX_SPEED_CMS : b.speedLimitCms;
  plant.speedLimitEn = (b.speedLimitEn != 0);
}

bool settingsLoad(PlantData& plant) {
  SettingsBlob b{};
  EEPROM.get(kAddr, b);
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
  b.speedLimitCms = plant.speedLimitCms;
  b.speedLimitEn = plant.speedLimitEn ? 1u : 0u;
  b.reserved = 0;
  EEPROM.put(kAddr, b);
}
