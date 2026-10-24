#include "cc_alarm.h"
#include "cc_time.h"
#include <string.h>

#define V070_ALARM 12  // bytes per alarm before the mode

static const uint8_t PRESETS[CC_DAYS_PRESETS] = { 0, 0x7F, 0x3E, 0x41, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 };
static const char *const NAMES[CC_DAYS_PRESETS] = { "Once", "Every day", "Weekdays", "Weekends", "Mondays", "Tuesdays",
                                                    "Wednesdays", "Thursdays", "Fridays", "Saturdays", "Sundays" };

cc_sec cc_alarm_next(const CcAlarm *a, cc_sec now) {
  if (!a->on) return CC_NEVER;
  if (a->snooze >= now - CC_ALARM_LATE) return a->snooze;  // never when 0 (not snoozed)
  cc_sec lo = now - CC_ALARM_LATE > a->handled ? now - CC_ALARM_LATE : a->handled + 1;
  for (int32_t d = cc_day_of(lo), last = d + 7; d <= last; d++) {
    cc_sec t = cc_at(d, a->at_min);
    if (t >= lo && (!a->days || (a->days >> cc_weekday(d)) & 1)) return t;
  }
  return CC_NEVER;
}

void cc_alarm_set(CcAlarm *a, uint16_t at_min, uint8_t days, cc_sec now) {
  a->at_min = at_min;
  a->days = days;
  a->on = 1;
  a->handled = now;
  a->snooze = 0;
}

void cc_alarm_stop(CcAlarm *a, cc_sec now) {
  a->handled = now;
  a->snooze = 0;
  if (!a->days) a->on = 0;
}

void cc_alarm_snooze(CcAlarm *a, cc_sec now) {
  a->handled = now;
  a->snooze = now + CC_ALARM_SNOOZE_MIN * 60;
}

void cc_alarm_remove(CcAlarms *l, int i) {
  if (i < 0 || i >= l->n) return;
  for (l->n--; i < l->n; i++) l->a[i] = l->a[i + 1];
}

bool cc_alarms_read(CcAlarms *l, const uint8_t *buf, int len) {
  memset(l, 0, sizeof(*l));
  if (len < 1 || buf[0] > CC_ALARM_MAX) return false;
  if (len == (int)sizeof(*l)) {
    memcpy(l, buf, sizeof(*l));
    return true;
  }
  if (len != (int)offsetof(CcAlarms, a) + CC_ALARM_MAX * V070_ALARM) return false;
  l->n = buf[0];
  for (int i = 0; i < l->n; i++) memcpy(&l->a[i], buf + offsetof(CcAlarms, a) + i * V070_ALARM, V070_ALARM);
  return true;
}

int cc_alarm_fires(const CcAlarms *l, cc_sec now, CcFire f[], int n, int max) {
  for (int i = 0; i < l->n; i++) {
    cc_sec t = cc_alarm_next(&l->a[i], now);
    if (t != CC_NEVER) n = cc_fire_insert(f, n, max, (CcFire){ t, (uint16_t)i, CC_FIRE_ALARM, CC_CRITICAL });
  }
  return n;
}

uint8_t cc_days_preset(int i) { return PRESETS[i]; }

int cc_days_index(uint8_t days) {
  for (int i = 0; i < CC_DAYS_PRESETS; i++)
    if (PRESETS[i] == days) return i;
  return 0;
}

const char *cc_days_name(uint8_t days) { return NAMES[cc_days_index(days)]; }
