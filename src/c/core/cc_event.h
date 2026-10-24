#pragma once
// Dated events and their advance-reminder ladders. A ladder entry ("rung") is d > 0 days
// before at the event reminder time, the morning of, or one hour before.
#include "cc_types.h"

#define CC_RUNG_MORNING 0
#define CC_RUNG_HOUR (-1)
enum { CC_LADDER_GENTLE, CC_LADDER_NORMAL, CC_LADDER_PERSISTENT };

int cc_ladder_preset(int preset, int8_t out[CC_LADDER_MAX]);  // returns the rung count
cc_sec cc_event_at(const CcItem *ev);
// When one rung alerts; CC_NEVER for an entry that isn't a rung. Morning of is the event
// reminder time, or an hour before when the event is less than an hour after it.
cc_sec cc_rung_at(const CcItem *ev, int8_t rung, const CcSettings *cfg);
// Every rung before the event, ascending, duplicates merged. Returns the count.
int cc_event_rungs(const CcItem *ev, const CcSettings *cfg, cc_sec out[], int max);
cc_sec cc_event_first_rung_on(const CcItem *ev, int32_t day, const CcSettings *cfg);  // 0 = none that day
