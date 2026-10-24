#include "cc_event.h"
#include "cc_time.h"
#include <string.h>

static const int8_t PRESETS[][CC_LADDER_MAX + 1] = {  // count, then rungs
  [CC_LADDER_GENTLE] = { 2, 1, CC_RUNG_MORNING },
  [CC_LADDER_NORMAL] = { 3, 7, 1, CC_RUNG_MORNING },
  [CC_LADDER_PERSISTENT] = { 6, 21, 7, 3, 1, CC_RUNG_MORNING, CC_RUNG_HOUR },
};

int cc_ladder_preset(int preset, int8_t out[CC_LADDER_MAX]) {
  if (preset < CC_LADDER_GENTLE || preset > CC_LADDER_PERSISTENT) return 0;
  memcpy(out, &PRESETS[preset][1], CC_LADDER_MAX);
  return PRESETS[preset][0];
}

cc_sec cc_event_at(const CcItem *ev) { return cc_at(ev->event_day, ev->at_min); }

cc_sec cc_rung_at(const CcItem *ev, int8_t rung, const CcSettings *cfg) {
  if (rung > 0) return cc_at(ev->event_day - rung, cfg->event_time_min);
  // Morning-of is an hour before for an event less than an hour after the reminder hour.
  if (rung == CC_RUNG_HOUR || (rung == CC_RUNG_MORNING && ev->at_min < cfg->event_time_min + 60))
    return cc_event_at(ev) - 3600;
  if (rung == CC_RUNG_MORNING) return cc_at(ev->event_day, cfg->event_time_min);
  return CC_NEVER;
}

int cc_event_rungs(const CcItem *ev, const CcSettings *cfg, cc_sec out[], int max) {
  int n = 0, cnt = ev->ladder_count > CC_LADDER_MAX ? CC_LADDER_MAX : ev->ladder_count;
  cc_sec at = cc_event_at(ev);
  for (int i = 0; i < cnt; i++) {
    cc_sec t = cc_rung_at(ev, ev->ladder[i], cfg);
    if (t >= at) continue;  // CC_NEVER, or a reminder time at or after the event
    int j = n;
    while (j > 0 && out[j - 1] > t) j--;
    if ((j > 0 && out[j - 1] == t) || n >= max) continue;
    memmove(&out[j + 1], &out[j], (size_t)(n - j) * sizeof(cc_sec));
    out[j] = t;
    n++;
  }
  return n;
}

cc_sec cc_event_first_rung_on(const CcItem *ev, int32_t day, const CcSettings *cfg) {
  cc_sec r[CC_LADDER_MAX];
  int n = cc_event_rungs(ev, cfg, r, CC_LADDER_MAX);
  for (int i = 0; i < n; i++)
    if (cc_day_of(r[i]) == day) return r[i];
  return 0;
}
