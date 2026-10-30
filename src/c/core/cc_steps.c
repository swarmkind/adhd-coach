#include "cc_steps.h"
#include "cc_time.h"

static const CcStep *s_pool;

int32_t cc_ramp_at(const CcRamp *r, int32_t day) {
  if (!r->inc || !r->every || day <= r->since) return r->start;
  int32_t v = r->start + r->inc * ((day - r->since) / r->every);
  return r->cap && r->cap >= r->start && v > r->cap ? r->cap : v;
}

static bool on_rhythm(uint8_t every, int32_t since, int32_t day) {
  return day >= since && (day - since) % every == 0;
}

bool cc_step_on_day(const CcStep *s, int32_t day) {
  return !s->show_every || on_rhythm(s->show_every, s->show_since, day);
}

void cc_steps_bind(const CcStep *pool) { s_pool = pool; }

const CcStep *cc_item_steps(const CcItem *it) { return s_pool ? s_pool + it->step_first : NULL; }

bool cc_item_on_day(const CcItem *it, int32_t day) {
  bool on = it->every_days ? on_rhythm(it->every_days, it->every_since, day)
          : (it->flags & CC_F_ANYTIME) || ((it->days >> cc_weekday(day)) & 1);
  if (!on || it->kind != CC_ROUTINE || !it->step_count || !s_pool) return on;
  uint8_t order[CC_MAX_STEPS];
  return cc_steps_today(it, cc_item_steps(it), day, order) > 0;
}

int cc_steps_today(const CcItem *it, const CcStep steps[], int32_t day, uint8_t order[CC_MAX_STEPS]) {
  int n = 0;
  for (int j = 0; j < it->step_count && j < CC_MAX_STEPS; j++)
    if (cc_step_on_day(&steps[j], day)) order[n++] = (uint8_t)j;
  return n;
}

int cc_pool_layout(CcItem items[], int n) {
  int total = 0;
  for (int i = 0; i < n; i++) {
    if (total + items[i].step_count + items[i].alt_count > CC_POOL_MAX) return -1;
    items[i].step_first = (uint8_t)total;
    total += items[i].step_count + items[i].alt_count;
  }
  return total;
}

bool cc_item_steps_ok(const CcItem *it, const CcStep steps[]) {
  int n = it->step_count, total = n + it->alt_count;
  uint32_t used = 0;
  for (int j = 0; j < total; j++) {
    uint8_t a = steps[j].alt;
    if (a == CC_NO_ALT) continue;
    if (j >= n || a < n || a >= total || (used >> (a - n)) & 1) return false;
    used |= 1u << (a - n);
  }
  return used == (it->alt_count ? (1u << it->alt_count) - 1 : 0);
}
