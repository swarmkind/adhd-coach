#include "t.h"
#include "cc_alarm.h"
#include "cc_time.h"

#define WEEKDAYS 0x3E
#define EVERY_DAY 0x7F

void test_alarm(void) {
  int32_t mon = 20384;
  while (cc_weekday(mon) != 1) mon++;
  cc_sec six = cc_at(mon, 360), seven = cc_at(mon, 420);
  CcAlarm a = { 0 };
  T_EQ(cc_alarm_next(&a, six), CC_NEVER);  // off

  // The next ring is the first matching day's time after the alarm was set.
  cc_alarm_set(&a, 420, WEEKDAYS, six);
  T_TRUE(a.on);
  T_EQ(cc_alarm_next(&a, six), seven);
  cc_alarm_set(&a, 420, WEEKDAYS, seven + 60);  // set just after today's time: tomorrow
  T_EQ(cc_alarm_next(&a, seven + 60), cc_at(mon + 1, 420));
  cc_alarm_set(&a, 420, WEEKDAYS, cc_at(mon + 4, 480));  // Friday 08:00: weekdays skip to Monday
  T_EQ(cc_alarm_next(&a, cc_at(mon + 4, 480)), cc_at(mon + 7, 420));
  cc_alarm_set(&a, 420, 1 << 1, cc_at(mon, 480));  // Mondays, set on a Monday after the time
  T_EQ(cc_alarm_next(&a, cc_at(mon, 480)), cc_at(mon + 7, 420));

  // Unanswered, it stays due; a ring missed with the watch off still sounds up to CC_ALARM_LATE.
  cc_alarm_set(&a, 420, EVERY_DAY, six);
  T_EQ(cc_alarm_next(&a, seven + 300), seven);
  T_EQ(cc_alarm_next(&a, seven + CC_ALARM_LATE), seven);
  T_EQ(cc_alarm_next(&a, seven + CC_ALARM_LATE + 1), cc_at(mon + 1, 420));

  // Stop: done until the next day it rings on.
  cc_alarm_stop(&a, seven + 20);
  T_TRUE(a.on);
  T_EQ(cc_alarm_next(&a, seven + 20), cc_at(mon + 1, 420));

  // Snooze: rings again CC_ALARM_SNOOZE_MIN after the press, and stays due until answered.
  cc_alarm_set(&a, 420, EVERY_DAY, six);
  cc_alarm_snooze(&a, seven + 30);
  cc_sec again = seven + 30 + CC_ALARM_SNOOZE_MIN * 60;
  T_EQ(cc_alarm_next(&a, seven + 31), again);
  T_EQ(cc_alarm_next(&a, again + 120), again);
  cc_alarm_snooze(&a, again + 120);  // snoozing a snooze
  T_EQ(cc_alarm_next(&a, again + 121), again + 120 + CC_ALARM_SNOOZE_MIN * 60);
  cc_alarm_stop(&a, again + 200);
  T_EQ(a.snooze, 0);
  T_EQ(cc_alarm_next(&a, again + 200), cc_at(mon + 1, 420));
  // A snooze missed with the watch off longer than CC_ALARM_LATE is dropped like a ring.
  cc_alarm_snooze(&a, seven);
  again = seven + CC_ALARM_SNOOZE_MIN * 60;
  T_EQ(cc_alarm_next(&a, again + CC_ALARM_LATE), again);
  T_EQ(cc_alarm_next(&a, again + CC_ALARM_LATE + 1), cc_at(mon + 1, 420));

  // Once: the next 07:00, then off when stopped. A snooze keeps it on until then.
  cc_alarm_set(&a, 420, 0, cc_at(mon, 600));
  T_EQ(cc_alarm_next(&a, cc_at(mon, 600)), cc_at(mon + 1, 420));
  cc_alarm_snooze(&a, cc_at(mon + 1, 420));
  T_TRUE(a.on);
  cc_alarm_stop(&a, cc_at(mon + 1, 431));
  T_TRUE(!a.on);
  T_EQ(cc_alarm_next(&a, cc_at(mon + 1, 431)), CC_NEVER);

  // Turned off while snoozed: silent.
  cc_alarm_set(&a, 420, EVERY_DAY, six);
  cc_alarm_snooze(&a, seven);
  a.on = 0;
  T_EQ(cc_alarm_next(&a, seven + 60), CC_NEVER);

  // Fires: each alarm's next ring joins the sorted fires, critical, id = its index in the list.
  CcAlarms l = { 0 };
  l.n = 3;
  cc_alarm_set(&l.a[0], 480, EVERY_DAY, six);
  cc_alarm_set(&l.a[1], 400, EVERY_DAY, six);
  cc_alarm_set(&l.a[2], 450, EVERY_DAY, six);
  l.a[2].on = 0;
  CcFire f[4] = { { cc_at(mon, 410), 9, CC_FIRE_DUE, CC_NORMAL } };
  int n = cc_alarm_fires(&l, six, f, 1, 4);
  T_EQ(n, 3);
  T_EQ(f[0].at, cc_at(mon, 400)); T_EQ(f[0].kind, CC_FIRE_ALARM); T_EQ(f[0].id, 1); T_EQ(f[0].priority, CC_CRITICAL);
  T_EQ(f[1].id, 9);
  T_EQ(f[2].at, cc_at(mon, 480)); T_EQ(f[2].id, 0);
  CcFire g[2] = { { cc_at(mon, 410), 9, CC_FIRE_DUE, CC_NORMAL } };
  T_EQ(cc_alarm_fires(&l, six, g, 1, 2), 2);  // full: the latest ring is the one left out
  T_EQ(g[0].id, 1); T_EQ(g[1].id, 9);

  cc_alarm_remove(&l, 0);
  T_EQ(l.n, 2); T_EQ(l.a[0].at_min, 400); T_EQ(l.a[1].at_min, 450);
  cc_alarm_remove(&l, 5);  // out of range: nothing
  T_EQ(l.n, 2);

  // Vibrate only or sound: setting (or turning back on) keeps the choice.
  a.mode = CC_ALARM_VIBRATE;
  cc_alarm_set(&a, 480, EVERY_DAY, six);
  T_EQ(a.mode, CC_ALARM_VIBRATE);

  // Saved alarms read back; 0.7.0's, saved before the mode existed, read as sound alarms.
  CcAlarms saved = { 0 }, back;
  saved.n = 2;
  cc_alarm_set(&saved.a[0], 405, WEEKDAYS, six);
  cc_alarm_set(&saved.a[1], 1439, 0, six);
  saved.a[1].mode = CC_ALARM_VIBRATE;
  saved.a[1].snooze = seven;
  T_TRUE(cc_alarms_read(&back, (const uint8_t *)&saved, sizeof(saved)));
  T_EQ(back.n, 2); T_EQ(back.a[1].at_min, 1439); T_EQ(back.a[1].mode, CC_ALARM_VIBRATE); T_EQ(back.a[1].snooze, seven);
  struct { uint8_t n; struct { uint16_t at_min; uint8_t days, on; int32_t handled, snooze; } a[CC_ALARM_MAX]; } old = { 0 };
  old.n = 2;
  old.a[0].at_min = 405; old.a[0].days = WEEKDAYS; old.a[0].on = 1; old.a[0].handled = six;
  old.a[1].at_min = 1439; old.a[1].on = 1; old.a[1].snooze = seven;
  memset(&back, 0xFF, sizeof(back));
  T_TRUE(cc_alarms_read(&back, (const uint8_t *)&old, sizeof(old)));
  T_EQ(back.n, 2);
  T_EQ(back.a[0].at_min, 405); T_EQ(back.a[0].days, WEEKDAYS); T_EQ(back.a[0].on, 1); T_EQ(back.a[0].handled, six);
  T_EQ(back.a[0].mode, CC_ALARM_SOUND);
  T_EQ(back.a[1].at_min, 1439); T_EQ(back.a[1].snooze, seven); T_EQ(back.a[1].mode, CC_ALARM_SOUND);
  T_TRUE(!cc_alarms_read(&back, (const uint8_t *)&saved, -1));  // nothing saved
  T_EQ(back.n, 0);
  saved.n = CC_ALARM_MAX + 1;
  T_TRUE(!cc_alarms_read(&back, (const uint8_t *)&saved, sizeof(saved)));
  T_EQ(back.n, 0);
  T_TRUE(!cc_alarms_read(&back, (const uint8_t *)&saved, 50));  // neither size
  T_EQ(back.n, 0);

  // "In N min": rings once, at exactly now + N min, and is gone once stopped.
  CcAlarms in = { 0 };
  in.n = 2;
  cc_alarm_set(&in.a[0], 480, EVERY_DAY, six);
  in.a[1].mode = CC_ALARM_VIBRATE;
  cc_sec now = six + 25;  // 06:00:25
  cc_alarm_set_in(&in.a[1], 5, now);
  T_TRUE(in.a[1].on); T_EQ(in.a[1].at_min, 365); T_EQ(in.a[1].mode, CC_ALARM_VIBRATE);
  T_STR(cc_days_name(in.a[1].days), "Once");
  T_EQ(cc_alarm_next(&in.a[1], now), now + 300);
  T_EQ(cc_alarm_next(&in.a[1], now + 300 + CC_ALARM_LATE + 1), CC_NEVER);  // missed long ago: not again tomorrow
  cc_alarm_snooze(&in.a[1], now + 300);
  T_EQ(cc_alarm_next(&in.a[1], now + 301), now + 900);
  cc_alarms_stop(&in, 1, now + 905);
  T_EQ(in.n, 1); T_EQ(in.a[0].at_min, 480);  // gone; the others stay
  cc_alarms_stop(&in, 0, cc_at(mon, 480) + 5);  // an ordinary alarm stays, on
  T_EQ(in.n, 1); T_TRUE(in.a[0].on);
  cc_alarms_stop(&in, 3, now);  // out of range: nothing
  T_EQ(in.n, 1);
  // Turned off, then on again from the list: a plain once alarm at its time.
  cc_alarm_set_in(&in.a[0], 60, now);
  in.a[0].on = 0; in.a[0].snooze = 0;
  cc_alarm_set(&in.a[0], in.a[0].at_min, in.a[0].days, now + 10);
  T_EQ(in.a[0].days, 0); T_EQ(cc_alarm_next(&in.a[0], now + 10), cc_at(mon, 420));
  cc_alarms_stop(&in, 0, cc_at(mon, 421));
  T_EQ(in.n, 1); T_TRUE(!in.a[0].on);

  // The editor's day choices, in order, and their names.
  T_EQ(cc_days_preset(0), 0);
  T_EQ(cc_days_preset(1), EVERY_DAY);
  T_EQ(cc_days_preset(2), WEEKDAYS);
  T_EQ(cc_days_preset(3), 0x41);
  T_EQ(cc_days_preset(4), 1 << 1);                  // Mondays first
  T_EQ(cc_days_preset(CC_DAYS_PRESETS - 1), 1);     // Sundays last
  T_EQ(cc_days_index(WEEKDAYS), 2);
  T_EQ(cc_days_index(0x15), 0);                     // not a choice: back to the first
  T_STR(cc_days_name(0), "Once");
  T_STR(cc_days_name(EVERY_DAY), "Every day");
  T_STR(cc_days_name(WEEKDAYS), "Weekdays");
  T_STR(cc_days_name(0x41), "Weekends");
  T_STR(cc_days_name(1 << 3), "Wednesdays");
  T_STR(cc_days_name(1), "Sundays");
}
