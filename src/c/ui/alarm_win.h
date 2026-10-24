#pragma once
// A ringing alarm: a ring every few seconds until Snooze or Stop, or ten minutes have passed.
#include <stdbool.h>

void alarm_win_ring(int index);  // into g_alarms; ignored while an alarm is already ringing
