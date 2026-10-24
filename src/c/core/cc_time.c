#include "cc_time.h"

int32_t cc_day_of(cc_sec t) {
  int32_t d = t / CC_DAY;
  if (t % CC_DAY < 0) d--;
  return d;
}

uint16_t cc_min_of(cc_sec t) {
  return (uint16_t)((t - cc_day_of(t) * CC_DAY) / 60);
}

uint8_t cc_weekday(int32_t day) {
  int32_t w = (day + 4) % 7;  // day 0 was a Thursday
  return (uint8_t)(w < 0 ? w + 7 : w);
}

cc_sec cc_at(int32_t day, uint16_t min) {
  return day * CC_DAY + (cc_sec)min * 60;
}

// Days to a Gregorian date (Howard Hinnant's civil_from_days), integers only.
void cc_date_of(int32_t day, int *year, int *month, int *mday) {
  int32_t z = day + 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  int32_t doe = z - era * 146097;
  int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int32_t mp = (5 * doy + 2) / 153;
  int32_t m = mp < 10 ? mp + 3 : mp - 9;
  *year = (int)(yoe + era * 400 + (m <= 2));
  *month = (int)m;
  *mday = (int)(doy - (153 * mp + 2) / 5 + 1);
}

cc_sec cc_local_from_utc(int32_t utc, int32_t tz_offset_s) {
  return utc + tz_offset_s;
}
