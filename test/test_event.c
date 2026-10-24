#include "t.h"
#include "cc_event.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

static CcItem event_on(int32_t day, uint16_t at, int preset) {
  CcItem e; memset(&e, 0, sizeof(e));
  e.id = 9; e.kind = CC_EVENT; e.event_day = day; e.at_min = at;
  e.ladder_count = (uint8_t)cc_ladder_preset(preset, e.ladder);
  strcpy(e.name, "Dentist check-up");
  return e;
}

static void set_ladder(CcItem *e, const int8_t *l, int n) {
  for (int i = 0; i < n; i++) e->ladder[i] = l[i];
  e->ladder_count = (uint8_t)n;
}

void test_event(void) {
  CcSettings cfg = CC_SETTINGS_DEFAULT;
  int8_t l[CC_LADDER_MAX];
  cc_sec r[CC_LADDER_MAX];

  // presets
  T_EQ(cc_ladder_preset(CC_LADDER_NORMAL, l), 3);
  T_EQ(l[0], 7); T_EQ(l[1], 1); T_EQ(l[2], CC_RUNG_MORNING);
  T_EQ(cc_ladder_preset(CC_LADDER_PERSISTENT, l), 6);
  T_EQ(l[0], 21); T_EQ(l[1], 7); T_EQ(l[2], 3); T_EQ(l[3], 1); T_EQ(l[4], CC_RUNG_MORNING); T_EQ(l[5], CC_RUNG_HOUR);
  T_EQ(cc_ladder_preset(CC_LADDER_GENTLE, l), 2);
  T_EQ(l[0], 1); T_EQ(l[1], CC_RUNG_MORNING);

  // Normal ladder, event D+8 10:30
  CcItem e = event_on(D + 8, 630, CC_LADDER_NORMAL);
  T_EQ(cc_event_at(&e), cc_at(D + 8, 630));
  T_EQ(cc_event_rungs(&e, &cfg, r, CC_LADDER_MAX), 3);
  T_EQ(r[0], cc_at(D + 1, 540)); T_EQ(r[1], cc_at(D + 7, 540)); T_EQ(r[2], cc_at(D + 8, 540));

  // Persistent ladder, same event
  e = event_on(D + 8, 630, CC_LADDER_PERSISTENT);
  T_EQ(cc_event_rungs(&e, &cfg, r, CC_LADDER_MAX), 6);
  T_EQ(r[0], cc_at(D - 13, 540)); T_EQ(r[1], cc_at(D + 1, 540)); T_EQ(r[2], cc_at(D + 5, 540));
  T_EQ(r[3], cc_at(D + 7, 540)); T_EQ(r[4], cc_at(D + 8, 540)); T_EQ(r[5], cc_at(D + 8, 570));

  // Morning of falls back to an hour before when the event is earlier than 10:00
  const int8_t mh[] = { CC_RUNG_MORNING, CC_RUNG_HOUR };
  e = event_on(D, 480, CC_LADDER_GENTLE); set_ladder(&e, mh, 2);
  T_EQ(cc_rung_at(&e, CC_RUNG_MORNING, &cfg), cc_at(D, 420));
  T_EQ(cc_event_rungs(&e, &cfg, r, CC_LADDER_MAX), 1);  // both rungs are 07:00: one alert
  T_EQ(r[0], cc_at(D, 420));
  e = event_on(D, 570, CC_LADDER_GENTLE); set_ladder(&e, mh, 2);  // 09:30: both an hour before
  T_EQ(cc_event_rungs(&e, &cfg, r, CC_LADDER_MAX), 1);
  T_EQ(r[0], cc_at(D, 510));
  e = event_on(D, 540, CC_LADDER_GENTLE); set_ladder(&e, mh, 1);
  T_EQ(cc_rung_at(&e, CC_RUNG_MORNING, &cfg), cc_at(D, 480));  // never at the event's own time

  // Hour before a 00:30 event lands the evening before
  const int8_t h[] = { CC_RUNG_HOUR };
  e = event_on(D, 30, CC_LADDER_GENTLE); set_ladder(&e, h, 1);
  T_EQ(cc_event_rungs(&e, &cfg, r, CC_LADDER_MAX), 1);
  T_EQ(r[0], cc_at(D - 1, 1410));

  // The reminder time follows settings
  cfg.event_time_min = 480;
  e = event_on(D + 8, 630, CC_LADDER_NORMAL);
  T_EQ(cc_rung_at(&e, 7, &cfg), cc_at(D + 1, 480));

  // Today shows an event on its rung days: the first rung that day, 0 when it has none
  cfg = (CcSettings)CC_SETTINGS_DEFAULT;
  e = event_on(D + 8, 630, CC_LADDER_PERSISTENT);
  T_EQ(cc_event_first_rung_on(&e, D + 1, &cfg), cc_at(D + 1, 540));
  T_EQ(cc_event_first_rung_on(&e, D + 8, &cfg), cc_at(D + 8, 540));
  T_EQ(cc_event_first_rung_on(&e, D + 2, &cfg), 0);

  // Less than an hour after the reminder hour: the day-of rung moves to an hour before.
  CcSettings c9 = CC_SETTINGS_DEFAULT;  // reminders at 09:00
  e = event_on(D + 8, 570, CC_LADDER_NORMAL);  // 09:30
  T_EQ(cc_rung_at(&e, CC_RUNG_MORNING, &c9), cc_at(D + 8, 510));
  e = event_on(D + 8, 600, CC_LADDER_NORMAL);  // 10:00: an hour before is 09:00 either way
  T_EQ(cc_rung_at(&e, CC_RUNG_MORNING, &c9), cc_at(D + 8, 540));
  e = event_on(D + 8, 540, CC_LADDER_NORMAL);  // 09:00: unchanged, 08:00
  T_EQ(cc_rung_at(&e, CC_RUNG_MORNING, &c9), cc_at(D + 8, 480));
  e = event_on(D + 8, 570, CC_LADDER_PERSISTENT);  // morning and hour-before merge at 08:30
  T_EQ(cc_event_rungs(&e, &c9, r, CC_LADDER_MAX), 5);
  T_EQ(r[4], cc_at(D + 8, 510));
}
