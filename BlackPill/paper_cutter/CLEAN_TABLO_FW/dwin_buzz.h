#pragma once

#include <Arduino.h>

/** DWIN system VP 0x00A0 — duration in 8 ms units. */
void dwinBuzzBegin();

/** Short chirp on error pages. */
void dwinBuzzError();

/** Three beeps on СТОП / штатная остановка. */
void dwinBuzzStopTriple();

/**
 * Call every loop: keeps beeping while relayIsHigh;
 * advances stop-triple pattern. Triple has priority over relay tone.
 */
void dwinBuzzTick(uint32_t nowMs, bool relayIsHigh);
