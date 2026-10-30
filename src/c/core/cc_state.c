#include "cc_state.h"
#include "cc_sched.h"
#include "cc_time.h"
#include "cc_event.h"
#include "cc_steps.h"
#include "cc_fires.h"
#include <string.h>

// A slot or rung dealt with. An event's next rung gets its own auto-Later (reminders have none).
static void reminder_handled(CcItemState *st, cc_sec occurrence) {
  if (occurrence > st->handled_until) st->handled_until = occurrence;
  st->later_until = 0;
  st->auto_later_used = 0;
}

// An event's Later stays 10 min clear of the event; 0 when that leaves no room.
static cc_sec event_later(const CcItem *it, cc_sec until, cc_sec now) {
  cc_sec cap = cc_event_at(it) - 600;
  if (until > cap) until = cap;
  return until > now ? until : 0;
}

static void defer(CcItemState *st, const CcItem *it, cc_sec until, cc_sec now, cc_sec occurrence) {
  if (it->kind == CC_EVENT) until = event_later(it, until, now);
  if (until) st->later_until = until;
  else reminder_handled(st, occurrence);
}

void cc_apply(CcItemState *st, const CcItem *it, CcAction a, cc_sec now, cc_sec occurrence, const CcSettings *cfg) {
  bool rem = it->kind == CC_REMINDER, ev = it->kind == CC_EVENT, slots = rem || ev;
  cc_sec later = (cc_sec)(rem ? cfg->later_reminder_min : ev ? cfg->later_event_min : cfg->later_task_min) * 60;
  switch (a) {
    case CC_A_START: st->status = CC_ST_ACTIVE; st->later_until = 0; break;
    case CC_A_PAUSE: st->status = CC_ST_PAUSED; st->later_until = 0; break;
    case CC_A_RESUME:  // only undoes a pause; a reminder skips the slots it slept through
      if (st->status != CC_ST_PAUSED) break;
      st->status = CC_ST_NONE;
      if (slots) reminder_handled(st, cc_slot_at_or_before(it, now, cfg));
      break;
    case CC_A_HEADSUP_SEEN: st->flags |= CC_SF_HEADSUP_SHOWN; break;
    case CC_A_LATER: defer(st, it, now + later, now, occurrence); break;
    case CC_A_LATER_LONG: defer(st, it, now + CC_LATER_LONG_MIN * 60, now, occurrence); break;
    case CC_A_DONE:
      if (!slots) { st->status = CC_ST_DONE; st->later_until = 0; break; }
      if (st->count < 255) st->count++;
      reminder_handled(st, occurrence);
      break;
    case CC_A_SKIP:
      if (!slots) { st->status = CC_ST_SKIPPED; st->later_until = 0; break; }
      reminder_handled(st, occurrence);  // skip this one: neither counted nor missed
      break;
    case CC_A_IGNORED:
      if (rem) {  // no automatic Later for reminders
        if (st->missed < 255) st->missed++;
        reminder_handled(st, occurrence);
      } else if (ev) {  // one auto-Later per rung; the next rung gets its own
        cc_sec t = st->auto_later_used ? 0 : event_later(it, now + later, now);
        if (t) { st->auto_later_used = 1; st->later_until = t; break; }
        if (st->missed < 255) st->missed++;
        reminder_handled(st, occurrence);
      } else if (!st->auto_later_used) {
        st->auto_later_used = 1;
        st->later_until = now + later;
      } else {
        st->status = CC_ST_MISSED;
        st->later_until = 0;
      }
      break;
  }
}

CcItemState *cc_state_for(CcDay *d, uint16_t id) {
  for (int i = 0; i < d->n; i++)
    if (d->s[i].id == id) return &d->s[i];
  return NULL;
}

bool cc_quiet_holds(const CcItem *it, uint8_t fire_kind) {
  return it->kind == CC_REMINDER && fire_kind == CC_FIRE_DUE;
}

static bool miss(CcItemState *s, cc_sec slot) {
  if (slot <= s->handled_until) return false;
  if (s->missed < 255) s->missed++;
  s->handled_until = slot;
  return true;
}

static bool sweep_one(CcItemState *s, const CcItem *it, int32_t day, cc_sec lo, const CcSettings *cfg) {
  if (s->status != CC_ST_NONE || (it->flags & (CC_F_SILENT | CC_F_ANYTIME))) return false;
  if (it->kind != CC_EVENT && !cc_item_on_day(it, day)) return false;
  if (it->kind == CC_ROUTINE || it->kind == CC_TASK) {
    cc_sec due = s->later_until ? s->later_until : cc_at(day, it->at_min);
    if (due >= lo) return false;
    s->status = CC_ST_MISSED;
    s->later_until = 0;
    return true;
  }
  if (it->goal && s->count >= it->goal) return false;
  if (s->later_until >= lo) return false;  // a pending Later covers its slot
  bool changed = s->later_until != 0;
  s->later_until = 0;
  if (it->kind == CC_EVENT) {
    cc_sec r[CC_LADDER_MAX];
    int n = cc_event_rungs(it, cfg, r, CC_LADDER_MAX);
    bool missed = false;
    for (int i = 0; i < n && r[i] < lo; i++)
      if (cc_day_of(r[i]) == day && miss(s, r[i])) missed = true;
    if (missed) s->auto_later_used = 0;
    return changed || missed;
  }
  int step = it->every_min ? it->every_min : 1440;
  for (int m = it->at_min; m <= it->until_min; m += step) {
    cc_sec t = cc_at(day, (uint16_t)m);
    if (t >= lo) break;
    if (miss(s, t)) changed = true;
  }
  return changed;
}

int cc_sweep(CcDay *d, const CcItem items[], int n, cc_sec now, const CcSettings *cfg) {
  int changed = 0;
  for (int i = 0; i < n; i++) {
    CcItemState *s = cc_state_for(d, items[i].id);
    if (s && sweep_one(s, &items[i], d->day, now - cc_catchup_s(&items[i], cfg), cfg)) changed++;
  }
  return changed;
}

int cc_rollover(CcDay *d, const CcItem items[], int n, int32_t today, CcOutcome out[], int max) {
  if (d->day == today) return 0;
  int w = 0;
  for (int i = 0; i < d->n && w < max; i++) {
    const CcItemState *s = &d->s[i];
    out[w++] = (CcOutcome){ s->id, s->status, s->count, s->missed };
  }
  if (n > CC_MAX_ITEMS) n = CC_MAX_ITEMS;
  memset(d, 0, sizeof(*d));
  d->day = today;
  d->n = (uint8_t)n;
  for (int i = 0; i < n; i++) d->s[i].id = items[i].id;
  return w;
}

int cc_anytime_next(const CcItem items[], int n, CcDay *d) {
  for (int i = 0; i < n; i++) {
    const CcItem *it = &items[i];
    CcItemState *st = cc_state_for(d, it->id);
    if (it->kind == CC_TASK && (it->flags & CC_F_ANYTIME) && cc_item_on_day(it, d->day) && (!st || st->status == CC_ST_NONE))
      return i;
  }
  return -1;
}
