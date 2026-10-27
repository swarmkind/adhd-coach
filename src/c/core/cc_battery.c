#include "cc_battery.h"

int cc_battery_bars(int percent) {
  if (percent > 100) percent = 100;
  return cc_battery_low(percent) ? 1 : (percent + 19) / 20;
}

bool cc_battery_low(int percent) { return percent <= 20; }
