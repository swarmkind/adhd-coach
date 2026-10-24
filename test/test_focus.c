#include "t.h"
#include "cc_focus.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

void test_focus(void) {
  CcSettings cfg = CC_SETTINGS_DEFAULT;  // 25 / 5 / 15, 4 cycles
  cc_sec t = cc_at(D, 600);
  CcFocus f;

  cc_focus_start(&f, &cfg, 7, t);
  T_EQ(f.active, 1); T_EQ(f.phase, CC_FOCUS_WORK); T_EQ(f.cycle, 1); T_EQ(f.cycles, 4);
  T_EQ(f.label_id, 7); T_EQ(f.phase_end, t + 1500); T_EQ(cc_focus_end(&f), t + 1500);
  T_EQ(cc_focus_tick(&f, t + 1499), CC_FOCUS_NONE);
  T_EQ(cc_focus_tick(&f, t + 1500), CC_FOCUS_PHASE);
  T_EQ(f.phase, CC_FOCUS_SHORT); T_EQ(f.cycle, 1); T_EQ(f.phase_end, t + 1800);

  // closed app: one tick across several phases is one event, chained from phase ends
  cc_focus_start(&f, &cfg, 0, t);
  T_EQ(cc_focus_tick(&f, t + 3400), CC_FOCUS_PHASE);
  T_EQ(f.phase, CC_FOCUS_SHORT); T_EQ(f.cycle, 2); T_EQ(f.phase_end, t + 3600);
  T_EQ(cc_focus_tick(&f, t + 3401), CC_FOCUS_NONE);

  cc_focus_start(&f, &cfg, 0, t);
  T_EQ(cc_focus_tick(&f, t + 7799), CC_FOCUS_PHASE);
  T_EQ(f.phase, CC_FOCUS_LONG); T_EQ(f.cycle, 4);
  T_EQ(cc_focus_tick(&f, t + 7800), CC_FOCUS_FINISHED);  // 4 x 1500 + 3 x 300 + 900
  T_EQ(f.active, 0); T_EQ(cc_focus_end(&f), CC_NEVER); T_EQ(cc_focus_remaining(&f, t + 7800), 0);
  cc_focus_start(&f, &cfg, 0, t);
  T_EQ(cc_focus_tick(&f, t + 20000), CC_FOCUS_FINISHED);  // hours later: finished, once
  T_EQ(cc_focus_tick(&f, t + 20060), CC_FOCUS_NONE);

  // pause and resume
  cc_focus_start(&f, &cfg, 0, t);
  cc_focus_toggle_pause(&f, t + 600);
  T_EQ(f.paused, 1); T_EQ(cc_focus_remaining(&f, t + 650), 900); T_EQ(cc_focus_end(&f), CC_NEVER);
  T_EQ(cc_focus_tick(&f, t + 5000), CC_FOCUS_NONE);  // a paused phase never ends
  cc_focus_toggle_pause(&f, t + 700);
  T_EQ(f.paused, 0); T_EQ(f.phase_end, t + 1600); T_EQ(cc_focus_remaining(&f, t + 700), 900);

  // +5 min, running and paused
  cc_focus_start(&f, &cfg, 0, t);
  cc_focus_add(&f, 5, t + 100);
  T_EQ(f.phase_end, t + 1800);
  cc_focus_toggle_pause(&f, t + 200);
  cc_focus_add(&f, 5, t + 250);
  T_EQ(cc_focus_remaining(&f, t + 250), 1900);

  // skip: work 4 -> long -> finished
  cc_focus_start(&f, &cfg, 0, t);
  for (int i = 0; i < 6; i++) T_EQ(cc_focus_skip(&f, t), CC_FOCUS_PHASE);
  T_EQ(f.phase, CC_FOCUS_WORK); T_EQ(f.cycle, 4);
  T_EQ(cc_focus_skip(&f, t + 10), CC_FOCUS_PHASE);
  T_EQ(f.phase, CC_FOCUS_LONG); T_EQ(f.phase_end, t + 910);
  T_EQ(cc_focus_skip(&f, t + 20), CC_FOCUS_FINISHED);
  T_EQ(f.active, 0);

  // lengths are copied at start: settings changed mid-flight don't move a running Focus
  cc_focus_start(&f, &cfg, 0, t);
  cfg.focus_work_min = 50; cfg.focus_short_min = 10;
  T_EQ(cc_focus_tick(&f, t + 1500), CC_FOCUS_PHASE);
  T_EQ(f.phase_end, t + 1800);
  cfg = (CcSettings)CC_SETTINGS_DEFAULT;

  cc_focus_start(&f, &cfg, 0, t);
  cc_focus_stop(&f);
  T_EQ(f.active, 0); T_EQ(cc_focus_end(&f), CC_NEVER);
}

// The setup screen before a session: the left half of a length row shortens it, the right half
// lengthens it, by the same steps and limits as Settings.
void test_focus_setup(void) {
  CcBox work = { 0, 70, 162, 64 }, brk = { 0, 134, 162, 56 };
  T_EQ(cc_focus_tap(work, brk, 20, 100), CC_TAP_WORK_LESS);
  T_EQ(cc_focus_tap(work, brk, 140, 100), CC_TAP_WORK_MORE);
  T_EQ(cc_focus_tap(work, brk, 80, 70), CC_TAP_WORK_LESS);    // top-left edge is inside
  T_EQ(cc_focus_tap(work, brk, 81, 133), CC_TAP_WORK_MORE);   // the middle belongs to the right half
  T_EQ(cc_focus_tap(work, brk, 20, 150), CC_TAP_SHORT_LESS);
  T_EQ(cc_focus_tap(work, brk, 150, 189), CC_TAP_SHORT_MORE);
  T_EQ(cc_focus_tap(work, brk, 170, 100), CC_TAP_NONE);       // the button rail
  T_EQ(cc_focus_tap(work, brk, 50, 40), CC_TAP_NONE);         // the heading
  T_EQ(cc_focus_tap(work, brk, 50, 190), CC_TAP_NONE);        // below the break row
  T_EQ(cc_focus_tap(work, brk, -1, 100), CC_TAP_NONE);

  CcSettings cfg = CC_SETTINGS_DEFAULT;  // 25 / 5
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_MORE)); T_EQ(cfg.focus_work_min, 30);
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_LESS)); T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_LESS));
  T_EQ(cfg.focus_work_min, 20);
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_SHORT_MORE)); T_EQ(cfg.focus_short_min, 6);
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_SHORT_LESS)); T_EQ(cfg.focus_short_min, 5);
  T_EQ(cfg.focus_long_min, 15); T_EQ(cfg.focus_cycles, 4);  // the rest is left alone
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_NONE));

  cfg.focus_work_min = CC_FOCUS_WORK_MIN;  // held at the limits, and nothing to save
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_WORK_LESS)); T_EQ(cfg.focus_work_min, 5);
  cfg.focus_work_min = CC_FOCUS_WORK_MAX;
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_WORK_MORE)); T_EQ(cfg.focus_work_min, 90);
  cfg.focus_short_min = CC_FOCUS_SHORT_MIN;
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_SHORT_LESS)); T_EQ(cfg.focus_short_min, 1);
  cfg.focus_short_min = CC_FOCUS_SHORT_MAX;
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_SHORT_MORE)); T_EQ(cfg.focus_short_min, 30);

  cfg.focus_work_min = 6;  // a length off the 5-minute grid snaps onto it
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_MORE)); T_EQ(cfg.focus_work_min, 10);
  cfg.focus_work_min = 6;
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_LESS)); T_EQ(cfg.focus_work_min, 5);
  cfg.focus_work_min = 120;  // out of range from an older build: back inside in one step
  T_TRUE(cc_focus_adjust(&cfg, CC_TAP_WORK_LESS)); T_EQ(cfg.focus_work_min, 90);
  cfg.focus_work_min = 120;
  T_TRUE(!cc_focus_adjust(&cfg, CC_TAP_WORK_MORE)); T_EQ(cfg.focus_work_min, 120);

}
