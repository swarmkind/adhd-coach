#pragma once
// The voice log (newest first) and one note's transcript.
#include <pebble.h>
#include "../core/cc_types.h"

void voice_log_open(void);
void voice_note_open(uint32_t id);

// The reminder times a note to-do is offered: 0 in 30 min, 1 in an hour, 2 the coming morning.
#define VOICE_TODO_TIMES 3
cc_sec voice_todo_at(int which, cc_sec now);
cc_sec voice_todo_pick_start(cc_sec at, cc_sec now);  // where the picker starts: `at` if ahead, else the morning
const char *voice_todo_label(int which, cc_sec now);  // "In 30 min · 16:10"; kept until the next call
