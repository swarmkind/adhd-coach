#pragma once
// The Focus (pomodoro) timer: work and short breaks in turn, a long break after the last
// work phase, then finished. Phases chain from the previous end, so a closed app lands
// exactly where a running one would.
#include "cc_types.h"
#include "cc_touch.h"

typedef enum { CC_FOCUS_WORK, CC_FOCUS_SHORT, CC_FOCUS_LONG } CcFocusPhase;
typedef struct {
  uint8_t active, phase, cycle, cycles, paused;   // cycle 1..cycles
  uint8_t work_min, short_min, long_min;          // copied from settings at start
  uint16_t label_id;                              // 0 = no task
  cc_sec phase_end;                               // CC_NEVER while paused
  int32_t paused_left;
} CcFocus;
typedef enum { CC_FOCUS_NONE, CC_FOCUS_PHASE, CC_FOCUS_FINISHED } CcFocusEvent;

void cc_focus_start(CcFocus *f, const CcSettings *cfg, uint16_t label_id, cc_sec now);
CcFocusEvent cc_focus_tick(CcFocus *f, cc_sec now);   // advances through every phase that ended
CcFocusEvent cc_focus_skip(CcFocus *f, cc_sec now);   // next phase now
void cc_focus_toggle_pause(CcFocus *f, cc_sec now);
void cc_focus_add(CcFocus *f, int minutes, cc_sec now);
int32_t cc_focus_remaining(const CcFocus *f, cc_sec now);  // seconds
void cc_focus_stop(CcFocus *f);
cc_sec cc_focus_end(const CcFocus *f);                // phase_end, or CC_NEVER if inactive/paused

// Lengths that Settings and the setup screen allow (the planner uses the same).
#define CC_FOCUS_WORK_MIN 5
#define CC_FOCUS_WORK_MAX 90
#define CC_FOCUS_WORK_STEP 5
#define CC_FOCUS_SHORT_MIN 1
#define CC_FOCUS_SHORT_MAX 30
#define CC_FOCUS_SHORT_STEP 1

// The setup screen before a session: a tap on the left half of a length row shortens it, on
// the right half lengthens it. Boxes are in screen pixels, the ones the rows are drawn in.
typedef enum { CC_TAP_NONE, CC_TAP_WORK_LESS, CC_TAP_WORK_MORE, CC_TAP_SHORT_LESS, CC_TAP_SHORT_MORE } CcFocusTap;
CcFocusTap cc_focus_tap(CcBox work, CcBox brk, int x, int y);
bool cc_focus_adjust(CcSettings *cfg, CcFocusTap tap);  // one step onto the grid, held at the limits; true if changed
