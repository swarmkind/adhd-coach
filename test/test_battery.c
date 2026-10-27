#include "t.h"
#include "cc_battery.h"

void test_battery(void) {
  // Five bars, one per 20%; the last one left is the low one.
  T_EQ(cc_battery_bars(100), 5);
  T_EQ(cc_battery_bars(90), 5);
  T_EQ(cc_battery_bars(80), 4);
  T_EQ(cc_battery_bars(50), 3);
  T_EQ(cc_battery_bars(30), 2);
  T_EQ(cc_battery_bars(20), 1);
  T_EQ(cc_battery_bars(10), 1);
  T_EQ(cc_battery_bars(0), 1);    // nearly flat still shows the red bar
  T_EQ(cc_battery_bars(120), 5);  // out of range: clamped
  T_TRUE(cc_battery_low(20));
  T_TRUE(cc_battery_low(0));
  T_TRUE(!cc_battery_low(30));
}
