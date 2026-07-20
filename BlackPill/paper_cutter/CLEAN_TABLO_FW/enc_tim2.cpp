#include "enc_tim2.h"
#include "config.h"

#if defined(STM32F4xx)
#include "stm32f4xx.h"
#else
#error "enc_tim2 requires STM32F4 (Black Pill F411)"
#endif

static uint32_t s_lastCnt = 0;

void encTim2Begin() {
  // Clocks
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
  (void)RCC->APB1ENR;

  // PA0 / PA1 → AF1 TIM2_CH1 / TIM2_CH2 (do not touch PA2/PA3 USART2)
  GPIOA->MODER =
      (GPIOA->MODER & ~((3u << 0) | (3u << 2))) | (2u << 0) | (2u << 2);
  GPIOA->OTYPER &= ~((1u << 0) | (1u << 1));
  GPIOA->OSPEEDR |= (3u << 0) | (3u << 2);
  GPIOA->PUPDR =
      (GPIOA->PUPDR & ~((3u << 0) | (3u << 2))) | (1u << 0) | (1u << 2);
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~0xFFu) | (1u << 0) | (1u << 4);

  TIM2->CR1 = 0;
  TIM2->PSC = 0;
  TIM2->ARR = 0xFFFFFFFFu;

  // IC1=TI1, IC2=TI2 + digital filter (less noise → fewer false reverses)
  TIM2->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0 |
                (0x6u << TIM_CCMR1_IC1F_Pos) | (0x6u << TIM_CCMR1_IC2F_Pos);
  TIM2->CCER = 0;  // rising-active inputs (CC1P/CC2P = 0)

  // Encoder mode 2: count on TI1 (A) edges, direction from TI2 (B)
  // → 2 counts / A-period → 2000 counts / rev @ 1000 P/R
  TIM2->SMCR = (TIM2->SMCR & ~TIM_SMCR_SMS) | TIM_SMCR_SMS_1;

  TIM2->CNT = 0;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->SR = 0;
  TIM2->DIER = 0;  // no IRQ — poll UIF in encTim2Poll()
  TIM2->CR1 = TIM_CR1_CEN;

  s_lastCnt = 0;
}

void encTim2Clear() {
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->CNT = 0;
  TIM2->SR = 0;
  TIM2->CR1 |= TIM_CR1_CEN;
  s_lastCnt = 0;
}

void encTim2SyncBaseline() {
  s_lastCnt = TIM2->CNT;
  TIM2->SR = (uint32_t)~TIM_SR_UIF;
}

uint32_t encTim2Cnt() { return TIM2->CNT; }

EncTim2Delta encTim2Poll() {
  EncTim2Delta d = {};
  d.forward = 0;
  d.reverse = 0;
  d.overflow = false;

  const uint32_t cnt = TIM2->CNT;
  const bool uif = (TIM2->SR & TIM_SR_UIF) != 0;

  if (uif) {
    // Clear update flag. Soft path stays in FSM (g_nm) — display not cleared.
    TIM2->SR = (uint32_t)~TIM_SR_UIF;
    d.overflow = true;

    const bool down = (TIM2->CR1 & TIM_CR1_DIR) != 0;
    uint32_t spanned;
    if (!down) {
      // Up-count wrap: lastCnt .. 0xFFFFFFFF, then 0 .. cnt
      spanned = (0xFFFFFFFFu - s_lastCnt) + 1u + cnt;
      d.forward = spanned;
    } else {
      // Down-count wrap: lastCnt .. 0, then 0xFFFFFFFF .. cnt
      spanned = s_lastCnt + 1u + (0xFFFFFFFFu - cnt);
      d.reverse = spanned;
    }

    // Reset hardware counter; soft totals live outside TIM2.
    TIM2->CNT = 0;
    s_lastCnt = 0;
    return d;
  }

  const int32_t delta = (int32_t)(cnt - s_lastCnt);
  s_lastCnt = cnt;
  if (delta > 0) {
    d.forward = (uint32_t)delta;
  } else if (delta < 0) {
    d.reverse = (uint32_t)(-delta);
  }
  return d;
}
