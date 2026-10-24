#include "t.h"
#include "cc_touch.h"

void test_touch(void) {
  // A tap from raw touch events: down, at most a little movement, up, at the touchdown point.
  CcTapTrack tr = { 0 };
  int x = -1, y = -1;
  T_TRUE(!cc_tap_feed(&tr, CC_TOUCH_DOWN, 140, 100, &x, &y));
  T_TRUE(cc_tap_feed(&tr, CC_TOUCH_UP, 0, 0, &x, &y));  // liftoff's own position is not used
  T_EQ(x, 140); T_EQ(y, 100);
  T_TRUE(!cc_tap_feed(&tr, CC_TOUCH_UP, 140, 100, &x, &y));  // a second liftoff is nothing
  cc_tap_feed(&tr, CC_TOUCH_DOWN, 30, 150, &x, &y);
  cc_tap_feed(&tr, CC_TOUCH_MOVE, 30 + CC_TAP_SLOP, 150 - CC_TAP_SLOP, &x, &y);  // a wobble is still a tap
  T_TRUE(cc_tap_feed(&tr, CC_TOUCH_UP, 0, 0, &x, &y)); T_EQ(x, 30); T_EQ(y, 150);
  cc_tap_feed(&tr, CC_TOUCH_DOWN, 30, 150, &x, &y);
  cc_tap_feed(&tr, CC_TOUCH_MOVE, 30, 150 + CC_TAP_SLOP + 1, &x, &y);  // a swipe is not
  cc_tap_feed(&tr, CC_TOUCH_MOVE, 30, 150, &x, &y);                    // even if it comes back
  T_TRUE(!cc_tap_feed(&tr, CC_TOUCH_UP, 30, 150, &x, &y));
  cc_tap_feed(&tr, CC_TOUCH_DOWN, 10, 10, &x, &y);
  cc_tap_feed(&tr, CC_TOUCH_DOWN, 140, 160, &x, &y);  // a missed liftoff: the new touchdown counts
  T_TRUE(cc_tap_feed(&tr, CC_TOUCH_UP, 0, 0, &x, &y)); T_EQ(x, 140); T_EQ(y, 160);

  // The rail on a 200 px screen: 38 px wide, buttons centred at 52, 114 and 176.
  T_EQ(cc_rail_zone(180, 52, 156, 83, 145), CC_RAIL_UP);
  T_EQ(cc_rail_zone(156, 82, 156, 83, 145), CC_RAIL_UP);      // the rail's own left edge
  T_EQ(cc_rail_zone(199, 83, 156, 83, 145), CC_RAIL_SELECT);
  T_EQ(cc_rail_zone(170, 144, 156, 83, 145), CC_RAIL_SELECT);
  T_EQ(cc_rail_zone(170, 145, 156, 83, 145), CC_RAIL_DOWN);
  T_EQ(cc_rail_zone(170, 227, 156, 83, 145), CC_RAIL_DOWN);
  T_EQ(cc_rail_zone(155, 114, 156, 83, 145), CC_RAIL_NONE);    // the screen body
  T_EQ(cc_rail_zone(180, -1, 156, 83, 145), CC_RAIL_NONE);
}
