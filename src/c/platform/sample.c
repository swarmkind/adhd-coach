#include <pebble.h>
#include "sample.h"
#include "store.h"
#include "../config.h"
#include "../core/cc_time.h"
#include "../core/cc_event.h"
#include "../core/cc_steps.h"

static int s_used;  // pool steps so far: an item's steps are added right after it

static CcItem *add(uint16_t id, uint8_t kind, const char *name, uint16_t at, uint8_t days) {
  CcItem *it = &g_items[g_nitems];
  memset(it, 0, sizeof(*it));
  it->id = id; it->kind = kind; it->at_min = at; it->days = days; it->order = (uint8_t)g_nitems;
  it->step_first = (uint8_t)s_used;
  strncpy(it->name, name, CC_NAME_MAX - 1);
  g_nitems++;
  return it;
}

static CcStep *add_step(CcItem *it, uint8_t type, const char *name, uint16_t dur_s) {
  CcStep *s = &g_pool[s_used++];
  it->step_count++;
  memset(s, 0, sizeof(*s));
  s->type = type; s->amount.start = dur_s; s->alt = CC_NO_ALT;
  strncpy(s->name, name, CC_NAME_MAX - 1);
  return s;
}

void sample_load(cc_sec now) {
  int32_t today = cc_day_of(now);
  uint16_t m = cc_min_of(now);
  g_nitems = 0;
  s_used = 0;

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
  // Step-ups: curls at 21 reps (8, +1 a week for 13 weeks) and 10 kg, or shoulder press instead;
  // red light every other day, off today. No item after the routine has steps, so they stay together.
  CcStep *curls = add_step(r, CC_STEP_MANUAL, "Curls", 0);
  curls->flags = CC_SF_QTY | CC_SF_WEIGHT;
  curls->amount = (CcRamp){ 8, 0, today - 91, 1, 7 };
  curls->weight = (CcRamp){ 100, 0, today, 10, 28 };
  curls->num = (CcNumSpec){ 0, 0, 0, 1, "kg" };
  CcStep *light = add_step(r, CC_STEP_TIMED, "Red light", 600);
  light->show_every = 2; light->show_since = today - 1;
  curls->alt = (uint8_t)(r->step_count + r->alt_count);
  CcStep *press = add_step(r, CC_STEP_MANUAL, "Shoulder press", 0);  // the alternative, after the main steps
  r->step_count--; r->alt_count++;
  press->flags = CC_SF_QTY; press->amount.start = 8;
  CcItem *many = add(7, CC_ROUTINE, "Sixteen steps", (uint16_t)((m + 30) % 1440), 0x7F);
  for (int i = 0; i < 16; i++) {
    char name[12];
    snprintf(name, sizeof(name), "Step %d", i + 1);
    add_step(many, CC_STEP_MANUAL, name, 0);
  }
#else
  (void)m;
#endif
  cc_pool_layout(g_items, g_nitems);
  store_save_schedule();
}
