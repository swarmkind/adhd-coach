#include "t.h"

int t_pass = 0, t_fail = 0;

int main(void) {
  RUN(test_time);
  RUN(test_codec);
  RUN(test_sched);
  RUN(test_state);
  RUN(test_anytime_next);
  RUN(test_fires);
  RUN(test_run);
  RUN(test_run_steps);
  RUN(test_run_value);
  RUN(test_run_refit);
  RUN(test_run_refit_swaps);
  RUN(test_run_alive);
  RUN(test_num);
  RUN(test_ring);
  RUN(test_event);
  RUN(test_focus);
  RUN(test_focus_setup);
  RUN(test_focus_reset);
  RUN(test_touch);
  RUN(test_alarm);
  RUN(test_note);
  RUN(test_sync);
  RUN(test_battery);
  RUN(test_todo);
  RUN(test_steps);
  RUN(test_keys);
  printf("%d passed, %d failed\n", t_pass, t_fail);
  return t_fail ? 1 : 0;
}
