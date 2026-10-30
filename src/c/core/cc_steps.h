#pragma once
// Routine steps: values that step up over time, every-N-days rhythms for steps and items,
// and the pool that holds every item's steps (main steps, then their alternatives).
#include "cc_types.h"

int32_t cc_ramp_at(const CcRamp *r, int32_t day);
bool cc_step_on_day(const CcStep *s, int32_t day);

// The pool items' step_first indexes into. Unbound (NULL), a routine counts as on whenever
// its days say so; bound, it also needs a step on that day.
void cc_steps_bind(const CcStep *pool);
const CcStep *cc_item_steps(const CcItem *it);  // NULL when unbound
bool cc_item_on_day(const CcItem *it, int32_t day);  // routines, reminders and tasks, not events

// Today's main steps, in order: their indices among the item's steps. Returns how many.
int cc_steps_today(const CcItem *it, const CcStep steps[], int32_t day, uint8_t order[CC_MAX_STEPS]);
int cc_pool_layout(CcItem items[], int n);  // sets step_first; the total, or -1 past CC_POOL_MAX
bool cc_item_steps_ok(const CcItem *it, const CcStep steps[]);  // every alternative used once, by a main step
