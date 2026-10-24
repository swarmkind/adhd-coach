#pragma once
// Touch on Coach's own screens, as pure logic: taps built from raw touch events, and which
// button of the rail a tap landed on.
#include "cc_types.h"

typedef struct { int16_t x, y, w, h; } CcBox;  // screen pixels
// A tap built from raw touch events: a touchdown, at most CC_TAP_SLOP pixels of movement either
// way, then a liftoff. The tap is at the touchdown point. Event types follow TouchEventType.
enum { CC_TOUCH_DOWN, CC_TOUCH_UP, CC_TOUCH_MOVE };
#define CC_TAP_SLOP 12
typedef struct { int16_t x, y; uint8_t down, moved; } CcTapTrack;
bool cc_tap_feed(CcTapTrack *t, int type, int x, int y, int *tap_x, int *tap_y);  // true on a tap

// The rail: a tap at or right of rail_x is the button beside it. Up above split_up, Select down
// to split_down, Down below that.
typedef enum { CC_RAIL_NONE, CC_RAIL_UP, CC_RAIL_SELECT, CC_RAIL_DOWN } CcRailZone;
CcRailZone cc_rail_zone(int x, int y, int rail_x, int split_up, int split_down);
