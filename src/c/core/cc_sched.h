#pragma once
// When does an item next want attention? All times are local seconds.
#include "cc_types.h"

// How late an alert may still fire: the larger of the catch-up window and the item's grace.
cc_sec cc_catchup_s(const CcItem *it, const CcSettings *cfg);

// Soonest time >= now - catch-up at which the item wants an alert, up to 31 days ahead
// (events: their next rung, however far). `today` is the state for cc_day_of(now)
// (NULL = fresh); later days count as fresh. CC_NEVER for silent and anytime items.
cc_sec cc_next_due(const CcItem *it, const CcItemState *today, cc_sec now, const CcSettings *cfg);

// For reminders and events: the slot or rung a due time belongs to (a Later maps back to it).
cc_sec cc_occurrence_for(const CcItem *it, const CcItemState *today, cc_sec due, const CcSettings *cfg);
// Reminders and events: the latest slot or rung <= t that day, 0 before the first.
cc_sec cc_slot_at_or_before(const CcItem *it, cc_sec t, const CcSettings *cfg);

// due - lead for important and critical routines and tasks. CC_NEVER for other items,
// once shown, for a Later, or once the due time has passed.
cc_sec cc_next_headsup(const CcItem *it, const CcItemState *today, cc_sec now, const CcSettings *cfg);
