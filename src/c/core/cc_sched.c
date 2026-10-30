#include "cc_sched.h"
#include "cc_time.h"
#include "cc_event.h"
#include "cc_steps.h"

static const CcItemState FRESH;

static bool on_day(const CcItem *it, int32_t day) { return cc_item_on_day(it, day); }
static int every(const CcItem *it) { return it->every_min ? it->every_min : 1440; }  // 0 = one slot

cc_sec cc_catchup_s(const CcItem *it, const CcSettings *cfg) {
  int m = it->grace_min > cfg->catchup_min ? it->grace_min : cfg->catchup_min;
  return (cc_sec)m * 60;
}

// First reminder slot on `day` after `after` and not before `lo`.
static cc_sec next_slot(const CcItem *it, int32_t day, cc_sec after, cc_sec lo) {
  for (int m = it->at_min; m <= it->until_min; m += every(it)) {
    cc_sec t = cc_at(day, (uint16_t)m);
    if (t > after && t >= lo) return t;
  }
  return CC_NEVER;
}

// Today's alert, or CC_NEVER if today has nothing left.
static cc_sec due_today(const CcItem *it, const CcItemState *s, int32_t day, cc_sec lo) {
  if (!on_day(it, day) || s->status != CC_ST_NONE) return CC_NEVER;
  if (it->kind != CC_REMINDER) {
    cc_sec t = s->later_until ? s->later_until : cc_at(day, it->at_min);  // a Later replaces the slot
    return t >= lo ? t : CC_NEVER;
  }
  if (it->goal && s->count >= it->goal) return CC_NEVER;
  // A pending Later covers every slot up to its own time.
  cc_sec after = s->handled_until > s->later_until ? s->handled_until : s->later_until;
  cc_sec t = next_slot(it, day, after, lo);
  if (s->later_until && s->later_until >= lo && s->later_until < t) t = s->later_until;
  return t;
}

// An event's rungs work like reminder slots, except that they span days. Rungs before today
// belonged to a state that is gone; status silences today's rungs only.
static cc_sec event_due(const CcItem *it, const CcItemState *s, int32_t day, cc_sec lo, const CcSettings *cfg) {
  cc_sec r[CC_LADDER_MAX], t = CC_NEVER;
  int n = cc_event_rungs(it, cfg, r, CC_LADDER_MAX);
  bool quiet = s->status != CC_ST_NONE;
  cc_sec after = s->handled_until > s->later_until ? s->handled_until : s->later_until;
  for (int i = 0; i < n && t == CC_NEVER; i++) {
    int32_t rd = cc_day_of(r[i]);
    if (rd < day || r[i] < lo || (rd == day && (quiet || r[i] <= after))) continue;
    t = r[i];
  }
  if (!quiet && s->later_until && s->later_until >= lo && s->later_until < t) t = s->later_until;
  return t;
}

cc_sec cc_next_due(const CcItem *it, const CcItemState *today, cc_sec now, const CcSettings *cfg) {
  if (it->flags & (CC_F_SILENT | CC_F_ANYTIME)) return CC_NEVER;
  int32_t day = cc_day_of(now);
  if (it->kind == CC_EVENT) return event_due(it, today ? today : &FRESH, day, now - cc_catchup_s(it, cfg), cfg);
  cc_sec t = due_today(it, today ? today : &FRESH, day, now - cc_catchup_s(it, cfg));
  if (t != CC_NEVER) return t;
  for (int k = 1; k <= 31; k++)  // a rhythm can be up to CC_EVERY_MAX days apart
    if (on_day(it, day + k)) return cc_at(day + k, it->at_min);
  return CC_NEVER;
}

cc_sec cc_occurrence_for(const CcItem *it, const CcItemState *today, cc_sec due, const CcSettings *cfg) {
  if ((it->kind != CC_REMINDER && it->kind != CC_EVENT) || !today || !today->later_until ||
      due != today->later_until) return due;
  cc_sec s = cc_slot_at_or_before(it, due, cfg);
  return s ? s : due;
}

cc_sec cc_slot_at_or_before(const CcItem *it, cc_sec t, const CcSettings *cfg) {
  if (it->kind == CC_EVENT) {
    cc_sec r[CC_LADDER_MAX], best = 0;
    int n = cc_event_rungs(it, cfg, r, CC_LADDER_MAX);
    for (int i = 0; i < n && r[i] <= t; i++)
      if (cc_day_of(r[i]) == cc_day_of(t)) best = r[i];
    return best;
  }
  int m = cc_min_of(t);
  if (it->kind != CC_REMINDER || m < it->at_min) return 0;
  if (m > it->until_min) m = it->until_min;
  return cc_at(cc_day_of(t), (uint16_t)(it->at_min + (m - it->at_min) / every(it) * every(it)));
}

cc_sec cc_next_headsup(const CcItem *it, const CcItemState *today, cc_sec now, const CcSettings *cfg) {
  if (it->kind != CC_ROUTINE && it->kind != CC_TASK) return CC_NEVER;
  int lead = it->priority == CC_CRITICAL ? cfg->headsup_critical_min
           : it->priority == CC_IMPORTANT ? cfg->headsup_important_min : 0;
  if (!lead) return CC_NEVER;
  if (!today) today = &FRESH;
  cc_sec due = cc_next_due(it, today, now, cfg);
  if (due == CC_NEVER || due <= now) return CC_NEVER;
  int32_t day = cc_day_of(now);
  cc_sec hu = due - (cc_sec)lead * 60;
  if (cc_day_of(hu) < day) return CC_NEVER;  // belonged to yesterday's state, which is gone
  if (cc_day_of(hu) == day && (today->flags & CC_SF_HEADSUP_SHOWN)) return CC_NEVER;
  if (cc_day_of(due) == day && today->later_until) return CC_NEVER;  // due is a Later: already alerted
  return hu;
}
