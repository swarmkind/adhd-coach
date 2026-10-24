#pragma once
// Wakeups: the watch launches Coach when something is due, even with the app closed.

void wake_rearm(void);          // cancel all, then arm the next (at most 8) fire times
bool wake_focus_check(void);   // advance a Focus whose phase ended: buzz, save, rearm; true if it moved
void wake_rearm_on_exit(void);  // the same, keeping fires due now (an unanswered alert returns)
int wake_push_due(void);      // alerts for what is due now; returns how many it showed (Quiet Time holds some)
void wake_process_due(void);  // rollover, sweep, push due alerts, save, rearm
