#pragma once
// A routine in progress: which step, and the clock of a timed step.
#include "cc_types.h"

typedef struct {
  uint16_t item_id;
  int32_t day;                // local day the run started
  uint8_t active, step, step_count, paused;
  cc_sec step_end;            // CC_NEVER for untimed steps
  cc_sec advanced_at;         // last auto-advance (0 = none), for undo
  int32_t paused_left;        // seconds left when paused
} CcRun;

typedef enum { CC_RUN_NONE, CC_RUN_TIME_UP, CC_RUN_FINISHED } CcRunEvent;

#define CC_UNDO_S 5

void cc_run_start(CcRun *r, const CcItem *it, const CcStep steps[], cc_sec now);
CcRunEvent cc_run_next(CcRun *r, const CcStep steps[], cc_sec now);   // Select on manual/number, Back = skip
CcRunEvent cc_run_tick(CcRun *r, const CcStep steps[], cc_sec now);   // auto-advance at zero
bool cc_run_undo(CcRun *r, const CcStep steps[], cc_sec now);         // <= 5 s after an auto-advance
void cc_run_toggle_pause(CcRun *r, cc_sec now);
int32_t cc_run_remaining(const CcRun *r, cc_sec now);                 // seconds; 0 for untimed
bool cc_run_counts_for_today(const CcRun *r, int32_t today);
bool cc_run_resumes(const CcRun *r, uint16_t id, int32_t today);   // Start on `id` continues r, not a new run
