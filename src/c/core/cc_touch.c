#include "cc_touch.h"

static int dist(int a, int b) { return a > b ? a - b : b - a; }

bool cc_tap_feed(CcTapTrack *t, int type, int x, int y, int *tap_x, int *tap_y) {
  switch (type) {
    case CC_TOUCH_DOWN:
      t->x = (int16_t)x; t->y = (int16_t)y; t->down = 1; t->moved = 0;
      return false;
    case CC_TOUCH_MOVE:
      if (t->down && (dist(x, t->x) > CC_TAP_SLOP || dist(y, t->y) > CC_TAP_SLOP)) t->moved = 1;
      return false;
    case CC_TOUCH_UP: {
      bool tap = t->down && !t->moved;
      t->down = 0;
      if (tap) { *tap_x = t->x; *tap_y = t->y; }
      return tap;
    }
  }
  return false;
}

CcRailZone cc_rail_zone(int x, int y, int rail_x, int split_up, int split_down) {
  if (x < rail_x || y < 0) return CC_RAIL_NONE;
  return y < split_up ? CC_RAIL_UP : y < split_down ? CC_RAIL_SELECT : CC_RAIL_DOWN;
}
