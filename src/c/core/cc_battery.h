#pragma once
// The battery bar on home: five bars, one per 20%, the last one red at 20% or less.
#include <stdbool.h>

int cc_battery_bars(int percent);   // 1 to 5
bool cc_battery_low(int percent);
