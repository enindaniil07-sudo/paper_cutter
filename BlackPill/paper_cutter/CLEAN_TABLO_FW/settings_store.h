#pragma once

#include <Arduino.h>
#include "fsm.h"

/** Load settings from flash EEPROM emulation. Returns false if empty/invalid. */
bool settingsLoad(PlantData& plant);

/** Persist settings when any field changed. */
void settingsSave(const PlantData& plant);
