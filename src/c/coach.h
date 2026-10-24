#pragma once
// The single entry point for changes to today's state, from any window.
#include "core/cc_state.h"

const CcItem *coach_item(uint16_t id);  // NULL if absent
int coach_slot(uint16_t id);            // index into g_items / g_steps, or -1

// Roll the day state over to `now`'s day if needed: sweep the old day, push its outcomes to history.
void coach_ensure_today(cc_sec now);

void coach_apply(uint16_t id, CcAction a, cc_sec occurrence);

// Start: a routine with steps opens the runner; anything else is marked started.
void coach_start(uint16_t id);

// For actions not raised by an alert: the reminder slot "now" belongs to (the next due, mapped
// back from a Later), or `now` when nothing is left today.
cc_sec coach_current_occurrence(uint16_t id);

void coach_minute(void);  // minute tick: rollover, sweep, refresh

// COACH_DEMO only: rewrite the sample schedule with times just after now, fresh day, no run.
void coach_demo_reset(void);
