#include "t.h"
#include "cc_run.h"
#include "cc_time.h"
#include "cc_codec.h"
#include "cc_steps.h"

#define D 20749  // Friday 2026-10-23

static CcStep step(uint8_t type, const char *name, uint16_t dur) {
  CcStep s; memset(&s, 0, sizeof(s));
  s.type = type; s.amount.start = dur; s.alt = CC_NO_ALT; strcpy(s.name, name);
  return s;
}

void test_run(void) {
  CcStep steps[3] = {
    step(CC_STEP_MANUAL, "Pushups", 0),
    step(CC_STEP_TIMED, "Plank", 60),
    step(CC_STEP_NUMBER, "Weight", 0),
  };
  steps[2].num = (CcNumSpec){ 1, 300, 2500, 1, "kg" };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.step_count = 3;

  cc_sec t = cc_at(D, 420);
  CcRun r;
  cc_run_start(&r, &R, steps, t);
  T_EQ(r.step, 0); T_EQ(r.active, 1); T_EQ(r.day, D); T_EQ(r.item_id, 7); T_EQ(r.step_count, 3);
  T_EQ(cc_run_remaining(&r, t), 0);  // untimed
  T_EQ(cc_run_tick(&r, steps, t + 500), CC_RUN_NONE);  // manual steps never auto-advance

  // A timed step waits for Start, showing its full length.
  T_EQ(cc_run_next(&r, steps, t), CC_RUN_NONE);
  T_EQ(r.step, 1); T_TRUE(cc_run_waiting(&r));
  T_EQ(cc_run_remaining(&r, t + 15), 60);
  T_EQ(cc_run_tick(&r, steps, t + 500), CC_RUN_NONE);  // no clock until Start
  cc_run_toggle_pause(&r, t + 10);  // Start
  T_TRUE(!cc_run_waiting(&r)); T_EQ(r.paused, 0); T_EQ(r.step_end, t + 70);
  T_EQ(cc_run_remaining(&r, t + 25), 45);
  T_TRUE(!cc_run_undo(&r, steps, t + 11));  // nothing auto-advanced yet

  T_EQ(cc_run_tick(&r, steps, t + 69), CC_RUN_NONE);
  T_EQ(cc_run_tick(&r, steps, t + 70), CC_RUN_TIME_UP);
  T_EQ(r.step, 2); T_EQ(r.advanced_at, t + 70);
  T_EQ(cc_run_tick(&r, steps, t + 71), CC_RUN_NONE);

  T_TRUE(cc_run_undo(&r, steps, t + 74));  // back to the timed step, full length, waiting again
  T_EQ(r.step, 1); T_TRUE(cc_run_waiting(&r)); T_EQ(cc_run_remaining(&r, t + 90), 60);
  T_TRUE(!cc_run_undo(&r, steps, t + 75));  // one undo per advance
  cc_run_toggle_pause(&r, t + 80);
  T_EQ(cc_run_tick(&r, steps, t + 140), CC_RUN_TIME_UP);
  T_TRUE(!cc_run_undo(&r, steps, t + 146));  // more than 5 s later
  T_EQ(r.step, 2);

  T_EQ(cc_run_next(&r, steps, t + 140), CC_RUN_FINISHED);
  T_EQ(r.active, 0);
  T_EQ(cc_run_next(&r, steps, t + 141), CC_RUN_NONE);  // finished runs stay finished

  // pause on a fresh timed step started at s
  cc_run_start(&r, &R, steps, t);
  cc_sec s = t + 1000;
  cc_run_next(&r, steps, s);
  cc_run_toggle_pause(&r, s);       // Start
  cc_run_toggle_pause(&r, s + 20);  // Stop
  T_EQ(r.paused, 1); T_EQ(r.paused_left, 40);
  T_TRUE(!cc_run_waiting(&r));      // stopped part way, not waiting to start
  T_EQ(cc_run_remaining(&r, s + 50), 40);
  T_EQ(cc_run_tick(&r, steps, s + 70), CC_RUN_NONE);  // the clock is stopped
  cc_run_toggle_pause(&r, s + 100);
  T_EQ(r.paused, 0); T_EQ(r.step_end, s + 140);

  // a timed last step finishes the run on its own
  CcStep two[2] = { step(CC_STEP_MANUAL, "Water", 0), step(CC_STEP_TIMED, "Stretch", 120) };
  R.step_count = 2;
  cc_run_start(&r, &R, two, t);
  cc_run_next(&r, two, t);
  cc_run_toggle_pause(&r, t);
  T_EQ(cc_run_tick(&r, two, t + 120), CC_RUN_FINISHED);
  T_EQ(r.active, 0);

  // a routine whose first step is timed waits for Start too
  CcStep first[1] = { step(CC_STEP_TIMED, "Plank", 60) };
  R.step_count = 1;
  cc_run_start(&r, &R, first, t);
  T_TRUE(cc_run_waiting(&r));
  T_EQ(cc_run_tick(&r, first, t + 60), CC_RUN_NONE);

  T_TRUE(cc_run_counts_for_today(&r, D));
  T_TRUE(!cc_run_counts_for_today(&r, D + 1));  // run started on D, today D+1

  // Start resumes only today's run of the same routine; yesterday's parked run starts fresh
  R.id = 7;
  cc_run_start(&r, &R, first, t);
  T_TRUE(cc_run_resumes(&r, 7, D));
  T_TRUE(!cc_run_resumes(&r, 8, D));
  T_TRUE(!cc_run_resumes(&r, 7, D + 1));
  r.active = 0;
  T_TRUE(!cc_run_resumes(&r, 7, D));
}

// Steps that step up, an every-other-day step and an alternative.
void test_run_steps(void) {
  CcStep st[4] = {
    step(CC_STEP_MANUAL, "Curls", 0), step(CC_STEP_TIMED, "Plank", 60),
    step(CC_STEP_MANUAL, "Red light", 0), step(CC_STEP_MANUAL, "Shoulder press", 0),
  };
  st[0].flags = CC_SF_QTY | CC_SF_WEIGHT;
  st[0].amount = (CcRamp){ 8, 0, D, 1, 7 }; st[0].weight = (CcRamp){ 100, 0, D, 10, 28 }; st[0].alt = 3;
  st[1].amount = (CcRamp){ 60, 0, D, 5, 7 };
  st[2].show_every = 2; st[2].show_since = D;
  st[3].flags = CC_SF_QTY; st[3].amount = (CcRamp){ 8, 0, D, 0, 0 };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 3; R.alt_count = 1;

  cc_sec t = cc_at(D + 7, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_EQ(r.step_count, 2); T_EQ(r.order[0], 0); T_EQ(r.order[1], 1);  // red light is off on D+7
  T_STR(cc_run_cur(&r, st)->name, "Curls");
  T_STR(cc_run_other(&r, &R, st)->name, "Shoulder press");
  T_TRUE(cc_run_swap(&r, &R, st, t)); T_EQ(r.order[0], 3);
  T_STR(cc_run_cur(&r, st)->name, "Shoulder press"); T_STR(cc_run_other(&r, &R, st)->name, "Curls");
  T_TRUE(cc_run_swap(&r, &R, st, t)); T_EQ(r.order[0], 0);
  cc_run_next(&r, st, cc_at(D + 14, 1));  // past midnight
  T_EQ(r.paused_left, 65);                 // the run's own day: 60 + 5
  T_TRUE(cc_run_other(&r, &R, st) == NULL);
  T_TRUE(!cc_run_swap(&r, &R, st, t));     // Plank has no alternative

  // Swapping a timed step brings its alternative waiting at its own full length.
  st[1].alt = 3; st[0].alt = CC_NO_ALT;
  st[3].type = CC_STEP_TIMED; st[3].amount = (CcRamp){ 45, 0, D, 0, 0 };
  cc_run_start(&r, &R, st, t);
  cc_run_next(&r, st, t);
  cc_run_toggle_pause(&r, t);  // running
  T_TRUE(cc_run_swap(&r, &R, st, t + 10));
  T_TRUE(cc_run_waiting(&r)); T_EQ(cc_run_remaining(&r, t + 20), 45);

  // A main step whose alt is not an alternative never offers a swap.
  st[1].alt = 0;
  cc_run_start(&r, &R, st, t);
  cc_run_next(&r, st, t);
  T_TRUE(cc_run_other(&r, &R, st) == NULL); T_TRUE(!cc_run_swap(&r, &R, st, t));

  // Run records: today's order survives; a 1.4.1 record reads in step order.
  uint8_t buf[256];
  CcRun run; memset(&run, 0, sizeof(run));
  run.item_id = 7; run.day = D; run.active = 1; run.step = 1; run.step_count = 2;
  run.order[0] = 3; run.order[1] = 1; run.value = CC_RUN_PLAN;
  int n = cc_pack_run(&run, buf, sizeof(buf));
  CcRun back;
  T_TRUE(n > 0 && cc_unpack_run(buf, n, &back));
  T_EQ(back.order[0], 3); T_EQ(back.order[1], 1); T_EQ(back.step, 1); T_EQ(back.value, CC_RUN_PLAN);
  run.value = 20;  // a count changed with Up and Down survives Leave and a relaunch
  n = cc_pack_run(&run, buf, sizeof(buf));
  T_TRUE(n > 0 && cc_unpack_run(buf, n, &back)); T_EQ(back.value, 20);
  run.swapped = 1;
  n = cc_pack_run(&run, buf, sizeof(buf));
  T_TRUE(n > 0 && cc_unpack_run(buf, n, &back)); T_EQ(back.swapped, 1);
  uint8_t v1[] = { 1, 7, 0, 0x0D, 0x51, 0, 0, 1, 1, 3, 0,  0xFF, 0xFF, 0xFF, 0x7F,  0, 0, 0, 0,  0, 0, 0, 0 };
  T_TRUE(cc_unpack_run(v1, sizeof(v1), &back));
  T_EQ(back.day, D); T_EQ(back.step_count, 3); T_EQ(back.order[0], 0); T_EQ(back.order[1], 1); T_EQ(back.order[2], 2);
  T_EQ(back.value, CC_RUN_PLAN); T_EQ(back.swapped, 0);
  run.order[1] = CC_ITEM_STEPS;  // an order past any item's steps is refused
  n = cc_pack_run(&run, buf, sizeof(buf));
  T_TRUE(!cc_unpack_run(buf, n, &back));
}


// Up and Down change a step's count; the plan's count shows until they do, and a new step starts from its plan.
void test_run_value(void) {
  CcStep st[3] = { step(CC_STEP_MANUAL, "Curls", 0), step(CC_STEP_MANUAL, "Stretch", 0), step(CC_STEP_MANUAL, "Press", 0) };
  st[0].flags = CC_SF_QTY; st[0].amount = (CcRamp){ 8, 0, D, 1, 7 }; st[0].alt = 2;
  st[2].flags = CC_SF_QTY; st[2].amount = (CcRamp){ 6, 0, D, 0, 0 };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 2; R.alt_count = 1;
  cc_sec t = cc_at(D + 7, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_EQ(r.value, CC_RUN_PLAN); T_EQ(cc_run_qty(&r, cc_run_cur(&r, st)), 9);  // 8, up 1 a week
  r.value = 7;
  T_EQ(cc_run_qty(&r, cc_run_cur(&r, st)), 7);
  T_TRUE(cc_run_swap(&r, &R, st, t));
  T_EQ(cc_run_qty(&r, cc_run_cur(&r, st)), 6);  // the alternative's own count
  r.value = 5;
  cc_run_next(&r, st, t);
  T_EQ(r.value, CC_RUN_PLAN);
}

// After a schedule change a run carries on with its routine's current steps.
void test_run_refit(void) {
  CcStep st[4] = { step(CC_STEP_MANUAL, "Curls", 0), step(CC_STEP_TIMED, "Plank", 60),
                   step(CC_STEP_MANUAL, "Stretch", 0), step(CC_STEP_MANUAL, "Press", 0) };
  st[0].flags = CC_SF_QTY; st[0].amount = (CcRamp){ 8, 0, D, 0, 0 }; st[0].alt = 3;
  st[3].flags = CC_SF_QTY; st[3].amount = (CcRamp){ 6, 0, D, 0, 0 };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 3; R.alt_count = 1;
  cc_sec t = cc_at(D, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_TRUE(cc_run_swap(&r, &R, st, t)); T_EQ(r.swapped, 1);
  r.value = 5;

  // A save that leaves the current step alone keeps the swap, the place and a changed count.
  CcStep was = st[3];
  st[2].amount.start = 3;
  T_TRUE(cc_run_refit(&r, &R, st, &was, t));
  T_EQ(r.order[0], 3); T_EQ(r.step, 0); T_EQ(r.value, 5);
  // New numbers for the current step win over a changed count.
  was = st[3]; st[3].amount.start = 8;
  T_TRUE(cc_run_refit(&r, &R, st, &was, t));
  T_EQ(r.order[0], 3); T_EQ(r.value, CC_RUN_PLAN); T_EQ(cc_run_qty(&r, cc_run_cur(&r, st)), 8);

  // Its alternative went: the run carries on with the main step.
  st[0].alt = CC_NO_ALT; R.alt_count = 0; r.value = 5;
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t));
  T_EQ(r.order[0], 0); T_EQ(r.step, 0); T_EQ(r.value, CC_RUN_PLAN); T_EQ(r.swapped, 0);

  // The current step became a timer: it waits at its full length. Back to Do it: no clock.
  st[0].type = CC_STEP_TIMED; st[0].amount = (CcRamp){ 45, 0, D, 0, 0 };
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t + 5));
  T_TRUE(cc_run_waiting(&r)); T_EQ(cc_run_remaining(&r, t + 9), 45);
  cc_sec end = r.step_end;
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t + 30)); T_EQ(r.step_end, end);  // nothing changed: nothing moves
  st[0].amount.start = 50;                                                  // a new length before Start
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t + 30)); T_EQ(cc_run_remaining(&r, t + 40), 50);
  st[0].type = CC_STEP_MANUAL;
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t + 5));
  T_EQ(r.step_end, CC_NEVER); T_EQ(r.paused, 0); T_EQ(cc_run_remaining(&r, t + 9), 0);

  // A different step at the same place, such as a Number step moved up: a changed count doesn't carry over.
  r.value = 25; was = st[0];
  st[0].type = CC_STEP_NUMBER; st[0].num = (CcNumSpec){ 1, 400, 1500, 1, "kg" };
  T_TRUE(cc_run_refit(&r, &R, st, &was, t));
  T_EQ(r.value, CC_RUN_PLAN);
  st[0].type = CC_STEP_MANUAL;

  // A later step that changed kind is entered as it is now, when the run gets there.
  st[1].type = CC_STEP_MANUAL;
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t));
  cc_run_next(&r, st, t);
  T_EQ(r.step_end, CC_NEVER); T_TRUE(!cc_run_waiting(&r));

  // A different number of steps today, or no longer a routine: the run is over.
  R.step_count = 2; T_TRUE(!cc_run_refit(&r, &R, st, NULL, t));
  R.step_count = 3; R.kind = CC_TASK; T_TRUE(!cc_run_refit(&r, &R, st, NULL, t));
}

// Only a swap the run made is kept, and it follows its alternative to a new index.
void test_run_refit_swaps(void) {
  // A every other day (off today), B, C: parked on B, not swapped.
  CcStep st[4] = { step(CC_STEP_MANUAL, "A", 0), step(CC_STEP_MANUAL, "B", 0), step(CC_STEP_MANUAL, "C", 0),
                   step(CC_STEP_MANUAL, "C alt", 0) };
  st[0].show_every = 2; st[0].show_since = D + 1;
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 3;
  cc_sec t = cc_at(D, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_EQ(r.step_count, 2); T_EQ(r.order[0], 1); T_EQ(r.order[1], 2);
  // The planner deletes A and gives C an alternative: B(0), C(1, alt 2), C alt(2).
  st[0] = st[1]; st[1] = st[2]; st[2] = st[3]; st[1].alt = 2;
  R.step_count = 2; R.alt_count = 1;
  T_TRUE(cc_run_refit(&r, &R, st, NULL, t));
  T_EQ(r.order[0], 0); T_EQ(r.order[1], 1);  // C, not its new alternative
  T_STR(cc_run_cur(&r, st)->name, "B");

  // Swapped to Press, then Plank's alternative is deleted, so Press moves from 4 to 3.
  CcStep g[5] = { step(CC_STEP_MANUAL, "Curls", 0), step(CC_STEP_MANUAL, "Plank", 0), step(CC_STEP_MANUAL, "Stretch", 0),
                  step(CC_STEP_MANUAL, "Wall sit", 0), step(CC_STEP_MANUAL, "Press", 0) };
  g[0].alt = 4; g[1].alt = 3;
  CcItem G; memset(&G, 0, sizeof(G));
  G.id = 8; G.kind = CC_ROUTINE; G.days = 0x7F; G.step_count = 3; G.alt_count = 2;
  cc_run_start(&r, &G, g, t);
  T_TRUE(cc_run_swap(&r, &G, g, t)); T_EQ(r.order[0], 4);
  g[3] = g[4]; g[0].alt = 3; g[1].alt = CC_NO_ALT; G.alt_count = 1;
  T_TRUE(cc_run_refit(&r, &G, g, NULL, t));
  T_EQ(r.order[0], 3); T_STR(cc_run_cur(&r, g)->name, "Press");
  T_TRUE(cc_run_swap(&r, &G, g, t)); T_EQ(r.swapped, 0);  // back to Curls
}

// A parked run read at launch carries on only if its routine and today's steps still match it.
void test_run_alive(void) {
  CcStep st[3] = { step(CC_STEP_MANUAL, "Pushups", 0), step(CC_STEP_TIMED, "Plank", 60), step(CC_STEP_MANUAL, "Stretch", 0) };
  CcItem items[2]; memset(items, 0, sizeof(items));
  items[0].id = 4; items[0].kind = CC_TASK;
  items[1].id = 7; items[1].kind = CC_ROUTINE; items[1].days = 0x7F; items[1].step_count = 3;
  cc_pool_layout(items, 2);
  CcRun r;
  cc_run_start(&r, &items[1], st, cc_at(D, 420));
  cc_sec now = cc_at(D, 430);
  T_TRUE(cc_run_alive(&r, items, 2, st, now));
  T_TRUE(!cc_run_alive(&r, items, 2, st, now + 86400));  // parked on an earlier day
  items[1].step_count = 2;                                // a step that wouldn't read was dropped at load
  T_TRUE(!cc_run_alive(&r, items, 2, st, now));
  items[1].step_count = 3; items[1].id = 8;               // its routine went
  T_TRUE(!cc_run_alive(&r, items, 2, st, now));
  items[1].id = 7; r.active = 0;
  T_TRUE(!cc_run_alive(&r, items, 2, st, now));
}

// A skip can be taken back for a few seconds, and it never counts as a done step.
void test_run_skip(void) {
  CcStep st[3] = { step(CC_STEP_MANUAL, "A", 0), step(CC_STEP_TIMED, "B", 30), step(CC_STEP_MANUAL, "C", 0) };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 3;
  cc_sec t = cc_at(D, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_EQ(r.started, t); T_EQ(r.done, 0);
  T_EQ(cc_run_next(&r, st, t + 10), CC_RUN_NONE);  // A done
  T_EQ(r.done, 1); T_EQ(r.advanced_at, 0);
  T_EQ(cc_run_skip(&r, st, t + 20), CC_RUN_NONE);  // B skipped
  T_EQ(r.step, 2); T_EQ(r.done, 1); T_EQ(r.advanced_at, t + 20);
  T_TRUE(cc_run_undo(&r, st, t + 24));             // back on B, waiting at its full length
  T_EQ(r.step, 1); T_TRUE(cc_run_waiting(&r)); T_EQ(r.done, 1);
  T_TRUE(!cc_run_undo(&r, st, t + 25));            // once
  T_EQ(cc_run_skip(&r, st, t + 30), CC_RUN_NONE);
  T_TRUE(!cc_run_undo(&r, st, t + 36));            // too late
  T_EQ(cc_run_skip(&r, st, t + 40), CC_RUN_FINISHED);
  T_EQ(r.done, 1); T_EQ(r.active, 0);

  // A timer that runs out is done; undoing it takes that back.
  cc_run_start(&r, &R, st, t);
  cc_run_skip(&r, st, t);
  cc_run_toggle_pause(&r, t + 1);
  T_EQ(cc_run_tick(&r, st, t + 31), CC_RUN_TIME_UP);
  T_EQ(r.done, 2);
  T_TRUE(cc_run_undo(&r, st, t + 33));
  T_EQ(r.done, 0); T_EQ(r.step, 1);
}

// The run's end in the log: steps << 25 | steps done << 20 | seconds on its screens, parked time left out.
void test_run_log_value(void) {
  CcStep st[3] = { step(CC_STEP_MANUAL, "A", 0), step(CC_STEP_MANUAL, "B", 0), step(CC_STEP_MANUAL, "C", 0) };
  CcItem R; memset(&R, 0, sizeof(R));
  R.id = 7; R.kind = CC_ROUTINE; R.days = 0x7F; R.step_count = 3;
  cc_sec t = cc_at(D, 420);
  CcRun r;
  cc_run_start(&r, &R, st, t);
  T_EQ(cc_run_log_value(&r, t + 34 * 60), 3 << 25 | 2040);
  cc_run_next(&r, st, t);
  cc_run_skip(&r, st, t);
  T_EQ(cc_run_log_value(&r, t + 20 * 86400), 3 << 25 | 1 << 20 | 0xFFFFF);  // one done; capped
  // Parked for an hour after a minute: the hour doesn't count, and a run that ends parked stops at the park.
  cc_run_away(&r, t + 60);
  T_EQ(cc_run_log_value(&r, t + 9999), 3 << 25 | 1 << 20 | 60);
  cc_run_back(&r, t + 3660);
  T_EQ(r.away_at, 0);
  T_EQ(cc_run_log_value(&r, t + 3720), 3 << 25 | 1 << 20 | 120);
  cc_run_back(&r, t + 5000);  // not away: nothing moves
  T_EQ(cc_run_log_value(&r, t + 3720), 3 << 25 | 1 << 20 | 120);
  r.started = 0;
  T_EQ(cc_run_log_value(&r, t), 3 << 25 | 1 << 20);  // a run from 1.5.0 has no start: no length
}
