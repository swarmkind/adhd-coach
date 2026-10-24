#include <pebble.h>
#include "sample.h"
#include "store.h"
#include "../config.h"
#include "../core/cc_time.h"
#include "../core/cc_event.h"

static CcItem *add(uint16_t id, uint8_t kind, const char *name, uint16_t at, uint8_t days) {
  CcItem *it = &g_items[g_nitems];
  memset(it, 0, sizeof(*it));
  it->id = id; it->kind = kind; it->at_min = at; it->days = days; it->order = (uint8_t)g_nitems;
  strncpy(it->name, name, CC_NAME_MAX - 1);
  g_nitems++;
  return it;
}

static CcStep *add_step(CcItem *it, uint8_t type, const char *name, uint16_t dur_s) {
  CcStep *s = &g_steps[it - g_items][it->step_count++];
  memset(s, 0, sizeof(*s));
  s->type = type; s->dur_s = dur_s;
  strncpy(s->name, name, CC_NAME_MAX - 1);
  return s;
}

void sample_load(cc_sec now) {
  int32_t today = cc_day_of(now);
  uint16_t m = cc_min_of(now);
  g_nitems = 0;

  CcItem *r = add(1, CC_ROUTINE, "Morning routine", 420, 0x3E);  // 07:00 Mon-Fri
  add_step(r, CC_STEP_MANUAL, "Drink a glass of water", 0);
  add_step(r, CC_STEP_TIMED, "Plank", 60);
  CcStep *w = add_step(r, CC_STEP_NUMBER, "Weight", 0);
  w->num = (CcNumSpec){ 1, 300, 2500, 1, "kg" };
  add_step(r, CC_STEP_TIMED, "Stretch hamstrings and hips", 120);

  CcItem *water = add(2, CC_REMINDER, "Drink water", 540, 0x7F);
  water->until_min = 1260; water->every_min = 60; water->goal = 8;

  CcItem *push = add(3, CC_REMINDER, "10 pushups", 600, 0x7F);
  push->until_min = 1200; push->every_min = 120; push->flags = CC_F_ASK_NUMBER;
  push->num = (CcNumSpec){ 1, 0, 500, 0, "reps" };

  CcItem *dentist = add(4, CC_TASK, "Call the dentist", 840, (uint8_t)(1 << cc_weekday(today)));
  dentist->priority = CC_IMPORTANT;

  CcItem *tidy = add(5, CC_TASK, "Tidy desk", 0, 0x7F);
  tidy->flags = CC_F_ANYTIME;

  CcItem *checkup = add(6, CC_EVENT, "Dentist check-up", 630, 0);
  checkup->event_day = today + 8;
  checkup->ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_NORMAL, checkup->ladder);

#if COACH_DEMO
  r->at_min = (uint16_t)((m + 3) % 1440); r->days = 0x7F;
  dentist->at_min = (uint16_t)((m + 6) % 1440); dentist->days = 0x7F;
  cc_sec ev = now + 69 * 60;  // its hour-before rung lands at now + 9 min
  checkup->event_day = cc_day_of(ev); checkup->at_min = cc_min_of(ev);
  checkup->ladder[0] = CC_RUNG_HOUR; checkup->ladder_count = 1;
#else
  (void)m;
#endif
  store_save_schedule();
}
