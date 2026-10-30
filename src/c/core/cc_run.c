#include "cc_run.h"
#include "cc_time.h"
#include "cc_steps.h"
#include <string.h>

static void enter(CcRun *r, const CcStep steps[], uint8_t i, cc_sec now) {
  const CcStep *s = &steps[r->order[i]];
  bool timed = s->type == CC_STEP_TIMED;
  r->step = i;
  r->value = CC_RUN_PLAN;
  r->paused = timed ? CC_RUN_READY : 0;  // the clock starts on Select, once you're ready
  int32_t len = timed ? cc_ramp_at(&s->amount, r->day) : 0;  // the run's own day, past midnight too
  r->paused_left = len;
  r->step_end = timed ? now + len : CC_NEVER;
}

static CcRunEvent advance(CcRun *r, const CcStep steps[], cc_sec now) {
  if (r->step + 1 >= r->step_count) {
    r->active = 0;
    r->step_end = CC_NEVER;
    return CC_RUN_FINISHED;
  }
  enter(r, steps, r->step + 1, now);
  return CC_RUN_NONE;
}

void cc_run_start(CcRun *r, const CcItem *it, const CcStep steps[], cc_sec now) {
  memset(r, 0, sizeof(*r));
  r->item_id = it->id;
  r->day = cc_day_of(now);
  r->step_count = (uint8_t)cc_steps_today(it, steps, r->day, r->order);
  r->step_end = CC_NEVER;
  r->active = r->step_count > 0;
  if (r->active) enter(r, steps, 0, now);
}

CcRunEvent cc_run_next(CcRun *r, const CcStep steps[], cc_sec now) {
  if (!r->active) return CC_RUN_NONE;
  r->advanced_at = 0;
  return advance(r, steps, now);
}

CcRunEvent cc_run_tick(CcRun *r, const CcStep steps[], cc_sec now) {
  if (!r->active || r->paused || r->step_end == CC_NEVER || now < r->step_end) return CC_RUN_NONE;
  if (advance(r, steps, now) == CC_RUN_FINISHED) return CC_RUN_FINISHED;
  r->advanced_at = now;
  return CC_RUN_TIME_UP;
}

bool cc_run_undo(CcRun *r, const CcStep steps[], cc_sec now) {
  if (!r->active || !r->advanced_at || now - r->advanced_at > CC_UNDO_S || r->step == 0) return false;
  enter(r, steps, r->step - 1, now);  // back to the timed step, at full length
  r->advanced_at = 0;
  return true;
}

void cc_run_toggle_pause(CcRun *r, cc_sec now) {
  if (!r->active) return;
  bool timed = r->step_end != CC_NEVER;
  if (r->paused) {
    r->paused = 0;
    if (timed) r->step_end = now + r->paused_left;
    r->paused_left = 0;
  } else {
    r->paused = 1;
    r->paused_left = timed && r->step_end > now ? r->step_end - now : 0;
  }
}

const CcStep *cc_run_cur(const CcRun *r, const CcStep steps[]) { return &steps[r->order[r->step]]; }

// The index a swap of the current step would bring: the main step's alternative, or back.
static int other_index(const CcRun *r, const CcItem *it, const CcStep steps[]) {
  if (!r->active) return -1;
  int n = it->step_count, total = n + it->alt_count, cur = r->order[r->step];
  if (cur < n) {
    int a = steps[cur].alt;
    return a != CC_NO_ALT && a >= n && a < total ? a : -1;
  }
  for (int j = 0; j < n; j++)
    if (steps[j].alt == cur) return j;
  return -1;
}

const CcStep *cc_run_other(const CcRun *r, const CcItem *it, const CcStep steps[]) {
  int k = other_index(r, it, steps);
  return k < 0 ? NULL : &steps[k];
}

bool cc_run_swap(CcRun *r, const CcItem *it, const CcStep steps[], cc_sec now) {
  int k = other_index(r, it, steps);
  if (k < 0) return false;
  r->order[r->step] = (uint8_t)k;
  if (k >= it->step_count) r->swapped |= 1u << r->step;
  else r->swapped &= ~(1u << r->step);
  r->advanced_at = 0;
  enter(r, steps, r->step, now);  // a timer starts over, waiting at its own length
  return true;
}

int32_t cc_run_qty(const CcRun *r, const CcStep *s) {
  return r->value != CC_RUN_PLAN ? r->value : cc_ramp_at(&s->amount, r->day);
}

static bool same_ramp(const CcRamp *a, const CcRamp *b) {
  return a->start == b->start && a->cap == b->cap && a->since == b->since && a->inc == b->inc && a->every == b->every;
}

// What a changed count depends on: the step's kind, its count and its number's scale.
static bool same_step(const CcStep *a, const CcStep *b) {
  return a->type == b->type && a->flags == b->flags && same_ramp(&a->amount, &b->amount) &&
         a->num.step == b->num.step && a->num.min == b->num.min && a->num.max == b->num.max &&
         a->num.decimals == b->num.decimals;
}

bool cc_run_refit(CcRun *r, const CcItem *it, const CcStep steps[], const CcStep *was, cc_sec now) {
  uint8_t order[CC_MAX_STEPS];
  if (it->kind != CC_ROUTINE || cc_steps_today(it, steps, r->day, order) != r->step_count) return false;
  int n = it->step_count, total = n + it->alt_count;
  uint8_t at = r->order[r->step];
  for (int i = 0; i < r->step_count; i++) {  // a swap follows its alternative; without one it is undone
    int a = steps[order[i]].alt;
    bool keep = (r->swapped >> i & 1) && a != CC_NO_ALT && a >= n && a < total;
    r->order[i] = keep ? (uint8_t)a : order[i];
    if (!keep) r->swapped &= ~(1u << i);
  }
  if (!r->active) return true;
  const CcStep *s = cc_run_cur(r, steps);
  if (r->order[r->step] != at || (was && !same_step(was, s))) r->value = CC_RUN_PLAN;  // the edit wins
  // The current step is entered again if it changed, changed kind, or got a new length before its Start.
  bool timed = s->type == CC_STEP_TIMED;
  bool relength = timed && r->paused == CC_RUN_READY && r->paused_left != cc_ramp_at(&s->amount, r->day);
  if (r->order[r->step] != at || timed != (r->step_end != CC_NEVER) || relength) {
    int32_t value = r->value;
    enter(r, steps, r->step, now);
    r->value = value;
    r->advanced_at = 0;
  }
  return true;
}

bool cc_run_alive(CcRun *r, const CcItem items[], int n, const CcStep pool[], cc_sec now) {
  if (!r->active || r->day != cc_day_of(now)) return false;
  for (int i = 0; i < n; i++)
    if (items[i].id == r->item_id) return cc_run_refit(r, &items[i], &pool[items[i].step_first], NULL, now);
  return false;
}

bool cc_run_waiting(const CcRun *r) { return r->active && r->paused == CC_RUN_READY; }

int32_t cc_run_remaining(const CcRun *r, cc_sec now) {
  if (!r->active || r->step_end == CC_NEVER) return 0;
  if (r->paused) return r->paused_left;
  return r->step_end > now ? r->step_end - now : 0;
}

bool cc_run_counts_for_today(const CcRun *r, int32_t today) { return r->day == today; }

bool cc_run_resumes(const CcRun *r, uint16_t id, int32_t today) {
  return r->active && r->item_id == id && r->day == today;
}
