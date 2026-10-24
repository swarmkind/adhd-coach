#pragma once
// The settings list. Select on a row edits it on the number screen; Save keeps it on the watch.
#include <pebble.h>

#define SETTINGS_ROW_FOCUS 4  // first of the Focus rows

void settings_win_open(int first_row);
