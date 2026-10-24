#pragma once
// Voice capture through the Dictation API: no confirmation screen, no error dialogs.
#include <stdbool.h>
#include <stdint.h>

typedef void (*VoiceDone)(bool saved);

// Records one note, linked to the running routine, Focus label or started task. `done` gets
// false only when nothing was saved (Back out of listening).
void voice_note_start(VoiceDone done);

// One dictation for a rename: `got` receives the text, or NULL when it failed or was cancelled.
void voice_dictate_text(void (*got)(const char *text));

// "phone not reachable" and so on, for a missed note's reason.
const char *voice_reason_text(uint8_t reason);
