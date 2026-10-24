#include "cc_note.h"
#include "cc_codec.h"
#include <string.h>

// Little-endian, field by field, like cc_codec.c. A writer/reader that fails once stays failed.
typedef struct { uint8_t *p; int len, pos; bool ok; } W;
typedef struct { const uint8_t *p; int len, pos; bool ok; } R;

static void w8(W *w, uint32_t v) {
  if (!w->ok || w->pos + 1 > w->len) { w->ok = false; return; }
  w->p[w->pos++] = (uint8_t)v;
}
static void w16(W *w, uint32_t v) { w8(w, v & 0xFF); w8(w, (v >> 8) & 0xFF); }
static void w32(W *w, uint32_t v) { w16(w, v & 0xFFFF); w16(w, (v >> 16) & 0xFFFF); }
static void wbytes(W *w, const char *s, int n) { for (int i = 0; i < n; i++) w8(w, (uint8_t)s[i]); }

static uint32_t r8(R *r) {
  if (!r->ok || r->pos + 1 > r->len) { r->ok = false; return 0; }
  return r->p[r->pos++];
}
static uint32_t r16(R *r) { uint32_t a = r8(r); return a | (r8(r) << 8); }
static uint32_t r32(R *r) { uint32_t a = r16(r); return a | (r16(r) << 16); }
static void rbytes(R *r, char *out, int n) { for (int i = 0; i < n; i++) out[i] = (char)r8(r); }

int cc_note_parts(uint16_t text_len) {
  if (text_len <= CC_NOTE_HEAD_TEXT) return 1;
  return 1 + (text_len - CC_NOTE_HEAD_TEXT + CC_NOTE_PART_TEXT - 1) / CC_NOTE_PART_TEXT;
}

uint16_t cc_note_clip(char *text) {
  cc_utf8_trunc(text, CC_NOTE_TEXT_MAX + 1);
  return (uint16_t)strlen(text);
}

// Where part p's bytes start in the transcript, and how many there are.
static int part_span(uint16_t text_len, int part, int *start) {
  if (part == 0) { *start = 0; return text_len < CC_NOTE_HEAD_TEXT ? text_len : CC_NOTE_HEAD_TEXT; }
  *start = CC_NOTE_HEAD_TEXT + (part - 1) * CC_NOTE_PART_TEXT;
  int n = text_len - *start;
  return n > CC_NOTE_PART_TEXT ? CC_NOTE_PART_TEXT : n;
}

int cc_pack_note_head(const CcNote *n, const char *text, uint8_t *buf, int len) {
  if (n->text_len > CC_NOTE_TEXT_MAX) return -1;
  W w = { buf, len, 0, true };
  w8(&w, CC_CODEC_V1);
  w32(&w, n->id); w16(&w, n->len_s); w16(&w, n->link);
  w8(&w, n->status); w8(&w, n->reason); w8(&w, n->flags); w16(&w, n->text_len);
  char name[CC_NAME_MAX];
  strncpy(name, n->name, sizeof(name) - 1);
  name[sizeof(name) - 1] = 0;
  cc_utf8_trunc(name, sizeof(name));
  int nl = (int)strlen(name);
  w8(&w, (uint32_t)nl); wbytes(&w, name, nl);
  int start, cnt = part_span(n->text_len, 0, &start);
  wbytes(&w, text + start, cnt);
  return w.ok ? w.pos : -1;
}

bool cc_unpack_note_head(const uint8_t *buf, int len, CcNote *n, char *text) {
  R r = { buf, len, 0, true };
  if (r8(&r) != CC_CODEC_V1 || !r.ok) return false;
  CcNote t; memset(&t, 0, sizeof(t));
  t.id = r32(&r); t.len_s = (uint16_t)r16(&r); t.link = (uint16_t)r16(&r);
  t.status = (uint8_t)r8(&r); t.reason = (uint8_t)r8(&r); t.flags = (uint8_t)r8(&r);
  t.text_len = (uint16_t)r16(&r);
  int nl = (int)r8(&r);
  if (!r.ok || nl >= CC_NAME_MAX || t.text_len > CC_NOTE_TEXT_MAX) return false;
  rbytes(&r, t.name, nl);
  int start, cnt = part_span(t.text_len, 0, &start);
  rbytes(&r, text, cnt);
  if (!r.ok) return false;
  text[cnt] = 0;  // the parts fill in the rest
  *n = t;
  return true;
}

int cc_pack_note_part(const CcNote *n, const char *text, int part, uint8_t *buf, int len) {
  if (part < 1 || part >= cc_note_parts(n->text_len)) return -1;
  W w = { buf, len, 0, true };
  w8(&w, CC_CODEC_V1);
  w32(&w, n->id); w8(&w, (uint32_t)part);
  int start, cnt = part_span(n->text_len, part, &start);
  wbytes(&w, text + start, cnt);
  return w.ok ? w.pos : -1;
}

bool cc_unpack_note_part(const uint8_t *buf, int len, const CcNote *n, int part, char *text) {
  R r = { buf, len, 0, true };
  if (r8(&r) != CC_CODEC_V1 || r32(&r) != n->id || (int)r8(&r) != part || !r.ok) return false;
  if (part < 1 || part >= cc_note_parts(n->text_len)) return false;
  int start, cnt = part_span(n->text_len, part, &start);
  rbytes(&r, text + start, cnt);
  if (!r.ok) return false;
  text[start + cnt] = 0;
  return true;
}

int cc_note_victim(const CcNoteSlot *slots, int n) {
  int synced = -1, oldest = -1;
  for (int i = 0; i < n; i++) {
    if (!slots[i].used) return i;
    if ((slots[i].flags & CC_NOTE_F_SYNCED) && (synced < 0 || slots[i].id < slots[synced].id)) synced = i;
    if (oldest < 0 || slots[i].id < slots[oldest].id) oldest = i;
  }
  return synced >= 0 ? synced : oldest;
}
