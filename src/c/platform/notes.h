#pragma once
// Voice notes in persist slots. The slot index is read on first use, so a wakeup launch that
// never touches notes reads nothing.
#include "../core/cc_note.h"
#include "../core/cc_keys.h"

#define NOTES_TOMB_MAX 60
#define NOTES_SLOTS K_NOTE_SLOTS

int notes_count(void);
int notes_list(CcNote *out, int max);                  // newest first
bool notes_read(uint32_t id, CcNote *n, char *text);   // text: CC_NOTE_TEXT_MAX + 1
bool notes_add(const CcNote *n, const char *text);     // takes cc_note_victim's slot when full
bool notes_rename(uint32_t id, const char *name);      // clears SYNCED
bool notes_delete(uint32_t id);                        // a synced note leaves a tombstone

// For sync: deleted synced notes the phone has not been told about yet.
int notes_tombs(uint32_t *out, int max);
void notes_clear_tombs(void);
void notes_drop_tombs(int k);                          // the first k reached the phone

int notes_unsynced(uint32_t *ids, int max, bool all);  // oldest first; `all` includes synced notes
void notes_mark_synced(uint32_t id);
bool notes_apply_rename(uint32_t id, const char *name);  // from the phone: stays synced
bool notes_apply_delete(uint32_t id);                    // from the phone: no tombstone
