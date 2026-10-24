#include "cc_focus.h"
#include <string.h>

static cc_sec len_s(const CcFocus *f) {
  int m = f->phase == CC_FOCUS_WORK ? f->work_min : f->phase == CC_FOCUS_SHORT ? f->short_min : f->long_min;
  return (cc_sec)m * 60;
}

void cc_focus_start(CcFocus *f, const CcSettings *cfg, uint16_t label_id, cc_sec now) {
  memset(f, 0, sizeof(*f));
  f->active = 1; f->phase = CC_FOCUS_WORK; f->cycle = 1;
  f->cycles = cfg->focus_cycles ? cfg->focus_cycles : 1;
  f->work_min = cfg->focus_work_min; f->short_min = cfg->focus_short_min; f->long_min = cfg->focus_long_min;
  f->label_id = label_id;
  f->phase_end = now + len_s(f);
}

// The phase after the current one, starting at `start`. False when the long break ended.
static bool advance(CcFocus *f, cc_sec start) {
  if (f->phase == CC_FOCUS_LONG) { cc_focus_stop(f); return false; }
  if (f->phase == CC_FOCUS_SHORT) { f->phase = CC_FOCUS_WORK; f->cycle++; }
  else f->phase = f->cycle >= f->cycles ? CC_FOCUS_LONG : CC_FOCUS_SHORT;
  f->phase_end = start + len_s(f);
  return true;
}

CcFocusEvent cc_focus_tick(CcFocus *f, cc_sec now) {
  bool moved = false;
  while (f->active && !f->paused && now >= f->phase_end) {
    if (!advance(f, f->phase_end)) return CC_FOCUS_FINISHED;
    moved = true;
  }
  return moved ? CC_FOCUS_PHASE : CC_FOCUS_NONE;
}

CcFocusEvent cc_focus_skip(CcFocus *f, cc_sec now) {
  if (!f->active) return CC_FOCUS_NONE;
  f->paused = 0; f->paused_left = 0;
  return advance(f, now) ? CC_FOCUS_PHASE : CC_FOCUS_FINISHED;
}

void cc_focus_toggle_pause(CcFocus *f, cc_sec now) {
  if (!f->active) return;
  if (f->paused) {
    f->phase_end = now + f->paused_left;
    f->paused = 0; f->paused_left = 0;
  } else {
    f->paused_left = f->phase_end > now ? f->phase_end - now : 0;
    f->phase_end = CC_NEVER;
    f->paused = 1;
  }
}

void cc_focus_add(CcFocus *f, int minutes, cc_sec now) {
  if (!f->active) return;
  if (f->paused) f->paused_left += minutes * 60;
  else f->phase_end = (f->phase_end > now ? f->phase_end : now) + minutes * 60;
}

int32_t cc_focus_remaining(const CcFocus *f, cc_sec now) {
  if (!f->active) return 0;
  if (f->paused) return f->paused_left;
  return f->phase_end > now ? f->phase_end - now : 0;
}

void cc_focus_stop(CcFocus *f) {
  f->active = 0; f->paused = 0; f->paused_left = 0;
  f->phase_end = CC_NEVER;
}

cc_sec cc_focus_end(const CcFocus *f) { return f->active && !f->paused ? f->phase_end : CC_NEVER; }

static bool in_box(CcBox b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

CcFocusTap cc_focus_tap(CcBox work, CcBox brk, int x, int y) {
  if (in_box(work, x, y)) return x < work.x + work.w / 2 ? CC_TAP_WORK_LESS : CC_TAP_WORK_MORE;
  if (in_box(brk, x, y)) return x < brk.x + brk.w / 2 ? CC_TAP_SHORT_LESS : CC_TAP_SHORT_MORE;
  return CC_TAP_NONE;
}

// The next multiple of step above (or below) v, kept within lo..hi.
static int step_to(int v, int dir, int step, int lo, int hi) {
  int n = dir > 0 ? (v / step + 1) * step : ((v + step - 1) / step - 1) * step;
  return n < lo ? lo : n > hi ? hi : n;
}

bool cc_focus_adjust(CcSettings *cfg, CcFocusTap tap) {
  uint8_t *v;
  int dir, step, lo, hi;
  switch (tap) {
    case CC_TAP_WORK_LESS: case CC_TAP_WORK_MORE:
      v = &cfg->focus_work_min; step = CC_FOCUS_WORK_STEP; lo = CC_FOCUS_WORK_MIN; hi = CC_FOCUS_WORK_MAX; break;
    case CC_TAP_SHORT_LESS: case CC_TAP_SHORT_MORE:
      v = &cfg->focus_short_min; step = CC_FOCUS_SHORT_STEP; lo = CC_FOCUS_SHORT_MIN; hi = CC_FOCUS_SHORT_MAX; break;
    default: return false;
  }
  dir = tap == CC_TAP_WORK_MORE || tap == CC_TAP_SHORT_MORE ? 1 : -1;
  if (dir > 0 ? *v >= hi : *v <= lo) return false;
  *v = (uint8_t)step_to(*v, dir, step, lo, hi);
  return true;
}
