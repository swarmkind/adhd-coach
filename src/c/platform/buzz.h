#pragma once
#include <stdint.h>
#include <stdbool.h>

// Each cue is a vibration and, when Sounds is on and the watch isn't muted or in Quiet Time,
// a short tone of its own.

void buzz_alert(uint8_t priority);  // tasks and routines, strength by priority
void buzz_headsup(void);
void buzz_nudge(void);              // reminders
void buzz_tick(void);               // saved
void buzz_step_done(void);          // a routine step's timer reached zero
void buzz_missed(void);             // a voice note saved without a transcript
void buzz_focus(bool finished);     // a Focus phase ended, or the whole session
void buzz_alarm(bool sound);        // one ring, with beeps or vibration only; rings through Quiet Time
