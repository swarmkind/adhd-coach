#pragma once
// Everything that will want the watch's attention, and the wakeups that cover it.
#include "cc_types.h"

typedef enum { CC_FIRE_DUE, CC_FIRE_HEADSUP, CC_FIRE_STEP_END, CC_FIRE_FOCUS_END, CC_FIRE_ALARM, CC_FIRE_NOTE } CcFireKind;
typedef struct { cc_sec at; uint16_t id; uint8_t kind, priority; } CcFire;

// One next due and one next heads-up per item, plus the running step's end and the Focus
// phase end (CC_NEVER if none; id 0, critical). Sorted by `at`, ties critical first; the
// earliest `max` are kept.
int cc_collect(const CcItem items[], int n, const CcDay *d, cc_sec step_end, cc_sec focus_end,
               cc_sec now, const CcSettings *cfg, CcFire out[], int max);

// Adds f to fires sorted by `at` (a full array drops whatever sorts last); the new count.
int cc_fire_insert(CcFire out[], int n, int max, CcFire f);

// Fires with at <= now + 30, ordered critical -> important -> normal, then earliest first.
int cc_due_now(const CcFire f[], int n, cc_sec now, CcFire out[], int max);

// Wakeup times for fires sorted by `at`: earlier than now + 30 is clamped to now + 30; a fire
// within 60 s of a planned time is dropped (one wake handles both); at most `max`, earliest first.
#define CC_WAKE_MAX 8
int cc_plan_wakeups(const CcFire f[], int n, cc_sec now, cc_sec out[], int max);
