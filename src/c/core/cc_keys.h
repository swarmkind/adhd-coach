#pragma once
// The watch's persist keys in one place, so a host test can check that no two ranges overlap.
#include "cc_types.h"

#define K_SETTINGS 1
#define K_SCHEDULE 2
#define K_DAY 10          // + chunk, K_DAY_N
#define K_RUN 20
#define K_FOCUS 22
#define K_LAST 30         // + chunk, K_LAST_N
#define K_LOG_RING 40
#define K_HIST_RING 41
#define K_NOTE_TOMB 44
#define K_SYNC 45
#define K_SOUNDS 46       // watch-only, not synced
#define K_ALARMS 47       // watch-only, not synced
#define K_TODOS 48        // watch-only, not synced
#define K_ITEM 100        // + slot
#define K_STEP 1000       // 1.4.1: + slot * K_STEP_V1 + step, read once to migrate
#define K_HIST 4000       // + ring slot, K_HIST_N
#define K_LOG 5000        // + ring slot, K_LOG_N
#define K_NOTE 6000       // + slot * CC_NOTE_PARTS_MAX + part, K_NOTE_SLOTS
#define K_STEP2 7000      // + slot * CC_ITEM_STEPS + index: main steps, then alternatives

#define K_DAY_N 3
#define K_LAST_N 4
#define K_STEP_V1 16
#define K_HIST_N 90
#define K_LOG_N 200
#define K_NOTE_SLOTS 100
