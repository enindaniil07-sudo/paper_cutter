/*
  Second-order digital PLL for a ÷N frequency divider (reference = input square wave).

  - Period path: EWMA on measured T_in between reference rising edges.
  - Phase path: phase error = (t_ref - t_last_out_rise) wrapped to [-T_out/2, T_out/2]
    with T_out = N * T_in (output full period).
  - VCO: locked half-period = nominal N*T_in/2 + PI correction (µs).

  Tuned for very low frequencies (~0.1–1 Hz). Adjust Kp/Ki if lock is sluggish or rings.
*/
#ifndef PLL_H
#define PLL_H

#include <stdint.h>

struct DivNPLL {
  uint8_t divN;

  uint32_t T_in_ewma_us;
  uint32_t locked_half_us;

  int32_t phase_i;
  int32_t last_pe_us;

  // Correction = (Kp * pe + Ki * phase_i) >> K_SHIFT
  static const int32_t K_SHIFT = 10;
  static const int32_t Kp = 48;
  static const int32_t Ki = 3;

  void reset() {
    divN = 3;
    T_in_ewma_us = 0;
    locked_half_us = 0;
    phase_i = 0;
    last_pe_us = 0;
  }

  void setDiv(uint8_t n) {
    divN = (n == 0) ? 1 : n;
  }

  static int32_t wrapPhaseErrUs(int64_t d, int64_t t_out_full) {
    if (t_out_full <= 0) {
      return 0;
    }
    const int64_t tf = t_out_full;
    int64_t r = d % tf;
    if (r < 0) {
      r += tf;
    }
    if (r > tf / 2) {
      r -= tf;
    }
    return (int32_t)r;
  }

  /*
    Call on each reference rising edge.
    - ref_us: micros() at this rising edge
    - period_in_us: ref_us - previous_ref_us (0 on first edge)
    - out_rise_us: micros() when D9 last went HIGH; 0 if unknown
  */
  void onRefRise(uint32_t ref_us, uint32_t period_in_us, uint32_t out_rise_us) {
    if (divN == 0) {
      divN = 1;
    }
    if (period_in_us < 50U) {
      return;
    }

    if (T_in_ewma_us == 0U) {
      T_in_ewma_us = period_in_us;
    } else {
      T_in_ewma_us = (7U * T_in_ewma_us + period_in_us) / 8U;
    }

    uint64_t t_full64 = (uint64_t)divN * (uint64_t)T_in_ewma_us;
    if (t_full64 > 0xFFFFFFFFULL) {
      t_full64 = 0xFFFFFFFFULL;
    }
    const uint32_t t_out_full = (uint32_t)t_full64;
    if (t_out_full < 4U) {
      locked_half_us = 2U;
      return;
    }

    const uint32_t nominal_half = t_out_full / 2U;
    if (nominal_half == 0U) {
      locked_half_us = 1U;
      return;
    }

    int32_t pe = 0;
    if (out_rise_us != 0U) {
      const int64_t d = (int64_t)ref_us - (int64_t)out_rise_us;
      pe = wrapPhaseErrUs(d, (int64_t)t_out_full);
    }
    last_pe_us = pe;

    const int64_t lim = (int64_t)t_out_full / 4;
    int64_t pi = (int64_t)phase_i + (int64_t)pe;
    if (pi > lim) {
      pi = lim;
    }
    if (pi < -lim) {
      pi = -lim;
    }
    phase_i = (int32_t)pi;

    int64_t corr = ((int64_t)Kp * (int64_t)pe + (int64_t)Ki * (int64_t)phase_i) >> K_SHIFT;
    int64_t nh = (int64_t)nominal_half + corr;
    if (nh < 1) {
      nh = 1;
    }
    if (nh > 2000000000LL) {
      nh = 2000000000LL;
    }
    locked_half_us = (uint32_t)nh;
  }

  uint32_t inputPeriodEstimateUs() const {
    return T_in_ewma_us;
  }

  int32_t lastPhaseErrUs() const {
    return last_pe_us;
  }
};

#endif
