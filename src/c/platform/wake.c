#include <pebble.h>
#include "wake.h"
#include "store.h"
#include "clock.h"
#include "buzz.h"
#include "../coach.h"
#include "../ui/alert_win.h"
#include "../ui/run_win.h"
#include "../ui/focus_win.h"
#include "../ui/home_win.h"
#include "../ui/today_win.h"
#include "../ui/alarm_win.h"
#include "../core/cc_fires.h"
#include "../core/cc_sched.h"
#include "../core/cc_state.h"
#include "../core/cc_time.h"
#include "../core/cc_alarm.h"
#include "../core/cc_todo.h"

#define FIRES_MAX 32

static AppTimer *s_focus_timer;  // the running Focus phase's end, while the app is open

static cc_sec step_end(void) {
  return g_run.active && !g_run.paused ? g_run.step_end : CC_NEVER;
}

// Items, the running step, Focus, alarms and note to-dos, earliest first.
static int collect(cc_sec now, CcFire f[]) {
  int n = cc_collect(g_items, g_nitems, g_day, step_end(), cc_focus_end(&g_focus), now, &g_cfg, f, FIRES_MAX);
  n = cc_alarm_fires(&g_alarms, now, f, n, FIRES_MAX);
  return cc_todo_fires(g_todos, f, n, FIRES_MAX);
}

static void focus_timer_fired(void *data) {
  s_focus_timer = NULL;
  wake_focus_check();
}

static void arm_focus_timer(cc_sec now) {
  if (s_focus_timer) { app_timer_cancel(s_focus_timer); s_focus_timer = NULL; }
  cc_sec end = cc_focus_end(&g_focus);
  if (end != CC_NEVER) s_focus_timer = app_timer_register((uint32_t)(end > now ? end - now : 0) * 1000 + 100, focus_timer_fired, NULL);
}

// While the app is open, fires already due are on screen (or about to be), so they get no
// wakeup; otherwise the alert would relaunch itself every 30 s. On exit they are included.
static void rearm(bool include_due) {
  wakeup_cancel_all();
  cc_sec now = clock_now_local();
  arm_focus_timer(now);
  CcFire f[FIRES_MAX];
  int n = collect(now, f);
  int first = 0;
  while (!include_due && first < n && f[first].at <= now + 30) first++;
  cc_sec t[CC_WAKE_MAX];
  int k = cc_plan_wakeups(f + first, n - first, now, t, CC_WAKE_MAX);
  int32_t tz = clock_tz_offset();
  for (int i = 0; i < k; i++) {
    time_t utc = t[i] - tz;
    WakeupId id = E_RANGE;
    for (int tries = 0; tries < 4 && id == E_RANGE; tries++) {  // E_RANGE: another wakeup within a minute
      id = wakeup_schedule(utc + tries * 60, 0, true);
    }
    int m = cc_min_of(t[i]);
    APP_LOG(APP_LOG_LEVEL_INFO, "wake %d at %02d:%02d:%02d -> %d", i, m / 60, m % 60, (int)(t[i] % 60), (int)id);
  }
}

void wake_rearm(void) { rearm(false); }

// The Focus screen may cover home or Today, never an alert, a menu or an editor.
static bool nothing_in_front(void) {
  return !window_stack_get_top_window() || home_win_on_top() || today_win_on_top() || focus_win_on_top();
}

bool wake_focus_check(void) {
  uint16_t lab = g_focus.label_id;
  CcFocusEvent ev = cc_focus_tick(&g_focus, clock_now_local());
  if (ev == CC_FOCUS_NONE) return false;
  APP_LOG(APP_LOG_LEVEL_INFO, "focus: %s", ev == CC_FOCUS_FINISHED ? "finished" : "next phase");
  buzz_focus(ev == CC_FOCUS_FINISHED);  // once, however many phases passed
  store_save_focus();
  if (ev == CC_FOCUS_FINISHED) focus_win_finished(lab);
  if (nothing_in_front()) focus_win_open(0);
  ui_refresh_all();
  rearm(false);
  return true;
}
void wake_rearm_on_exit(void) { rearm(true); }

static void alert_done(const CcFire *fire, CcAction a) {
  if (fire->kind == CC_FIRE_HEADSUP) { coach_apply(fire->id, CC_A_HEADSUP_SEEN, 0); return; }
  if (a == CC_A_START) { coach_start(fire->id); return; }
  const CcItem *it = coach_item(fire->id);
  cc_sec occ = it ? cc_occurrence_for(it, cc_state_for(g_day, fire->id), fire->at, &g_cfg) : fire->at;
  coach_apply(fire->id, a, occ);
}

static void todo_done(const CcFire *fire, CcAction a) {
  cc_todo_answer(g_todos, fire->id, a, clock_now_local(), g_cfg.later_task_min);
  store_save_todos();
  ui_refresh_all();
  wake_rearm();
}

int wake_push_due(void) {
  cc_sec now = clock_now_local();
  CcFire f[FIRES_MAX], due[CC_WAKE_MAX];
  int n = collect(now, f);
  int k = cc_due_now(f, n, now, due, CC_WAKE_MAX);
  int shown = 0;
  bool quiet = quiet_time_is_active();
  for (int i = 0; i < k; i++) {
    APP_LOG(APP_LOG_LEVEL_INFO, "due: item %d kind %d prio %d", due[i].id, due[i].kind, due[i].priority);
    if (due[i].kind == CC_FIRE_ALARM) { alarm_win_ring(due[i].id); shown++; continue; }  // Quiet Time or not
    if (due[i].kind == CC_FIRE_NOTE) { alert_win_push(&due[i], todo_done); shown++; continue; }  // like a task
    const CcItem *it = coach_item(due[i].id);
    if (quiet && it && cc_quiet_holds(it, due[i].kind)) {  // no screen, no buzz, not missed
      APP_LOG(APP_LOG_LEVEL_INFO, "quiet: skip item %d", due[i].id);
      alert_done(&due[i], CC_A_SKIP);
      continue;
    }
    if (due[i].kind == CC_FIRE_STEP_END) run_win_open();  // the runner advances and buzzes
    else if (due[i].kind == CC_FIRE_FOCUS_END) {
      // Ends now or within 30 s: the check (or its timer) advances it. A wakeup launch with
      // nothing on screen opens Focus so the app stays up for that moment.
      if (!wake_focus_check() && !window_stack_get_top_window()) focus_win_open(0);
    } else alert_win_push(&due[i], alert_done);
    shown++;
  }
  return shown;
}

void wake_process_due(void) {
  cc_sec now = clock_now_local();
  coach_ensure_today(now);
  if (cc_sweep(g_day, g_items, g_nitems, now, &g_cfg)) store_save_day();
  wake_push_due();
  store_save_run();
  wake_rearm();
}
