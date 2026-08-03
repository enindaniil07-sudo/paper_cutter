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
// nm per TIM count: π × 0.08 × 1e9 / 2000
static constexpr uint32_t NM_PER_COUNT = 125664UL;

static constexpr uint32_t MAX_METERS = 99999u;
static constexpr uint16_t MAX_SPEED_CMS = 65535;  // U16 max ≈ 655.35 м/с (панель XXX.XX)

static constexpr uint16_t SPEED_PERIOD_MS = 40;
static constexpr uint16_t TRAVEL_PERIOD_MS = 50;
static constexpr uint16_t BUTTON_POLL_MS = 40;
static constexpr uint16_t BUTTON_DEBOUNCE_MS = 120;
static constexpr uint16_t TARGET_POLL_MS = 200;
static constexpr uint16_t SPEED_IDLE_ZERO_MS = 150;
static constexpr uint16_t RESET_TARGET_LOCK_MS = 5000;
static constexpr uint8_t SPEED_EMA_N = 4;

// Reverse: TIM counts (mode 2). 80 counts ≈ 14° shaft @ 2000 counts/rev.
static constexpr uint8_t ENC_REV_CONFIRM = 80;
static constexpr uint32_t ENC_REV_STREAK_GAP_US = 80000;

// TIM2_CH1 / TIM2_CH2 — PA0/PA1 (PA2/PA3 busy: USART2 ↔ DWIN).
static constexpr int PIN_LED = PC13;
// Реле тормоза: бывшие пины энкодера PB0/PB1 свободны. HIGH = катушка/драйвер вкл.
static constexpr int PIN_RELAY = PB0;

// --- VP ---
static constexpr uint16_t VP_TARGET = 0x6000;
static constexpr uint16_t VP_TRAVEL = 0x6010;
static constexpr uint16_t VP_SPEED = 0x6020;
static constexpr uint16_t VP_PROGRESS = 0x6030;

static constexpr uint16_t VP_STOP = 0x6051;
static constexpr uint16_t VP_RESET = 0x6052;
static constexpr uint16_t VP_KB_OPEN = 0x6053;
static constexpr uint16_t VP_ERR_ACK = 0x6054;
static constexpr uint16_t VP_SETTINGS = 0x6055;
static constexpr uint16_t VP_SETTINGS_BACK = 0x6056;

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

// Settings (page 17 list → keyboard page 18). 6090 is LONG (4 B).
static constexpr uint16_t VP_BRAKE = 0x6090;          // м, U32
static constexpr uint16_t VP_BRAKE_ON_MS = 0x6094;    // ШИМ «1» мс
static constexpr uint16_t VP_BRAKE_OFF_MS = 0x6096;   // ШИМ «0» мс
static constexpr uint16_t VP_ENC_INVERT = 0x6098;     // 0=норма, 1=инверсия A/B

// DWIN system: buzzer duration (unit 8 ms). Example 0x007D ≈ 1 s.
static constexpr uint16_t VP_DWIN_BUZZ = 0x00A0;
static constexpr uint16_t BUZZ_ERROR_MS = 200;
static constexpr uint16_t BUZZ_STOP_BEEP_MS = 120;
static constexpr uint16_t BUZZ_STOP_GAP_MS = 100;
static constexpr uint16_t BUZZ_RELAY_BEEP_MS = 220;
static constexpr uint16_t BUZZ_RELAY_PERIOD_MS = 180;

static constexpr uint16_t PAGE_MAIN = 0;
static constexpr uint16_t PAGE_KEYPAD = 10;
static constexpr uint16_t PAGE_ERR_REVERSE = 11;
static constexpr uint16_t PAGE_ERR_NO_ENC = 12;
// Pages 13–15 reserved on panel (unused by FW).
static constexpr uint16_t PAGE_ERR_BRAKE = 16;  // тормоз не замедляет

// После СТАРТ (Run): нет импульсов ни с A, ни с B дольше N мс → стр. 12.
// Любой импульс TIM / смена CNT сбрасывает таймер.
static constexpr bool ENC_NO_SIGNAL_ENABLE = true;
static constexpr uint32_t ENC_NO_SIGNAL_MS = 4000u;

// В зоне торможения: раз в PERIOD_MS сравниваем EMA-скорость с прошлой пробой.
// Не упала (и всё ещё ≥ MIN) → стр. 16.
static constexpr bool BRAKE_EFF_ENABLE = true;
static constexpr uint32_t BRAKE_EFF_PERIOD_MS = 5000u;
static constexpr uint16_t BRAKE_EFF_MIN_CMS = 15;  // ниже = уже почти стоп, OK
// Нет импульсов столько мс → вал остановлен (отпуск реле).
static constexpr uint32_t BRAKE_HOLD_IDLE_MS = 400u;
