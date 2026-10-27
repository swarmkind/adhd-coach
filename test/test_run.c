#include "t.h"
#include "cc_run.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

static CcStep step(uint8_t type, const char *name, uint16_t dur) {
  CcStep s; memset(&s, 0, sizeof(s));
  s.type = type; s.dur_s = dur; strcpy(s.name, name);
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
