#include "t.h"
#include "cc_state.h"
#include "cc_sched.h"
#include "cc_time.h"
#include "cc_event.h"
#include "cc_fires.h"

#define D 20749  // Friday 2026-10-23

static CcItem mk(uint16_t id, uint8_t kind, uint16_t at) {
  CcItem it; memset(&it, 0, sizeof(it));
  it.id = id; it.kind = kind; it.days = 0x7F; it.at_min = at;
  return it;
}

static CcItem water(void) {
  CcItem w = mk(2, CC_REMINDER, 540);
  w.until_min = 1260; w.every_min = 60; w.goal = 8;
  return w;
}

static CcItemState st(uint16_t id) {
  CcItemState s; memset(&s, 0, sizeof(s)); s.id = id; return s;
}

static void day_of(CcDay *d, int32_t day, const CcItem items[], int n) {
  memset(d, 0, sizeof(*d));
  d->day = day; d->n = (uint8_t)n;
  for (int i = 0; i < n; i++) d->s[i].id = items[i].id;
}

void test_state(void) {
  CcSettings cfg = CC_SETTINGS_DEFAULT;
  CcItem T = mk(1, CC_TASK, 420), W = water();

  CcItemState a = st(1);
  cc_apply(&a, &T, CC_A_LATER, cc_at(D, 420), 0, &cfg);
  T_EQ(a.later_until, cc_at(D, 450));
  T_EQ(a.status, CC_ST_NONE);

  CcItemState a60 = st(1);  // More > Later 60 min
  cc_apply(&a60, &T, CC_A_LATER_LONG, cc_at(D, 420), 0, &cfg);
  T_EQ(a60.later_until, cc_at(D, 480));
  CcItemState w60 = st(2);
  cc_apply(&w60, &W, CC_A_LATER_LONG, cc_at(D, 600), cc_at(D, 600), &cfg);
  T_EQ(w60.later_until, cc_at(D, 660));

  CcItemState b = st(1);
  cc_apply(&b, &T, CC_A_IGNORED, cc_at(D, 421), 0, &cfg);
  T_EQ(b.later_until, cc_at(D, 451));
  T_EQ(b.auto_later_used, 1);
  T_EQ(b.status, CC_ST_NONE);
  cc_apply(&b, &T, CC_A_IGNORED, cc_at(D, 452), 0, &cfg);
  T_EQ(b.status, CC_ST_MISSED);
  T_EQ(b.later_until, 0);

  CcItemState w1 = st(2); w1.later_until = cc_at(D, 610);
  cc_apply(&w1, &W, CC_A_IGNORED, cc_at(D, 611), cc_at(D, 600), &cfg);
  T_EQ(w1.missed, 1);
  T_EQ(w1.handled_until, cc_at(D, 600));
  T_EQ(w1.later_until, 0);
  T_EQ(w1.status, CC_ST_NONE);

  CcItemState w2 = st(2); w2.later_until = cc_at(D, 610);
  cc_apply(&w2, &W, CC_A_DONE, cc_at(D, 612), cc_at(D, 600), &cfg);
  T_EQ(w2.count, 1);
  T_EQ(w2.handled_until, cc_at(D, 600));
  T_EQ(w2.later_until, 0);
  T_EQ(w2.status, CC_ST_NONE);

  CcItemState w3 = st(2); w3.handled_until = cc_at(D, 540);
  cc_apply(&w3, &W, CC_A_LATER, cc_at(D, 600), cc_at(D, 600), &cfg);
  T_EQ(w3.later_until, cc_at(D, 610));
  T_EQ(w3.handled_until, cc_at(D, 540));

  CcItemState w4 = st(2);
  cc_apply(&w4, &W, CC_A_SKIP, cc_at(D, 601), cc_at(D, 600), &cfg);  // skip this one
  T_EQ(w4.handled_until, cc_at(D, 600));
  T_EQ(w4.count, 0);
  T_EQ(w4.missed, 0);
  T_EQ(w4.status, CC_ST_NONE);

  CcItemState c = st(1);
  cc_apply(&c, &T, CC_A_DONE, cc_at(D, 430), 0, &cfg);      T_EQ(c.status, CC_ST_DONE);
  c = st(1); cc_apply(&c, &T, CC_A_SKIP, cc_at(D, 430), 0, &cfg);  T_EQ(c.status, CC_ST_SKIPPED);
  c = st(1); cc_apply(&c, &T, CC_A_START, cc_at(D, 430), 0, &cfg); T_EQ(c.status, CC_ST_ACTIVE);
  c = st(1); cc_apply(&c, &T, CC_A_PAUSE, cc_at(D, 430), 0, &cfg); T_EQ(c.status, CC_ST_PAUSED);
  // Resume undoes a pause and nothing else
  c = st(1); cc_apply(&c, &T, CC_A_PAUSE, cc_at(D, 430), 0, &cfg); cc_apply(&c, &T, CC_A_RESUME, cc_at(D, 440), 0, &cfg);
  T_EQ(c.status, CC_ST_NONE);
  c = st(1); cc_apply(&c, &T, CC_A_DONE, cc_at(D, 430), 0, &cfg); cc_apply(&c, &T, CC_A_RESUME, cc_at(D, 440), 0, &cfg);
  T_EQ(c.status, CC_ST_DONE);
  // a resumed reminder picks up at the next slot; the slots it slept through are not missed
  CcItemState p = st(2);
  cc_apply(&p, &W, CC_A_PAUSE, cc_at(D, 840), 0, &cfg);   // paused 14:00
  cc_apply(&p, &W, CC_A_RESUME, cc_at(D, 965), 0, &cfg);  // back 16:05
  T_EQ(p.status, CC_ST_NONE);
  T_EQ(p.handled_until, cc_at(D, 960));
  T_EQ(cc_next_due(&W, &p, cc_at(D, 965), &cfg), cc_at(D, 1020));  // 17:00
  CcDay pd; day_of(&pd, D, &W, 1); pd.s[0] = p;
  cc_sweep(&pd, &W, 1, cc_at(D, 965), &cfg);
  T_EQ(pd.s[0].missed, 0);
  p = st(2);  // resumed before the first slot: nothing handled yet
  cc_apply(&p, &W, CC_A_PAUSE, cc_at(D, 480), 0, &cfg);
  cc_apply(&p, &W, CC_A_RESUME, cc_at(D, 500), 0, &cfg);
  T_EQ(p.handled_until, 0);
  T_EQ(cc_next_due(&W, &p, cc_at(D, 500), &cfg), cc_at(D, 540));

  c = st(1); cc_apply(&c, &T, CC_A_HEADSUP_SEEN, cc_at(D, 410), 0, &cfg);
  T_TRUE(c.flags & CC_SF_HEADSUP_SHOWN);
  T_EQ(c.status, CC_ST_NONE);

  // state lookup
  CcItem items[2] = { T, W };
  CcDay d;
  day_of(&d, D, items, 2);
  T_TRUE(cc_state_for(&d, 2) == &d.s[1]);
  T_TRUE(cc_state_for(&d, 9) == NULL);

  // sweep
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 431), &cfg), 1);  // 07:11: the 07:00 task is missed
  T_EQ(d.s[0].status, CC_ST_MISSED);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 431), &cfg), 0);  // nothing more to do

  day_of(&d, D, items, 2);
  d.s[0].status = CC_ST_DONE;
  d.s[1].handled_until = cc_at(D, 540);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 725), &cfg), 1);  // 12:05
  T_EQ(d.s[1].missed, 2);                                 // 10:00 and 11:00
  T_EQ(d.s[1].handled_until, cc_at(D, 660));              // 12:00 untouched
  T_EQ(d.s[0].status, CC_ST_DONE);

  uint8_t keep[3] = { CC_ST_ACTIVE, CC_ST_PAUSED, CC_ST_DONE };
  for (int i = 0; i < 3; i++) {
    day_of(&d, D, items, 2);
    d.s[0].status = keep[i]; d.s[1].status = keep[i];
    T_EQ(cc_sweep(&d, items, 2, cc_at(D, 725), &cfg), 0);
    T_EQ(d.s[0].status, keep[i]);
    T_EQ(d.s[1].missed, 0);
  }

  day_of(&d, D, items, 2);  // a pending reminder Later covers its slot
  d.s[0].status = CC_ST_DONE;
  d.s[1].handled_until = cc_at(D, 540); d.s[1].later_until = cc_at(D, 610);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 612), &cfg), 0);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 725), &cfg), 1);  // later stale by 12:05
  T_EQ(d.s[1].missed, 2);
  T_EQ(d.s[1].later_until, 0);

  day_of(&d, D, items, 2);  // a pending task Later is not missed yet; a stale one is
  d.s[1].status = CC_ST_DONE;
  d.s[0].later_until = cc_at(D, 450);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 455), &cfg), 0);
  T_EQ(cc_sweep(&d, items, 2, cc_at(D, 461), &cfg), 1);
  T_EQ(d.s[0].status, CC_ST_MISSED);

  CcItem S = T; S.flags = CC_F_SILENT;  // silent and anytime items are never missed
  CcItem A = T; A.flags = CC_F_ANYTIME;
  CcItem quiet[2] = { S, A }; quiet[1].id = 3;
  day_of(&d, D, quiet, 2);
  T_EQ(cc_sweep(&d, quiet, 2, cc_at(D, 725), &cfg), 0);

  // rollover
  day_of(&d, D, items, 2);
  d.s[0].status = CC_ST_DONE;
  d.s[1].count = 5; d.s[1].missed = 2; d.s[1].handled_until = cc_at(D, 1200);
  CcOutcome out[CC_MAX_ITEMS];
  T_EQ(cc_rollover(&d, items, 2, D + 1, out, CC_MAX_ITEMS), 2);
  T_EQ(out[0].id, 1); T_EQ(out[0].status, CC_ST_DONE); T_EQ(out[0].count, 0); T_EQ(out[0].missed, 0);
  T_EQ(out[1].id, 2); T_EQ(out[1].status, CC_ST_NONE); T_EQ(out[1].count, 5); T_EQ(out[1].missed, 2);
  T_EQ(d.day, D + 1);
  T_EQ(d.n, 2);
  T_EQ(d.s[0].id, 1); T_EQ(d.s[0].status, CC_ST_NONE);
  T_EQ(d.s[1].id, 2); T_EQ(d.s[1].count, 0); T_EQ(d.s[1].missed, 0); T_EQ(d.s[1].handled_until, 0);

  d.s[1].count = 3;
  T_EQ(cc_rollover(&d, items, 2, D + 1, out, CC_MAX_ITEMS), 0);  // same day: nothing changes
  T_EQ(d.s[1].count, 3);

  CcDay blank; memset(&blank, 0, sizeof(blank));  // first launch: no old day to report
  T_EQ(cc_rollover(&blank, items, 2, D, out, CC_MAX_ITEMS), 0);
  T_EQ(blank.day, D);
  T_EQ(blank.n, 2);

  // events: Later goes to "later today", clamped to 10 min before the event
  CcItem E = mk(9, CC_EVENT, 630); E.days = 0; E.event_day = D + 8;
  E.ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_NORMAL, E.ladder);
  CcItemState e1 = st(9);
  cc_apply(&e1, &E, CC_A_LATER, cc_at(D + 1, 541), cc_at(D + 1, 540), &cfg);
  T_EQ(e1.later_until, cc_at(D + 1, 661));
  CcItemState e60 = st(9);
  cc_apply(&e60, &E, CC_A_LATER_LONG, cc_at(D + 1, 541), cc_at(D + 1, 540), &cfg);
  T_EQ(e60.later_until, cc_at(D + 1, 601));
  const int8_t hour[] = { CC_RUNG_HOUR };
  CcItem H = E; H.event_day = D; H.ladder[0] = hour[0]; H.ladder_count = 1;  // today 10:30, rung 09:30
  CcItemState h1 = st(9);
  cc_apply(&h1, &H, CC_A_LATER, cc_at(D, 571), cc_at(D, 570), &cfg);
  T_EQ(h1.later_until, cc_at(D, 620));                                    // 10:20, not 11:31
  CcItemState h2 = st(9);
  cc_apply(&h2, &H, CC_A_LATER, cc_at(D, 625), cc_at(D, 570), &cfg);
  T_EQ(h2.later_until, 0);                                                // no room left: handled
  T_EQ(h2.handled_until, cc_at(D, 570));
  CcItemState e2 = st(9);
  cc_apply(&e2, &E, CC_A_IGNORED, cc_at(D + 1, 541), cc_at(D + 1, 540), &cfg);
  T_EQ(e2.later_until, cc_at(D + 1, 661)); T_EQ(e2.auto_later_used, 1); T_EQ(e2.missed, 0);
  cc_apply(&e2, &E, CC_A_IGNORED, cc_at(D + 1, 662), cc_at(D + 1, 540), &cfg);
  T_EQ(e2.missed, 1); T_EQ(e2.auto_later_used, 0); T_EQ(e2.later_until, 0);
  T_EQ(e2.handled_until, cc_at(D + 1, 540)); T_EQ(e2.status, CC_ST_NONE);
  CcItemState e3 = st(9);
  cc_apply(&e3, &E, CC_A_DONE, cc_at(D + 1, 541), cc_at(D + 1, 540), &cfg);
  T_EQ(e3.handled_until, cc_at(D + 1, 540)); T_EQ(e3.status, CC_ST_NONE);

  // sweep: the D+1 09:00 rung, unanswered by 10:00, counts as missed once
  CcDay ed; day_of(&ed, D + 1, &E, 1);
  T_EQ(cc_sweep(&ed, &E, 1, cc_at(D + 1, 600), &cfg), 1);
  T_EQ(ed.s[0].missed, 1); T_EQ(ed.s[0].handled_until, cc_at(D + 1, 540));
  T_EQ(cc_sweep(&ed, &E, 1, cc_at(D + 1, 610), &cfg), 0);

  // Quiet Time holds reminder alerts only
  CcItem Rt = mk(3, CC_ROUTINE, 420), Ta = mk(1, CC_TASK, 420);
  T_TRUE(cc_quiet_holds(&W, CC_FIRE_DUE));
  T_TRUE(!cc_quiet_holds(&Ta, CC_FIRE_DUE));
  T_TRUE(!cc_quiet_holds(&Rt, CC_FIRE_DUE));
  T_TRUE(!cc_quiet_holds(&E, CC_FIRE_DUE));
  T_TRUE(!cc_quiet_holds(&Ta, CC_FIRE_HEADSUP));
  T_TRUE(!cc_quiet_holds(&W, CC_FIRE_STEP_END));
  T_TRUE(!cc_quiet_holds(&W, CC_FIRE_FOCUS_END));
  // a held slot is skipped, so the sweep never calls it missed
  CcDay qd; day_of(&qd, D, &W, 1);
  cc_apply(&qd.s[0], &W, CC_A_SKIP, cc_at(D, 600), cc_at(D, 600), &cfg);
  T_EQ(cc_sweep(&qd, &W, 1, cc_at(D, 660), &cfg), 0);
  T_EQ(qd.s[0].missed, 0); T_EQ(qd.s[0].count, 0);
  T_EQ(cc_next_due(&W, &qd.s[0], cc_at(D, 660), &cfg), cc_at(D, 660));

  // an answered auto-Later frees the next rung's own auto-Later (Persistent, event today 14:30)
  CcItem P = mk(10, CC_EVENT, 870); P.days = 0; P.event_day = D;
  P.ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_PERSISTENT, P.ladder);  // today: 09:00, 13:30
  CcItemState p1 = st(10);
  cc_apply(&p1, &P, CC_A_IGNORED, cc_at(D, 541), cc_at(D, 540), &cfg);      // auto-Later to 11:01
  T_EQ(p1.auto_later_used, 1);
  cc_apply(&p1, &P, CC_A_DONE, cc_at(D, 661), cc_at(D, 540), &cfg);         // got it at 11:01
  T_EQ(p1.auto_later_used, 0);
  T_EQ(cc_next_due(&P, &p1, cc_at(D, 700), &cfg), cc_at(D, 810));
  cc_apply(&p1, &P, CC_A_IGNORED, cc_at(D, 811), cc_at(D, 810), &cfg);      // the 13:30 rung gets one too
  T_EQ(p1.later_until, cc_at(D, 860)); T_EQ(p1.missed, 0);
  CcItemState p2 = st(10);  // a Later with no room left also frees it
  p2.auto_later_used = 1; p2.later_until = cc_at(D, 860);
  cc_apply(&p2, &P, CC_A_LATER, cc_at(D, 861), cc_at(D, 810), &cfg);
  T_EQ(p2.later_until, 0); T_EQ(p2.auto_later_used, 0);
  CcItemState p3 = st(10); p3.auto_later_used = 1;
  cc_apply(&p3, &P, CC_A_SKIP, cc_at(D, 661), cc_at(D, 540), &cfg);
  T_EQ(p3.auto_later_used, 0);
}
