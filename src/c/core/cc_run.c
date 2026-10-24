#include "cc_run.h"
#include "cc_time.h"
#include <string.h>

static void enter(CcRun *r, const CcStep steps[], uint8_t i, cc_sec now) {
  r->step = i;
  r->paused = 0;
  r->paused_left = 0;
  r->step_end = steps[i].type == CC_STEP_TIMED ? now + steps[i].dur_s : CC_NEVER;
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
  r->step_count = it->step_count > CC_MAX_STEPS ? CC_MAX_STEPS : it->step_count;
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

int32_t cc_run_remaining(const CcRun *r, cc_sec now) {
  if (!r->active || r->step_end == CC_NEVER) return 0;
  if (r->paused) return r->paused_left;
  return r->step_end > now ? r->step_end - now : 0;
}

bool cc_run_counts_for_today(const CcRun *r, int32_t today) { return r->day == today; }

bool cc_run_resumes(const CcRun *r, uint16_t id, int32_t today) {
  return r->active && r->item_id == id && r->day == today;
}
