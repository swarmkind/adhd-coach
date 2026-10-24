#include "t.h"
#include "cc_note.h"
#include "cc_codec.h"

static CcNote note_of(uint16_t text_len) {
  CcNote n; memset(&n, 0, sizeof(n));
  n.id = 1792800000u; n.len_s = 12; n.link = 1; n.status = CC_NOTE_OK; n.text_len = text_len;
  strcpy(n.name, "Idea for the garden");
  return n;
}

// Pack a note into its records and read it back; returns true if every record fit and matched.
static bool round_trip(const char *text, uint16_t len) {
  CcNote n = note_of(len), back;
  uint8_t buf[256];
  char out[CC_NOTE_TEXT_MAX + 1];
  memset(out, 'x', sizeof(out));
  int k = cc_pack_note_head(&n, text, buf, sizeof(buf));
  if (k < 0 || k > 256 || !cc_unpack_note_head(buf, k, &back, out)) return false;
  for (int p = 1; p < cc_note_parts(len); p++) {
    k = cc_pack_note_part(&n, text, p, buf, sizeof(buf));
    if (k < 0 || k > 256 || !cc_unpack_note_part(buf, k, &back, p, out)) return false;
  }
  return back.id == n.id && back.len_s == 12 && back.link == 1 && back.text_len == len &&
         strcmp(back.name, n.name) == 0 && memcmp(out, text, len) == 0 && out[len] == 0;
}

void test_note(void) {
  static char text[1001];
  for (int i = 0; i < 1000; i++) text[i] = (char)('a' + i % 26);
  text[1000] = 0;

  T_EQ(cc_note_parts(0), 1); T_EQ(cc_note_parts(200), 1); T_EQ(cc_note_parts(201), 2);
  T_EQ(cc_note_parts(440), 2); T_EQ(cc_note_parts(441), 3); T_EQ(cc_note_parts(920), 4);

  T_TRUE(round_trip(text, 0)); T_TRUE(round_trip(text, 150)); T_TRUE(round_trip(text, 200));
  T_TRUE(round_trip(text, 201)); T_TRUE(round_trip(text, 700)); T_TRUE(round_trip(text, 920));

  // Clip: 1000 ASCII bytes -> 920; an "é" (2 bytes) straddling byte 920 goes whole.
  char c[1001];
  memcpy(c, text, sizeof(c));
  T_EQ(cc_note_clip(c), 920); T_EQ(strlen(c), 920);
  memcpy(c, text, sizeof(c));
  c[919] = (char)0xC3; c[920] = (char)0xA9;
  T_EQ(cc_note_clip(c), 919); T_EQ(strlen(c), 919);
  strcpy(c, "short");
  T_EQ(cc_note_clip(c), 5);

  // A part must belong to its note and sit at its index.
  CcNote n = note_of(700), back;
  uint8_t buf[256];
  char out[CC_NOTE_TEXT_MAX + 1];
  int k = cc_pack_note_head(&n, text, buf, sizeof(buf));
  T_TRUE(cc_unpack_note_head(buf, k, &back, out));
  k = cc_pack_note_part(&n, text, 2, buf, sizeof(buf));
  T_TRUE(!cc_unpack_note_part(buf, k, &back, 1, out));
  back.id++;
  T_TRUE(!cc_unpack_note_part(buf, k, &back, 2, out));
  T_EQ(cc_pack_note_part(&n, text, 4, buf, sizeof(buf)), -1);  // no such part for 700 bytes
  T_EQ(cc_pack_note_part(&n, text, 0, buf, sizeof(buf)), -1);

  // Versioned, and a missed note keeps its reason.
  n = note_of(0); n.status = CC_NOTE_MISSED; n.reason = 5; n.name[0] = 0;
  k = cc_pack_note_head(&n, "", buf, sizeof(buf));
  T_TRUE(cc_unpack_note_head(buf, k, &back, out));
  T_EQ(back.status, CC_NOTE_MISSED); T_EQ(back.reason, 5); T_STR(back.name, ""); T_STR(out, "");
  buf[0] = 2;
  T_TRUE(!cc_unpack_note_head(buf, k, &back, out));
  T_TRUE(!cc_unpack_note_head(buf, 5, &back, out));  // truncated

  // Victim: a free slot first, then the oldest synced, then the oldest.
  CcNoteSlot s[4] = { { 30, 1, 0 }, { 10, 1, 0 }, { 20, 1, CC_NOTE_F_SYNCED }, { 0, 0, 0 } };
  T_EQ(cc_note_victim(s, 4), 3);
  s[3] = (CcNoteSlot){ 15, 1, CC_NOTE_F_SYNCED };
  T_EQ(cc_note_victim(s, 4), 3);  // 15 is the oldest synced
  s[3].flags = 0;
  T_EQ(cc_note_victim(s, 4), 2);  // only 20 is synced
  s[2].flags = 0;
  T_EQ(cc_note_victim(s, 4), 1);  // none synced: the oldest, 10
}
