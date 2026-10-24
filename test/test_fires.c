#include "t.h"
#include "cc_fires.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

static CcItem mk(uint16_t id, uint8_t kind, uint16_t at) {
  CcItem it; memset(&it, 0, sizeof(it));
  it.id = id; it.kind = kind; it.days = 0x7F; it.at_min = at;
  return it;
}

static CcFire fire(cc_sec at, uint16_t id, uint8_t kind, uint8_t prio) {
  CcFire f = { at, id, kind, prio }; return f;
}

void test_fires(void) {
  CcSettings cfg = CC_SETTINGS_DEFAULT;
  CcItem R = mk(1, CC_ROUTINE, 420); R.priority = CC_IMPORTANT;
  CcItem W = mk(2, CC_REMINDER, 540); W.until_min = 1260; W.every_min = 60; W.goal = 8;
  CcItem items[2] = { W, R };  // deliberately out of time order
  CcDay d; memset(&d, 0, sizeof(d));
  d.day = D; d.n = 2; d.s[0].id = 2; d.s[1].id = 1;
  CcFire f[16];

  int n = cc_collect(items, 2, &d, CC_NEVER, CC_NEVER, cc_at(D, 360), &cfg, f, 16);
  T_EQ(n, 3);
  T_EQ(f[0].at, cc_at(D, 410)); T_EQ(f[0].id, 1); T_EQ(f[0].kind, CC_FIRE_HEADSUP);
  T_EQ(f[1].at, cc_at(D, 420)); T_EQ(f[1].id, 1); T_EQ(f[1].kind, CC_FIRE_DUE);
  T_EQ(f[2].at, cc_at(D, 540)); T_EQ(f[2].id, 2); T_EQ(f[2].kind, CC_FIRE_DUE);
  T_EQ(f[1].priority, CC_IMPORTANT);

  T_EQ(cc_collect(items, 2, &d, CC_NEVER, CC_NEVER, cc_at(D, 360), &cfg, f, 2), 2);  // capped: the earliest
  T_EQ(f[1].at, cc_at(D, 420));

  n = cc_collect(items, 2, &d, cc_at(D, 365), CC_NEVER, cc_at(D, 360), &cfg, f, 16);
  T_EQ(n, 4);
  T_EQ(f[0].at, cc_at(D, 365)); T_EQ(f[0].kind, CC_FIRE_STEP_END); T_EQ(f[0].id, 0);
  cc_sec t0 = cc_at(D, 360);
  n = cc_collect(items, 2, &d, t0 + 50, t0 + 40, t0, &cfg, f, 16);
  T_EQ(n, 5);
  T_EQ(f[0].at, t0 + 40); T_EQ(f[0].kind, CC_FIRE_FOCUS_END); T_EQ(f[0].id, 0); T_EQ(f[0].priority, CC_CRITICAL);
  T_EQ(f[1].at, t0 + 50); T_EQ(f[1].kind, CC_FIRE_STEP_END); T_EQ(f[1].id, 0);

  T_EQ(cc_collect(items, 0, &d, CC_NEVER, CC_NEVER, cc_at(D, 360), &cfg, f, 16), 0);  // empty schedule
  cc_sec w[CC_WAKE_MAX];
  T_EQ(cc_plan_wakeups(f, 0, cc_at(D, 360), w, CC_WAKE_MAX), 0);

  d.s[0].count = 8; d.s[1].status = CC_ST_DONE;  // all done, goal met
  n = cc_collect(items, 2, &d, CC_NEVER, CC_NEVER, cc_at(D, 600), &cfg, f, 16);
  T_EQ(n, 3);
  for (int i = 0; i < n; i++) T_EQ(cc_day_of(f[i].at), D + 1);

  // same time: critical first
  CcItem C = mk(3, CC_TASK, 540); C.priority = CC_CRITICAL;
  CcItem two[2] = { W, C };
  d.s[0].count = 0; d.s[1].id = 3; d.s[1].status = CC_ST_NONE;
  n = cc_collect(two, 2, &d, CC_NEVER, CC_NEVER, cc_at(D, 480), &cfg, f, 16);
  T_EQ(n, 3);  // heads-up 08:45, then C and W at 09:00
  T_EQ(f[1].id, 3); T_EQ(f[2].id, 2);

  cc_sec now = cc_at(D, 600);
  CcFire p[12];
  p[0] = fire(now + 10, 1, CC_FIRE_DUE, 0);
  p[1] = fire(now + 300, 2, CC_FIRE_DUE, 0);
  p[2] = fire(now + 320, 3, CC_FIRE_DUE, 0);
  p[3] = fire(now + 420, 4, CC_FIRE_DUE, 0);
  T_EQ(cc_plan_wakeups(p, 4, now, w, CC_WAKE_MAX), 3);
  T_EQ(w[0], now + 30); T_EQ(w[1], now + 300); T_EQ(w[2], now + 420);

  p[0] = fire(now - 50, 1, CC_FIRE_DUE, 0);  // two overdue fires share the clamped wake
  p[1] = fire(now - 10, 2, CC_FIRE_DUE, 0);
  T_EQ(cc_plan_wakeups(p, 2, now, w, CC_WAKE_MAX), 1);
  T_EQ(w[0], now + 30);

  for (int i = 0; i < 12; i++) p[i] = fire(now + 120 * (i + 1), (uint16_t)i, CC_FIRE_DUE, 0);
  T_EQ(cc_plan_wakeups(p, 12, now, w, CC_WAKE_MAX), 8);
  T_EQ(w[0], now + 120); T_EQ(w[7], now + 960);

  p[0] = fire(now - 20, 1, CC_FIRE_DUE, CC_CRITICAL);
  p[1] = fire(now - 40, 2, CC_FIRE_DUE, CC_NORMAL);
  p[2] = fire(now + 20, 3, CC_FIRE_DUE, CC_IMPORTANT);
  p[3] = fire(now + 90, 4, CC_FIRE_DUE, CC_NORMAL);
  CcFire due[8];
  T_EQ(cc_due_now(p, 4, now, due, 8), 3);
  T_EQ(due[0].id, 1); T_EQ(due[1].id, 3); T_EQ(due[2].id, 2);
  T_EQ(cc_due_now(p, 4, now, due, 1), 1);  // capped: the most urgent
  T_EQ(due[0].id, 1);
}
