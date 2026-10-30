#pragma once
// A routine in progress: which step, and the clock of a timed step.
#include "cc_types.h"

typedef struct {
  uint16_t item_id;
  int32_t day;                // local day the run started
  uint8_t active, step, step_count, paused;  // paused: 0 running, 1 stopped, CC_RUN_READY not started
  cc_sec step_end;            // CC_NEVER for untimed steps
  cc_sec advanced_at;         // last auto-advance (0 = none), for undo
  int32_t paused_left;        // seconds left when paused
  uint8_t order[CC_MAX_STEPS];  // today's steps as indices among the item's steps; a swap holds the alternative's
  int32_t value;              // the current step's count as changed with Up and Down; CC_RUN_PLAN until then
  uint32_t swapped;           // bit i: order[i] is the alternative of today's main step i
} CcRun;

typedef enum { CC_RUN_NONE, CC_RUN_TIME_UP, CC_RUN_FINISHED } CcRunEvent;

#define CC_UNDO_S 5
#define CC_RUN_READY 2  // a timed step waits for Start, at its full length
#define CC_RUN_PLAN INT32_MIN  // run.value: nothing changed, the plan's count shows

// `steps` is always the item's own steps (cc_item_steps): main steps, then alternatives.
void cc_run_start(CcRun *r, const CcItem *it, const CcStep steps[], cc_sec now);  // today's steps, in order
const CcStep *cc_run_cur(const CcRun *r, const CcStep steps[]);
const CcStep *cc_run_other(const CcRun *r, const CcItem *it, const CcStep steps[]);  // what a swap brings, or NULL
bool cc_run_swap(CcRun *r, const CcItem *it, const CcStep steps[], cc_sec now);      // false if nothing to swap
int32_t cc_run_qty(const CcRun *r, const CcStep *s);  // a Do it step's count: as changed, or today's planned
// After a schedule change: the run takes its routine's current steps, keeping its place and its swaps where the
// step still has an alternative. `was` is the current step before the change (NULL if unknown); if it differs, the
// edit wins over a changed count. False when the run can't carry on: not a routine, or a different number of steps today.
bool cc_run_refit(CcRun *r, const CcItem *it, const CcStep steps[], const CcStep *was, cc_sec now);
// Whether an active run carries on today: its routine is still there and cc_run_refit takes it.
bool cc_run_alive(CcRun *r, const CcItem items[], int n, const CcStep pool[], cc_sec now);
CcRunEvent cc_run_next(CcRun *r, const CcStep steps[], cc_sec now);   // Select on manual/number, Back = skip
CcRunEvent cc_run_tick(CcRun *r, const CcStep steps[], cc_sec now);   // auto-advance at zero
bool cc_run_undo(CcRun *r, const CcStep steps[], cc_sec now);         // <= 5 s after an auto-advance
void cc_run_toggle_pause(CcRun *r, cc_sec now);                       // Start, stop, carry on
bool cc_run_waiting(const CcRun *r);                                  // a timed step not started yet
int32_t cc_run_remaining(const CcRun *r, cc_sec now);                 // seconds; 0 for untimed
bool cc_run_counts_for_today(const CcRun *r, int32_t today);
bool cc_run_resumes(const CcRun *r, uint16_t id, int32_t today);   // Start on `id` continues r, not a new run
