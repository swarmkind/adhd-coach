#include "cc_num.h"

int cc_num_format(int32_t v, uint8_t decimals, char *buf, int len) {
  char tmp[16];
  int n = 0, digits = 0;
  if (decimals > 9) decimals = 9;
  uint32_t a = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  do {  // backwards: digits, the point after `decimals` of them, then the sign
    tmp[n++] = (char)('0' + a % 10);
    a /= 10;
    digits++;
    if (decimals && digits == decimals) tmp[n++] = '.';
  } while (a || digits <= decimals);
  if (v < 0) tmp[n++] = '-';
  if (n + 1 > len) {
    if (len > 0) buf[0] = 0;
    return -1;
  }
  for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}

int32_t cc_num_step(int32_t v, const CcNumSpec *s, int dir, uint16_t repeats) {
  int64_t mult = repeats < 10 ? 1 : repeats < 30 ? 5 : 10;
  int64_t x = (int64_t)v + (dir > 0 ? 1 : dir < 0 ? -1 : 0) * mult * s->step;
  if (x < s->min) x = s->min;
  if (x > s->max) x = s->max;
  return (int32_t)x;
}
