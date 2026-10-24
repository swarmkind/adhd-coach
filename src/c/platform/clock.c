#include <pebble.h>
#include "clock.h"
#include "../core/cc_time.h"

int32_t clock_tz_offset(void) {
  time_t t = time(NULL);
  struct tm l = *localtime(&t);  // copy before gmtime reuses the buffer
  struct tm g = *gmtime(&t);
  int32_t off = (l.tm_hour - g.tm_hour) * 3600 + (l.tm_min - g.tm_min) * 60 + (l.tm_sec - g.tm_sec);
  int days = l.tm_year != g.tm_year ? (l.tm_year > g.tm_year ? 1 : -1) : l.tm_yday - g.tm_yday;
  return off + days * CC_DAY;
}

cc_sec clock_now_local(void) { return cc_local_from_utc((int32_t)time(NULL), clock_tz_offset()); }
