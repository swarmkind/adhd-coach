#include <pebble.h>
#include "coach.h"
#include "platform/store.h"
#include "platform/clock.h"
#include "platform/wake.h"
#include "platform/sample.h"
#include "config.h"
#include "ui/home_win.h"
#include "ui/run_win.h"
#include "core/cc_run.h"
#include "core/cc_sched.h"
#include "core/cc_time.h"

int coach_slot(uint16_t id) {
  for (int i = 0; i < g_nitems; i++)
    if (g_items[i].id == id) return i;
  return -1;
}

const CcItem *coach_item(uint16_t id) {
  int i = coach_slot(id);
  return i < 0 ? NULL : &g_items[i];
}

void coach_ensure_today(cc_sec now) {
  int32_t today = cc_day_of(now);
  if (g_run.active && g_run.day != today && !run_win_showing()) {  // parked on an earlier day: that one is over
    g_run.active = 0;
    g_run.step_end = CC_NEVER;
    store_save_run();
  }
  if (g_day.day == today) return;
  int32_t old = g_day.day;
  if (g_day.n) cc_sweep(&g_day, g_items, g_nitems, cc_at(old + 1, 0) - 1, &g_cfg);  // the old day's last second
  CcOutcome out[CC_MAX_ITEMS];
  int n = cc_rollover(&g_day, g_items, g_nitems, today, out, CC_MAX_ITEMS);
  store_history_push(out, n, old);
  store_save_day();
  APP_LOG(APP_LOG_LEVEL_INFO, "rollover %d -> %d, %d outcomes", (int)old, (int)today, n);
}

static CcItemState *state_for(uint16_t id) {
  CcItemState *st = cc_state_for(&g_day, id);
  if (!st && g_day.n < CC_MAX_ITEMS) {  // item added since the day began
    st = &g_day.s[g_day.n++];
    memset(st, 0, sizeof(*st));
    st->id = id;
  }
  return st;
}

void coach_apply(uint16_t id, CcAction a, cc_sec occurrence) {
  const CcItem *it = coach_item(id);
  if (!it) return;
  cc_sec now = clock_now_local();
  coach_ensure_today(now);
  CcItemState *st = state_for(id);
  if (!st) return;
  cc_apply(st, it, a, now, occurrence, &g_cfg);
  store_save_day();
  if (g_run.active && g_run.item_id == id && (a == CC_A_DONE || a == CC_A_SKIP || a == CC_A_PAUSE)) {
    g_run.active = 0;  // finished or skipped from elsewhere: the parked run is over
    g_run.step_end = CC_NEVER;
    store_save_run();
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "apply item %d action %d -> status %d count %d", id, a, st->status, st->count);
  ui_refresh_all();
  wake_rearm();
}

void coach_start(uint16_t id) {
  int slot = coach_slot(id);
  if (slot < 0) return;
  if (cc_run_resumes(&g_run, id, cc_day_of(clock_now_local()))) { run_win_open(); return; }  // resume, don't restart
  coach_apply(id, CC_A_START, 0);
  const CcItem *it = &g_items[slot];
  if (it->kind != CC_ROUTINE || !it->step_count) return;
  cc_run_start(&g_run, it, g_steps[slot], clock_now_local());
  store_save_run();
  run_win_open();
}

cc_sec coach_current_occurrence(uint16_t id) {
  const CcItem *it = coach_item(id);
  cc_sec now = clock_now_local();
  if (!it) return now;
  CcItemState *st = cc_state_for(&g_day, id);
  cc_sec due = cc_next_due(it, st, now, &g_cfg);
  if (due == CC_NEVER || cc_day_of(due) != cc_day_of(now)) return now;
  return cc_occurrence_for(it, st, due, &g_cfg);
}

void coach_minute(void) {
  cc_sec now = clock_now_local();
  coach_ensure_today(now);
  if (cc_sweep(&g_day, g_items, g_nitems, now, &g_cfg)) store_save_day();
  ui_refresh_all();
}

void coach_demo_reset(void) {
#if COACH_DEMO
  cc_sec now = clock_now_local();
  sample_load(now);
  memset(&g_day, 0, sizeof(g_day));
  CcOutcome none[1];
  cc_rollover(&g_day, g_items, g_nitems, cc_day_of(now), none, 0);  // fresh states, no history
  memset(&g_run, 0, sizeof(g_run));
  g_run.step_end = CC_NEVER;
  store_save_day();
  store_save_run();
  APP_LOG(APP_LOG_LEVEL_INFO, "demo reset: %d items", g_nitems);
  ui_refresh_all();
  wake_process_due();
#endif
}
