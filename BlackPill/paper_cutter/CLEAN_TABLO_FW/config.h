#pragma once

#include <Arduino.h>

// CLEAN_TABLO — D:\paper_cutter\DWIN\CLEAN_TABLO
// Encoder Autonics E40S6-1000-3-T-24 (datasheet: 1000 P/R, phases A/B/Z), wheel Ø 8 cm

#ifndef USE_USART1
#define USE_USART1 0
#endif

// --- Mechanics (datasheet + wheel) ---
// Resolution: 1000 pulses/revolution on channel A (rising edges).
// C = π × 0.08 m ≈ 0.251327412 m/rev
// Distance [m] = N × C / PPR
// Speed   [m/s] = ΔN × C / (PPR × Δt)
// RPM           = (ΔN / PPR) × (60 / Δt)
static constexpr uint16_t ENC_PPR = 1000;
static constexpr float WHEEL_D_M = 0.08f;
static constexpr float CIRC_M = 3.14159265f * WHEEL_D_M;
// Fixed-point: nanometers per A-pulse (round(π × 0.08 × 1e9 / 1000))
static constexpr uint32_t NM_PER_PULSE = 251327UL;

// Макс. ЗАДАНО: 5 цифр ArtText/VarInput (N_Int=5) → 0…99999
static constexpr uint32_t MAX_METERS = 99999u;
static constexpr uint16_t MAX_SPEED_CMS = 9999;  // VP ×0.01 m/s
static constexpr uint16_t MAX_RPM = 9999;

// Скорость — чаще; ОСТАЛОСЬ/прогресс — чуть реже (UART).
static constexpr uint16_t SPEED_PERIOD_MS = 40;
static constexpr uint16_t TRAVEL_PERIOD_MS = 50;
static constexpr uint16_t BUTTON_POLL_MS = 40;
static constexpr uint16_t BUTTON_DEBOUNCE_MS = 120;
static constexpr uint16_t TARGET_POLL_MS = 200;
// Нет импульсов дольше этого → скорость/RPM в 0.
static constexpr uint16_t SPEED_IDLE_ZERO_MS = 150;
// TargetGate Resetting: пока панель не подтвердит ЗАДАНО=0 (макс. окно).
static constexpr uint16_t RESET_TARGET_LOCK_MS = 5000;
// Optical encoder: only reject impossible edge rates (noise), not mechanical bounce.
// 25 µs → max ~40 kHz ≈ 2400 RPM @ 1000 P/R (well above cutter speeds).
static constexpr uint32_t ENC_MIN_EDGE_US = 25;
// EMA weight for speed/RPM: out = (inst + (N-1)*prev) / N  (N=4 → α=0.25)
static constexpr uint8_t SPEED_EMA_N = 4;
// Сколько подряд «обратных» rising A нужно, прежде чем показать ошибку.
// 1 = сразу; 12 ≈ 4°; 40 ≈ 14° вала @ 1000 P/R.
static constexpr uint8_t ENC_REV_CONFIRM = 40;
// Если между обратными фронтами больше этого — streak сбрасывается (шум).
static constexpr uint32_t ENC_REV_STREAK_GAP_US = 80000;  // 80 ms

// On A rising: B is always sampled for reverse detection.
// ENC_USE_DIR=1 additionally ignores reverse pulses for distance (same as reverse error path).
#ifndef ENC_USE_DIR
#define ENC_USE_DIR 0
#endif
// ENC_FORWARD_B_LEVEL defined below with PAGE_*

static constexpr int PIN_ENC_A = PB0;  // 3.3 V max after level-shift
static constexpr int PIN_ENC_B = PB1;
static constexpr int PIN_LED = PC13;

// --- VP (must match CLEAN_TABLO) ---
static constexpr uint16_t VP_TARGET = 0x6000;   // long: ЗАДАНО, целые м
static constexpr uint16_t VP_TRAVEL = 0x6010;   // long: ОСТАЛОСЬ, целые м (как ЗАДАНО)
static constexpr uint16_t VP_SPEED = 0x6020;
static constexpr uint16_t VP_RPM = 0x6024;
static constexpr uint16_t VP_PROGRESS = 0x6030; // 0..100 %

static constexpr uint16_t VP_START = 0x6050;
static constexpr uint16_t VP_STOP = 0x6051;
static constexpr uint16_t VP_RESET = 0x6052;
static constexpr uint16_t VP_KB_OPEN = 0x6053;
static constexpr uint16_t VP_ERR_ACK = 0x6054;  // «ИСПРАВИТЬ» на стр. ошибки

static constexpr uint16_t VP_KB_BUF = 0x6080;
static constexpr uint16_t VP_KB_1 = 0x60A1;
static constexpr uint16_t VP_KB_2 = 0x60A2;
static constexpr uint16_t VP_KB_3 = 0x60A3;
static constexpr uint16_t VP_KB_4 = 0x60A4;
static constexpr uint16_t VP_KB_5 = 0x60A5;
static constexpr uint16_t VP_KB_6 = 0x60A6;
static constexpr uint16_t VP_KB_7 = 0x60A7;
static constexpr uint16_t VP_KB_8 = 0x60A8;
static constexpr uint16_t VP_KB_9 = 0x60A9;
static constexpr uint16_t VP_KB_0 = 0x60AA;
static constexpr uint16_t VP_KB_DEL = 0x60AB;
static constexpr uint16_t VP_KB_OK = 0x60AC;
static constexpr uint16_t VP_KB_CANCEL = 0x60AD;

static constexpr uint16_t PAGE_MAIN = 0;
static constexpr uint16_t PAGE_KEYPAD = 10;
static constexpr uint16_t PAGE_ERROR = 11;  // реверс энкодера

// На rising A: B == ENC_FORWARD_B_LEVEL → вперёд; иначе → ошибка реверса.
// Если сообщение появляется при «правильном» направлении — инвертируй уровень.
static constexpr int ENC_FORWARD_B_LEVEL = LOW;
