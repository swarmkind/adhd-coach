#include "t.h"
#include "cc_time.h"

// 2026-10-23 is local day 20749, a Friday. 07:00 = minute 420.
void test_time(void) {
  T_EQ(cc_weekday(0), 4);                 // 1970-01-01 Thursday
  T_EQ(cc_weekday(20454), 4);             // 2026-01-01 Thursday
  T_EQ(cc_weekday(20749), 5);             // 2026-10-23 Friday
  T_EQ(cc_at(20749, 420), 1792738800);
  T_EQ(cc_day_of(1792738800 + 59), 20749);
  T_EQ(cc_min_of(1792738800 + 59), 420);
  T_EQ(cc_local_from_utc(1792738800 - 3600, 3600), 1792738800);  // UTC 06:00 + 1h = local 07:00

  // calendar dates for the event lines
  int y, mo, md;
  cc_date_of(20749, &y, &mo, &md); T_EQ(y, 2026); T_EQ(mo, 10); T_EQ(md, 23);
  cc_date_of(20757, &y, &mo, &md); T_EQ(y, 2026); T_EQ(mo, 10); T_EQ(md, 31);
  cc_date_of(21243, &y, &mo, &md); T_EQ(y, 2028); T_EQ(mo, 2); T_EQ(md, 29);
  cc_date_of(21244, &y, &mo, &md); T_EQ(y, 2028); T_EQ(mo, 3); T_EQ(md, 1);
  cc_date_of(11016, &y, &mo, &md); T_EQ(y, 2000); T_EQ(mo, 2); T_EQ(md, 29);
  cc_date_of(10956, &y, &mo, &md); T_EQ(y, 1999); T_EQ(mo, 12); T_EQ(md, 31);
  cc_date_of(0, &y, &mo, &md); T_EQ(y, 1970); T_EQ(mo, 1); T_EQ(md, 1);
}
