#pragma once
// Voice notes: a transcript with its start time, length and linked item. A note is stored as
// one header record (meta, name, the first text bytes) plus up to three part records, each
// under the 256-byte persist limit.
#include "cc_types.h"

#define CC_NOTE_TEXT_MAX 920   // transcript bytes, excluding the NUL
#define CC_NOTE_HEAD_TEXT 200
#define CC_NOTE_PART_TEXT 240
#define CC_NOTE_PARTS_MAX 4    // header + 3 parts

enum { CC_NOTE_OK, CC_NOTE_MISSED };
enum { CC_NOTE_F_SYNCED = 1 };

typedef struct {
  uint32_t id;                    // UTC second the recording started
  uint16_t len_s, link;           // recording length; linked item id, 0 = none
  uint8_t status, reason, flags;  // reason: the dictation status of a missed note
  uint16_t text_len;
  char name[CC_NAME_MAX];         // "" = show the date and time
} CcNote;

typedef struct { uint32_t id; uint8_t used, flags; } CcNoteSlot;

int cc_note_parts(uint16_t text_len);  // records needed, 1..4
uint16_t cc_note_clip(char *text);     // cut to CC_NOTE_TEXT_MAX on a character boundary; new length

int cc_pack_note_head(const CcNote *n, const char *text, uint8_t *buf, int len);  // bytes, or -1
bool cc_unpack_note_head(const uint8_t *buf, int len, CcNote *n, char *text);     // text: CC_NOTE_TEXT_MAX + 1
int cc_pack_note_part(const CcNote *n, const char *text, int part, uint8_t *buf, int len);  // part 1..3
bool cc_unpack_note_part(const uint8_t *buf, int len, const CcNote *n, int part, char *text);

// The slot a new note goes in: a free one, else the oldest synced note's, else the oldest note's.
int cc_note_victim(const CcNoteSlot *slots, int n);
