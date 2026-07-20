#pragma once

#include <Arduino.h>

// CLEAN_TABLO — D:\paper_cutter\DWIN\CLEAN_TABLO
// Encoder Autonics E40S6-1000-3-T-24 + wheel Ø 8 cm
// Path: TIM2 hardware encoder (32-bit CNT), MCU polls CNT + UIF.

#ifndef USE_USART1
#define USE_USART1 0
#endif

// --- Mechanics ---
// Datasheet: 1000 P/R on channel A.
// TIM2 Encoder mode 2 counts both edges of A → 2000 TIM counts / revolution.
static constexpr uint16_t ENC_PPR = 1000;
static constexpr uint32_t ENC_COUNTS_PER_REV = 2000u;
static constexpr float WHEEL_D_M = 0.08f;
static constexpr float CIRC_M = 3.14159265f * WHEEL_D_M;
// nm per TIM count: π × 0.08 × 1e9 / 2000
static constexpr uint32_t NM_PER_COUNT = 125664UL;
// Legacy alias (1 A-rising ≈ 2 TIM counts)
static constexpr uint32_t NM_PER_PULSE = 251327UL;

static constexpr uint32_t MAX_METERS = 99999u;
static constexpr uint16_t MAX_SPEED_CMS = 9999;
static constexpr uint16_t MAX_RPM = 9999;

static constexpr uint16_t SPEED_PERIOD_MS = 40;
static constexpr uint16_t TRAVEL_PERIOD_MS = 50;
static constexpr uint16_t BUTTON_POLL_MS = 40;
static constexpr uint16_t BUTTON_DEBOUNCE_MS = 120;
static constexpr uint16_t TARGET_POLL_MS = 200;
static constexpr uint16_t SPEED_IDLE_ZERO_MS = 150;
static constexpr uint16_t RESET_TARGET_LOCK_MS = 5000;
static constexpr uint8_t SPEED_EMA_N = 4;

// Reverse: TIM counts (mode 2). 80 counts ≈ 40 old A-rising ≈ 14° shaft.
static constexpr uint8_t ENC_REV_CONFIRM = 80;
static constexpr uint32_t ENC_REV_STREAK_GAP_US = 80000;

// TIM2_CH1 / TIM2_CH2 — MUST rewire encoder from PB0/PB1 → PA0/PA1.
// PA2/PA3 busy (USART2 ↔ DWIN).
static constexpr int PIN_ENC_A = PA0;
static constexpr int PIN_ENC_B = PA1;
static constexpr int PIN_LED = PC13;

// --- VP ---
static constexpr uint16_t VP_TARGET = 0x6000;
static constexpr uint16_t VP_TRAVEL = 0x6010;
static constexpr uint16_t VP_SPEED = 0x6020;
static constexpr uint16_t VP_RPM = 0x6024;
static constexpr uint16_t VP_PROGRESS = 0x6030;

static constexpr uint16_t VP_START = 0x6050;
static constexpr uint16_t VP_STOP = 0x6051;
static constexpr uint16_t VP_RESET = 0x6052;
static constexpr uint16_t VP_KB_OPEN = 0x6053;
static constexpr uint16_t VP_ERR_ACK = 0x6054;

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
static constexpr uint16_t PAGE_ERR_REVERSE = 11;
static constexpr uint16_t PAGE_ERR_NO_ENC = 12;
static constexpr uint16_t PAGE_ERR_NO_TARGET = 13;
static constexpr uint16_t PAGE_ERR_SPEED_JUMP = 14;
static constexpr uint16_t PAGE_ERR_CHANNEL = 15;
// Legacy alias
static constexpr uint16_t PAGE_ERROR = PAGE_ERR_REVERSE;

// Run без импульсов TIM → «нет сигнала»
static constexpr uint32_t ENC_NO_SIGNAL_MS = 4000u;
// Один канал активен, второй молчит
static constexpr uint32_t ENC_CH_DEAD_MS = 500u;
static constexpr uint8_t ENC_CH_MIN_EDGES = 8;
// Скачок: instant > max(EMA×ratio, EMA+abs) при EMA ≥ floor
static constexpr uint8_t SPEED_JUMP_RATIO = 4;
static constexpr uint16_t SPEED_JUMP_ABS_CMS = 150;   // +1.5 м/с
static constexpr uint16_t SPEED_JUMP_MIN_EMA_CMS = 30; // EMA ≥ 0.30 м/с

