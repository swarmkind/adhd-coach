#include <pebble.h>
#include "notes.h"
#include "../core/cc_codec.h"

#define K_NOTE 6000      // 6000 + slot * 4 + part
#define K_NOTE_TOMB 44
#define NOTE_SLOTS NOTES_SLOTS

static CcNoteSlot *s_slots;  // [NOTE_SLOTS] on the heap from the first use: the app image must stay under 64 KB
static bool s_ready;
static uint8_t *s_buf;  // [PERSIST_DATA_MAX_LENGTH] on the heap from the first use
static char *s_text;  // a scratch transcript for reading headers, on the heap from the first use

static uint32_t key(int slot, int part) { return K_NOTE + slot * 4 + part; }

static void need_buf(void) {
  if (!s_buf) s_buf = malloc(PERSIST_DATA_MAX_LENGTH);
}

static bool write_key(uint32_t k, int len) {
  if (len < 0) { APP_LOG(APP_LOG_LEVEL_ERROR, "note pack failed for key %d", (int)k); return false; }
  int st = persist_write_data(k, s_buf, len);
  if (st < 0) APP_LOG(APP_LOG_LEVEL_ERROR, "note write %d failed: %d", (int)k, st);
  return st >= 0;
}

static bool read_head(int slot, CcNote *n, char *text) {
  if (!text || !s_buf || !persist_exists(key(slot, 0))) return false;  // no scratch buffer: no heap left
  int len = persist_read_data(key(slot, 0), s_buf, PERSIST_DATA_MAX_LENGTH);
  return len > 0 && cc_unpack_note_head(s_buf, len, n, text);
}

static void ensure(void) {
  if (s_ready) return;
  s_ready = true;
  need_buf();
  s_slots = calloc(NOTE_SLOTS, sizeof(*s_slots));  // ~800 bytes; fails only with no heap at all
  s_text = malloc(CC_NOTE_TEXT_MAX + 1);
  CcNote n;
  for (int i = 0; i < NOTE_SLOTS; i++) {
    bool used = read_head(i, &n, s_text);
    s_slots[i] = (CcNoteSlot){ used ? n.id : 0, used, used ? n.flags : 0 };
  }
}

static int slot_of(uint32_t id) {
  ensure();
  for (int i = 0; i < NOTE_SLOTS; i++)
    if (s_slots[i].used && s_slots[i].id == id) return i;
  return -1;
}

static void erase(int slot) {
  for (int p = 0; p < CC_NOTE_PARTS_MAX; p++)  // header first: a torn erase leaves no note
    if (persist_exists(key(slot, p))) persist_delete(key(slot, p));
  s_slots[slot] = (CcNoteSlot){ 0, 0, 0 };
}

int notes_count(void) {
  ensure();
  int n = 0;
  for (int i = 0; i < NOTE_SLOTS; i++) n += s_slots[i].used;
  return n;
}

int notes_list(CcNote *out, int max) {
  ensure();
  int n = 0;
  for (int i = 0; i < NOTE_SLOTS && n < max; i++) {
    if (!s_slots[i].used || !read_head(i, &out[n], s_text)) continue;
    int j = n++;
    while (j > 0 && out[j - 1].id < out[j].id) {  // newest first
      CcNote t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
      j--;
    }
  }
  return n;
}

bool notes_read(uint32_t id, CcNote *n, char *text) {
  int slot = slot_of(id);
  if (slot < 0 || !read_head(slot, n, text)) return false;
  for (int p = 1; p < cc_note_parts(n->text_len); p++) {
    int len = persist_exists(key(slot, p)) ? persist_read_data(key(slot, p), s_buf, PERSIST_DATA_MAX_LENGTH) : -1;
    if (len <= 0 || !cc_unpack_note_part(s_buf, len, n, p, text)) {  // a lost part: keep what reads
      APP_LOG(APP_LOG_LEVEL_WARNING, "note %u part %d unreadable", (unsigned)id, p);
      n->text_len = (uint16_t)strlen(text);
      break;
    }
  }
  return true;
}

bool notes_add(const CcNote *in, const char *text) {
  ensure();
  CcNote n = *in;
  while (slot_of(n.id) >= 0) n.id++;  // two notes in one second: keep ids unique
  int slot = cc_note_victim(s_slots, NOTE_SLOTS);
  if (s_slots[slot].used) {
    APP_LOG(APP_LOG_LEVEL_INFO, "notes full: dropping %u (%s)", (unsigned)s_slots[slot].id,
            (s_slots[slot].flags & CC_NOTE_F_SYNCED) ? "synced" : "NOT synced");
  }
  erase(slot);
  for (int p = 1; p < cc_note_parts(n.text_len); p++)  // parts first, header last
    if (!write_key(key(slot, p), cc_pack_note_part(&n, text, p, s_buf, PERSIST_DATA_MAX_LENGTH))) return false;
  if (!write_key(key(slot, 0), cc_pack_note_head(&n, text, s_buf, PERSIST_DATA_MAX_LENGTH))) return false;
  s_slots[slot] = (CcNoteSlot){ n.id, 1, n.flags };
  return true;
}

// Rewrites a note's header: a new name (NULL keeps it) and flags.
static bool rewrite_head(uint32_t id, const char *name, uint8_t set, uint8_t clear) {
  int slot = slot_of(id);
  CcNote n;
  if (slot < 0 || !read_head(slot, &n, s_text)) return false;  // the header holds the first text bytes
  if (name) {
    char t[CC_NAME_MAX * 2];  // wider than a name, so the cut sees a split character
    strncpy(t, name, sizeof(t) - 1);
    t[sizeof(t) - 1] = 0;
    cc_utf8_trunc(t, CC_NAME_MAX);
    strcpy(n.name, t);
  }
  n.flags = (uint8_t)((n.flags | set) & ~clear);
  if (!write_key(key(slot, 0), cc_pack_note_head(&n, s_text, s_buf, PERSIST_DATA_MAX_LENGTH))) return false;
  s_slots[slot].flags = n.flags;
  return true;
}

bool notes_rename(uint32_t id, const char *name) { return rewrite_head(id, name, 0, CC_NOTE_F_SYNCED); }
bool notes_apply_rename(uint32_t id, const char *name) { return rewrite_head(id, name, 0, 0); }

void notes_mark_synced(uint32_t id) {
  int slot = slot_of(id);
  if (slot >= 0 && !(s_slots[slot].flags & CC_NOTE_F_SYNCED)) rewrite_head(id, NULL, CC_NOTE_F_SYNCED, 0);
}

int notes_unsynced(uint32_t *ids, int max, bool all) {
  ensure();
  int n = 0;
  for (int i = 0; i < NOTE_SLOTS && n < max; i++) {
    if (!s_slots[i].used || (!all && (s_slots[i].flags & CC_NOTE_F_SYNCED))) continue;
    int j = n++;
    ids[j] = s_slots[i].id;
    for (; j > 0 && ids[j - 1] > ids[j]; j--) { uint32_t t = ids[j]; ids[j] = ids[j - 1]; ids[j - 1] = t; }
  }
  return n;
}

static int read_tombs(uint32_t *out) {
  need_buf();
  if (!s_buf) return 0;
  int len = persist_exists(K_NOTE_TOMB) ? persist_read_data(K_NOTE_TOMB, s_buf, PERSIST_DATA_MAX_LENGTH) : -1;
  if (len < 2 || s_buf[0] != CC_CODEC_V1 || s_buf[1] > NOTES_TOMB_MAX || len < 2 + 4 * s_buf[1]) return 0;
  int n = s_buf[1];
  for (int i = 0; i < n; i++) {
    const uint8_t *p = &s_buf[2 + 4 * i];
    out[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  }
  return n;
}

static void write_tombs(const uint32_t *ids, int n) {
  need_buf();
  if (!s_buf) return;
  s_buf[0] = CC_CODEC_V1;
  s_buf[1] = (uint8_t)n;
  for (int i = 0; i < n; i++) {
    uint8_t *p = &s_buf[2 + 4 * i];
    p[0] = ids[i] & 0xFF; p[1] = (ids[i] >> 8) & 0xFF; p[2] = (ids[i] >> 16) & 0xFF; p[3] = ids[i] >> 24;
  }
  write_key(K_NOTE_TOMB, 2 + 4 * n);
}

bool notes_delete(uint32_t id) {
  int slot = slot_of(id);
  if (slot < 0) return false;
  if (s_slots[slot].flags & CC_NOTE_F_SYNCED) {  // the phone has it: tell it at the next sync
    uint32_t t[NOTES_TOMB_MAX];
    int n = read_tombs(t);
    if (n == NOTES_TOMB_MAX) { memmove(t, t + 1, sizeof(t[0]) * (n - 1)); n--; }  // full: forget the oldest
    t[n++] = id;
    write_tombs(t, n);
  }
  erase(slot);
  return true;
}

bool notes_apply_delete(uint32_t id) {
  int slot = slot_of(id);
  if (slot < 0) return false;
  erase(slot);
  return true;
}

int notes_tombs(uint32_t *out, int max) {
  uint32_t t[NOTES_TOMB_MAX];
  int n = read_tombs(t);
  if (n > max) n = max;
  memcpy(out, t, sizeof(t[0]) * n);
  return n;
}

void notes_clear_tombs(void) {
  if (persist_exists(K_NOTE_TOMB)) persist_delete(K_NOTE_TOMB);
}

void notes_drop_tombs(int k) {
  uint32_t t[NOTES_TOMB_MAX];
  int n = read_tombs(t);
  if (k <= 0) return;
  if (k >= n) { notes_clear_tombs(); return; }
  write_tombs(t + k, n - k);
}
