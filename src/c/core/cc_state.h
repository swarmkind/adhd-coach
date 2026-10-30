#pragma once
// Today's per-item state: what the buttons do to it, missed sweeps, and midnight rollover.
#include "cc_types.h"

typedef enum { CC_A_START, CC_A_DONE, CC_A_SKIP, CC_A_LATER, CC_A_IGNORED, CC_A_PAUSE, CC_A_HEADSUP_SEEN,
               CC_A_LATER_LONG, CC_A_RESUME } CcAction;  // LATER_LONG = More > Later 60 min
#define CC_LATER_LONG_MIN 60

// `occurrence` (the slot or rung from cc_occurrence_for) matters for reminders and events.
void cc_apply(CcItemState *st, const CcItem *it, CcAction a, cc_sec now, cc_sec occurrence, const CcSettings *cfg);

CcItemState *cc_state_for(CcDay *d, uint16_t id);  // NULL if absent

// The first anytime task still open on d's day, as an index into items, or -1: what Done offers next.
int cc_anytime_next(const CcItem items[], int n, CcDay *d);

// Quiet Time holds a reminder's alert (answered as Skip, silently); everything else alerts.
bool cc_quiet_holds(const CcItem *it, uint8_t fire_kind);

// Marks due times older than the catch-up window as missed; returns how many states changed.
// Leaves ACTIVE, PAUSED, DONE and SKIPPED alone.
int cc_sweep(CcDay *d, const CcItem items[], int n, cc_sec now, const CcSettings *cfg);

typedef struct { uint16_t id; uint8_t status, count, missed; } CcOutcome;

// If d->day != today: one outcome per old state, then fresh states for `items`.
// Returns outcomes written; 0 on the same day (and nothing changes).
int cc_rollover(CcDay *d, const CcItem items[], int n, int32_t today, CcOutcome out[], int max);
