#pragma once
// Routine runner (one screen per step), number entry, and the done screen.
#include <pebble.h>
#include "../core/cc_types.h"

#define CC_STEP_NONE 255  // number logs that belong to an item, not a routine step
#define CC_ITEM_NONE 0    // a number that is not logged (settings)

void run_win_open(void);  // shows g_run's current step: after cc_run_start, or to resume
bool run_win_showing(void);  // the runner is on the window stack

// Number entry for a step or a reminder. Save logs the value; Back skips without logging.
void number_win_open(uint16_t item, uint8_t step, const CcNumSpec *spec, void (*done)(bool saved, int32_t value));
// The same screen for a setting: starts at `value`, Save reports it without logging, Back cancels.
void number_win_open_setting(const char *title, int32_t value, const CcNumSpec *spec,
                             void (*done)(bool saved, int32_t value));
