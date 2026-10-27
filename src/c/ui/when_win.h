#pragma once
// "Remind me": pick a day, an hour and minutes for a note to-do's reminder.
#include <pebble.h>
#include "../core/cc_types.h"

typedef void (*WhenDone)(cc_sec at);  // not called on Back

void when_win_open(cc_sec at, WhenDone done);
void when_win_refresh(void);  // the minute changed
