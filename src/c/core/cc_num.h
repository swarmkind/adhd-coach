#pragma once
// Fixed-point numbers: v scaled by 10^decimals (82.4 kg = 824, decimals 1).
#include "cc_types.h"

int cc_num_format(int32_t v, uint8_t decimals, char *buf, int len);  // chars written, or -1 ("" in buf)

// One press (dir +1 / -1) with acceleration: repeats < 10 -> 1 step, < 30 -> 5, else 10. Clamped to [min, max].
int32_t cc_num_step(int32_t v, const CcNumSpec *s, int dir, uint16_t repeats);
