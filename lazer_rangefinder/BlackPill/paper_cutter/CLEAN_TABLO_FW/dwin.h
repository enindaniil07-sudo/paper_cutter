#pragma once

#include <Arduino.h>

void dwinBegin(uint32_t baud);
void dwinWriteU16(uint16_t vp, uint16_t value);
void dwinWriteU16IfChanged(uint16_t vp, uint16_t value, uint16_t& cache);
/** Big-endian long at VP / VP+1 (DGUS V_Type=1). */
void dwinWriteU32(uint16_t vp, uint32_t value);
void dwinWriteU32IfChanged(uint16_t vp, uint32_t value, uint32_t& cache);
void dwinClearCmd(uint16_t vp);
void dwinRequestReadU16(uint16_t vp);
/** Read 2 words (long) from VP. */
void dwinRequestReadU32(uint16_t vp);
void dwinSetPage(uint16_t page);

typedef void (*DwinVpHandler)(uint16_t vp, uint32_t value);
void dwinPoll(DwinVpHandler onVp);
