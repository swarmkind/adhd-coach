#pragma once
// The Focus timer screen: work phases and breaks, kept in g_focus.
#include <pebble.h>

void focus_win_open(uint16_t label_id);  // 0 = no label; resumes a running session
bool focus_win_on_top(void);
void focus_win_finished(uint16_t label_id);  // a session ended: offer "Mark <task> done" when Focus is in front
