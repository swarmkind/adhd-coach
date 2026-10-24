#include "t.h"
#include "cc_sched.h"
#include "cc_time.h"
#include "cc_event.h"

#define D 20749  // Friday 2026-10-23

static CcItem routine(void) {
  CcItem r; memset(&r, 0, sizeof(r));
  r.id = 1; r.kind = CC_ROUTINE; r.days = 0x3E; r.at_min = 420;  // Mon-Fri 07:00
  strcpy(r.name, "Morning routine");
  return r;
}

static CcItem water(void) {
  CcItem w; memset(&w, 0, sizeof(w));
  w.id = 2; w.kind = CC_REMINDER; w.days = 0x7F;
  w.at_min = 540; w.until_min = 1260; w.every_min = 60; w.goal = 8;  // 09:00-21:00 hourly
  strcpy(w.name, "Drink water");
  return w;
}

static CcItemState st(uint16_t id) {
  CcItemState s; memset(&s, 0, sizeof(s)); s.id = id; return s;
}

void test_sched(void) {
  CcSettings cfg = CC_SETTINGS_DEFAULT;
  CcItem R = routine(), W = water();
  CcItemState fresh = st(1);

  T_EQ(cc_next_due(&R, &fresh, cc_at(D, 360), &cfg), cc_at(D, 420));      // 06:00 -> today 07:00
  T_EQ(cc_next_due(&R, &fresh, cc_at(D, 425), &cfg), cc_at(D, 420));      // 5 min late -> still today
  T_EQ(cc_next_due(&R, &fresh, cc_at(D, 431), &cfg), cc_at(D + 3, 420));  // 11 min late -> Monday
  R.grace_min = 30;
  T_EQ(cc_next_due(&R, &fresh, cc_at(D, 445), &cfg), cc_at(D, 420));      // grace widens catch-up
  R.grace_min = 0;

  CcItemState done = st(1); done.status = CC_ST_DONE;
  T_EQ(cc_next_due(&R, &done, cc_at(D, 390), &cfg), cc_at(D + 3, 420));   // done today
  CcItemState paused = st(1); paused.status = CC_ST_PAUSED;
  T_EQ(cc_next_due(&R, &paused, cc_at(D, 390), &cfg), cc_at(D + 3, 420)); // paused: no alert today
  CcItemState later0730 = st(1); later0730.later_until = cc_at(D, 450);
  T_EQ(cc_next_due(&R, &later0730, cc_at(D, 440), &cfg), cc_at(D, 450));  // Later until 07:30

  CcItemState wf = st(2);
  T_EQ(cc_next_due(&W, &wf, cc_at(D, 480), &cfg), cc_at(D, 540));
  CcItemState h0900 = st(2); h0900.handled_until = cc_at(D, 540);
  T_EQ(cc_next_due(&W, &h0900, cc_at(D, 570), &cfg), cc_at(D, 600));
  CcItemState count8 = st(2); count8.count = 8; count8.handled_until = cc_at(D, 900);
  T_EQ(cc_next_due(&W, &count8, cc_at(D, 930), &cfg), cc_at(D + 1, 540)); // goal met
  T_EQ(cc_next_due(&W, &wf, cc_at(D, 1290), &cfg), cc_at(D + 1, 540));    // after window
  // A Later on the 10:00 slot leaves handled_until alone; the Later covers the slot.
  CcItemState l1010 = st(2); l1010.handled_until = cc_at(D, 540); l1010.later_until = cc_at(D, 610);
  T_EQ(cc_next_due(&W, &l1010, cc_at(D, 605), &cfg), cc_at(D, 610));
  T_EQ(cc_occurrence_for(&W, &l1010, cc_at(D, 610), &cfg), cc_at(D, 600));
  T_EQ(cc_occurrence_for(&W, &h0900, cc_at(D, 600), &cfg), cc_at(D, 600));
  W.until_min = 1250;  // last slot is 20:00
  CcItemState h2000 = st(2); h2000.handled_until = cc_at(D, 1200);
  T_EQ(cc_next_due(&W, &h2000, cc_at(D, 1210), &cfg), cc_at(D + 1, 540));
  W.until_min = 1260;

  CcItem S = R; S.flags = CC_F_SILENT;
  T_EQ(cc_next_due(&S, &fresh, cc_at(D, 360), &cfg), CC_NEVER);
  CcItem A = R; A.kind = CC_TASK; A.flags = CC_F_ANYTIME;
  T_EQ(cc_next_due(&A, &fresh, cc_at(D, 360), &cfg), CC_NEVER);

  // heads-up: a Friday-only task at 14:00
  CcItem T = R; T.id = 3; T.kind = CC_TASK; T.days = 0x20; T.at_min = 840;
  CcItemState tf = st(3);
  T.priority = CC_IMPORTANT;
  T_EQ(cc_next_headsup(&T, &tf, cc_at(D, 780), &cfg), cc_at(D, 830));      // 13:50
  T.priority = CC_CRITICAL;
  T_EQ(cc_next_headsup(&T, &tf, cc_at(D, 780), &cfg), cc_at(D, 825));      // 13:45
  T.priority = CC_NORMAL;
  T_EQ(cc_next_headsup(&T, &tf, cc_at(D, 780), &cfg), CC_NEVER);
  T.priority = CC_IMPORTANT;
  CcItemState shown = st(3); shown.flags = CC_SF_HEADSUP_SHOWN;
  T_EQ(cc_next_headsup(&T, &shown, cc_at(D, 780), &cfg), CC_NEVER);        // once
  T_EQ(cc_next_headsup(&T, &tf, cc_at(D, 835), &cfg), cc_at(D, 830));       // launched late, still before 14:00
  T_EQ(cc_next_headsup(&T, &tf, cc_at(D, 841), &cfg), CC_NEVER);            // past the task itself
  CcItemState tl = st(3); tl.later_until = cc_at(D, 870);
  T_EQ(cc_next_headsup(&T, &tl, cc_at(D, 845), &cfg), CC_NEVER);           // a Later needs no heads-up
  CcItemState tdone = st(3); tdone.status = CC_ST_DONE; tdone.flags = CC_SF_HEADSUP_SHOWN;
  T_EQ(cc_next_headsup(&T, &tdone, cc_at(D, 900), &cfg), cc_at(D + 7, 830)); // done: next week's

  // a daily 00:05 task: its heads-up falls on the day before
  CcItem M = T; M.days = 0x7F; M.at_min = 5;
  T_EQ(cc_next_headsup(&M, &tf, cc_at(D, 1432), &cfg), cc_at(D, 1435));    // 23:55 today
  T_EQ(cc_next_headsup(&M, &shown, cc_at(D, 1436), &cfg), CC_NEVER);       // shown, not again
  T_EQ(cc_next_headsup(&M, &tf, cc_at(D + 1, 1), &cfg), CC_NEVER);         // after rollover: belonged to yesterday

  // events: Normal ladder, E = D+8 10:30, rungs D+1 / D+7 / D+8 at 09:00
  CcItem E; memset(&E, 0, sizeof(E));
  E.id = 9; E.kind = CC_EVENT; E.event_day = D + 8; E.at_min = 630;
  E.ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_NORMAL, E.ladder);
  CcItemState ef = st(9);
  T_EQ(cc_next_due(&E, NULL, cc_at(D, 720), &cfg), cc_at(D + 1, 540));        // no 7-day limit needed
  T_EQ(cc_next_due(&E, &ef, cc_at(D + 1, 545), &cfg), cc_at(D + 1, 540));     // inside catch-up
  T_EQ(cc_next_due(&E, &ef, cc_at(D + 1, 570), &cfg), cc_at(D + 7, 540));     // missed rung never bursts
  CcItemState eh = st(9); eh.handled_until = cc_at(D + 1, 540);
  T_EQ(cc_next_due(&E, &eh, cc_at(D + 1, 542), &cfg), cc_at(D + 7, 540));
  CcItemState el = st(9); el.later_until = cc_at(D + 1, 660);
  T_EQ(cc_next_due(&E, &el, cc_at(D + 1, 545), &cfg), cc_at(D + 1, 660));     // Later covers its rung
  T_EQ(cc_occurrence_for(&E, &el, cc_at(D + 1, 660), &cfg), cc_at(D + 1, 540));
  T_EQ(cc_next_due(&E, &ef, cc_at(D + 8, 660), &cfg), CC_NEVER);              // every rung has passed
  CcItemState edone = st(9); edone.status = CC_ST_DONE;
  T_EQ(cc_next_due(&E, &edone, cc_at(D + 1, 500), &cfg), cc_at(D + 7, 540));  // status silences today only
  CcItem ES = E; ES.flags = CC_F_SILENT;
  T_EQ(cc_next_due(&ES, &ef, cc_at(D, 720), &cfg), CC_NEVER);
  CcItem EP = E; EP.event_day = D + 30;
  EP.ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_PERSISTENT, EP.ladder);
  T_EQ(cc_next_due(&EP, &ef, cc_at(D, 720), &cfg), cc_at(D + 9, 540));        // three weeks before
  T_EQ(cc_next_headsup(&E, &ef, cc_at(D, 720), &cfg), CC_NEVER);              // events have no heads-up
}
