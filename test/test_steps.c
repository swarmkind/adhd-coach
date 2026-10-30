#include "t.h"
#include "cc_steps.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

static void ramps(void) {
  CcRamp q = { 20, 0, D, 1, 7 };
  T_EQ(cc_ramp_at(&q, D - 3), 20); T_EQ(cc_ramp_at(&q, D + 6), 20);
  T_EQ(cc_ramp_at(&q, D + 7), 21); T_EQ(cc_ramp_at(&q, D + 70), 30);
  q.cap = 25; T_EQ(cc_ramp_at(&q, D + 70), 25);
  CcRamp fixed = { 60, 0, D, 0, 0 }; T_EQ(cc_ramp_at(&fixed, D + 400), 60);
  CcRamp kg = { 100, 0, D, 10, 28 }; T_EQ(cc_ramp_at(&kg, D + 28), 110);  // 10.0 kg -> 11.0 kg
}

static void rhythms(void) {
  CcStep s; memset(&s, 0, sizeof(s));
  T_TRUE(cc_step_on_day(&s, D + 5));  // every run
  s.show_every = 3; s.show_since = D;
  T_TRUE(cc_step_on_day(&s, D)); T_TRUE(!cc_step_on_day(&s, D + 1));
  T_TRUE(cc_step_on_day(&s, D + 6)); T_TRUE(!cc_step_on_day(&s, D - 3));  // nothing before the start

  CcItem t; memset(&t, 0, sizeof(t));
  t.kind = CC_TASK; t.days = 0x3E;  // Mon-Fri; D is a Friday
  T_TRUE(cc_item_on_day(&t, D)); T_TRUE(!cc_item_on_day(&t, D + 1));
  t.every_days = 2; t.every_since = D;  // the rhythm replaces the weekdays
  T_TRUE(cc_item_on_day(&t, D + 2)); T_TRUE(!cc_item_on_day(&t, D + 3));
  t.every_days = 0; t.flags = CC_F_ANYTIME; T_TRUE(cc_item_on_day(&t, D + 1));
  t.every_days = 3; T_TRUE(!cc_item_on_day(&t, D + 1)); T_TRUE(cc_item_on_day(&t, D + 3));

  // A routine whose steps are all off today is off today, once the pool is bound.
  CcStep pool[4]; memset(pool, 0, sizeof(pool));
  pool[0].show_every = 2; pool[0].show_since = D + 1;
  pool[1].show_every = 2; pool[1].show_since = D + 1;
  CcItem r; memset(&r, 0, sizeof(r));
  r.kind = CC_ROUTINE; r.days = 0x7F; r.step_count = 2;
  T_TRUE(cc_item_on_day(&r, D));  // unbound: weekdays only
  cc_steps_bind(pool);
  T_TRUE(cc_item_steps(&r) == pool);
  T_TRUE(!cc_item_on_day(&r, D)); T_TRUE(cc_item_on_day(&r, D + 1));
  cc_steps_bind(NULL);
  T_TRUE(cc_item_steps(&r) == NULL);
}

static void today_and_layout(void) {
  CcStep four[4]; memset(four, 0, sizeof(four));
  four[1].show_every = 2; four[1].show_since = D;
  four[3].show_every = 3; four[3].show_since = D;
  CcItem r4; memset(&r4, 0, sizeof(r4));
  r4.kind = CC_ROUTINE; r4.step_count = 4;
  uint8_t order[CC_MAX_STEPS];
  T_EQ(cc_steps_today(&r4, four, D, order), 4);
  T_EQ(cc_steps_today(&r4, four, D + 1, order), 2); T_EQ(order[0], 0); T_EQ(order[1], 2);
  T_EQ(cc_steps_today(&r4, four, D + 2, order), 3); T_EQ(order[1], 1);

  CcItem L[3]; memset(L, 0, sizeof(L));
  L[0].step_count = 2; L[0].alt_count = 1; L[2].step_count = 3;
  T_EQ(cc_pool_layout(L, 3), 6); T_EQ(L[0].step_first, 0); T_EQ(L[1].step_first, 3); T_EQ(L[2].step_first, 3);
  CcItem big[13]; memset(big, 0, sizeof(big));
  for (int i = 0; i < 13; i++) big[i].step_count = 20;  // 260 > 240
  T_EQ(cc_pool_layout(big, 13), -1);

  CcStep a[4]; memset(a, 0, sizeof(a));
  CcItem ai; memset(&ai, 0, sizeof(ai));
  ai.step_count = 2; ai.alt_count = 1;
  a[0].alt = 2; a[1].alt = CC_NO_ALT; a[2].alt = CC_NO_ALT; T_TRUE(cc_item_steps_ok(&ai, a));
  a[1].alt = 2; T_TRUE(!cc_item_steps_ok(&ai, a));                        // two mains share one alternative
  a[1].alt = CC_NO_ALT; a[0].alt = 1; T_TRUE(!cc_item_steps_ok(&ai, a));  // points at a main step
  a[0].alt = 2; a[2].alt = 0; T_TRUE(!cc_item_steps_ok(&ai, a));          // an alternative with its own
  a[2].alt = CC_NO_ALT; a[3].alt = CC_NO_ALT; ai.alt_count = 2; T_TRUE(!cc_item_steps_ok(&ai, a));  // an alternative nobody uses
}

void test_steps(void) {
  ramps();
  rhythms();
  today_and_layout();
}
