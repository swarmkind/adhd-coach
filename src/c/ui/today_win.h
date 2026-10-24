#pragma once
// Today's list: timed items in time order, then anytime tasks. Select acts on a row.
#include <pebble.h>

void today_win_open(void);
void today_win_refresh(void);
bool today_win_on_top(void);
