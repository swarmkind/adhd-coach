#pragma once
// Coach's home: time, date, NOW or NEXT, and today's reminder progress.
#include <pebble.h>

void home_win_open(void);
bool home_win_on_top(void);
void ui_refresh_all(void);  // re-render home and Today after any state change
