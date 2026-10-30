#include "cc_sync.h"
#include "cc_codec.h"
#include <string.h>

static void put16(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

int cc_frame_put(uint8_t *buf, int cap, int pos, uint8_t type, const uint8_t *rec, int len) {
  if (pos < 0 || len < 0 || len > 0xFFFF || pos + CC_FRAME_HDR + len > cap) return -1;
  buf[pos] = type;
  put16(&buf[pos + 1], (uint32_t)len);
  memcpy(&buf[pos + CC_FRAME_HDR], rec, (size_t)len);
  return pos + CC_FRAME_HDR + len;
}

int cc_frame_next(const uint8_t *buf, int len, int pos, uint8_t *type, const uint8_t **rec, int *rec_len) {
  if (pos >= len) return 0;
  if (pos + CC_FRAME_HDR > len) return -1;
  int n = (int)get16(&buf[pos + 1]);
  if (pos + CC_FRAME_HDR + n > len) return -1;
  *type = buf[pos];
  *rec = &buf[pos + CC_FRAME_HDR];
  *rec_len = n;
  return pos + CC_FRAME_HDR + n;
}

int cc_pack_step_at(uint16_t item, uint8_t index, const CcStep *s, uint8_t *buf, int len) {
  if (len < 3) return -1;
  put16(buf, item);
  buf[2] = index;
  int n = cc_pack_step(s, buf + 3, len - 3);
  return n < 0 ? -1 : n + 3;
}

bool cc_unpack_step_at(const uint8_t *buf, int len, uint16_t *item, uint8_t *index, CcStep *s) {
  if (len < 3) return false;
  *item = (uint16_t)get16(buf);
  *index = buf[2];
  return cc_unpack_step(buf + 3, len - 3, s);
}

int cc_pack_settings_at(const CcSettings *c, uint32_t utc, uint8_t *buf, int len) {
  if (len < 4) return -1;
  put32(buf, utc);
  int n = cc_pack_settings(c, buf + 4, len - 4);
  return n < 0 ? -1 : n + 4;
}

bool cc_unpack_settings_at(const uint8_t *buf, int len, CcSettings *c, uint32_t *utc) {
  if (len < 4) return false;
  *utc = get32(buf);
  return cc_unpack_settings(buf + 4, len - 4, c);
}

int cc_pack_tombs(const uint32_t *ids, int n, uint8_t *buf, int len) {
  if (n < 0 || n > 255 || 1 + 4 * n > len) return -1;
  buf[0] = (uint8_t)n;
  for (int i = 0; i < n; i++) put32(&buf[1 + 4 * i], ids[i]);
  return 1 + 4 * n;
}

int cc_unpack_tombs(const uint8_t *buf, int len, uint32_t *ids, int max) {
  if (len < 1) return -1;
  int n = buf[0];
  if (n > max || len < 1 + 4 * n) return -1;
  for (int i = 0; i < n; i++) ids[i] = get32(&buf[1 + 4 * i]);
  return n;
}

int cc_pack_rename(uint32_t id, const char *name, uint8_t *buf, int len) {
  char t[CC_NAME_MAX * 2];  // wider than a name, so the cut sees a split character
  strncpy(t, name, sizeof(t) - 1);
  t[sizeof(t) - 1] = 0;
  cc_utf8_trunc(t, CC_NAME_MAX);
  int n = (int)strlen(t);
  if (5 + n > len) return -1;
  put32(buf, id);
  buf[4] = (uint8_t)n;
  memcpy(buf + 5, t, (size_t)n);
  return 5 + n;
}

bool cc_unpack_rename(const uint8_t *buf, int len, uint32_t *id, char name[CC_NAME_MAX]) {
  if (len < 5) return false;
  int n = buf[4];
  if (n >= CC_NAME_MAX || len < 5 + n) return false;
  *id = get32(buf);
  memcpy(name, buf + 5, (size_t)n);
  name[n] = 0;
  return true;
}

#define SYNC_LEN 17

int cc_pack_sync(const CcSync *s, uint8_t *buf, int len) {
  if (len < SYNC_LEN) return -1;
  buf[0] = CC_CODEC_V1;
  put32(buf + 1, s->rev); put32(buf + 5, s->phone_id);
  put32(buf + 9, s->settings_utc); put32(buf + 13, s->last_sync_utc);
  return SYNC_LEN;
}

bool cc_unpack_sync(const uint8_t *buf, int len, CcSync *s) {
  if (len < SYNC_LEN || buf[0] != CC_CODEC_V1) return false;
  s->rev = get32(buf + 1); s->phone_id = get32(buf + 5);
  s->settings_utc = get32(buf + 9); s->last_sync_utc = get32(buf + 13);
  return true;
}

int cc_day_prune(CcDay *d, const CcItem items[], int n) {
  int k = 0;
  for (int i = 0; i < d->n; i++) {
    bool live = false;
    for (int j = 0; j < n && !live; j++) live = items[j].id == d->s[i].id;
    if (live) d->s[k++] = d->s[i];
  }
  int dropped = d->n - k;
  d->n = (uint8_t)k;
  return dropped;
}

bool cc_stage_complete(const CcItem *items, int n, const uint64_t *step_mask) {
  for (int i = 0; i < n; i++) {
    uint64_t want = (1ull << (items[i].step_count + items[i].alt_count)) - 1;
    if ((step_mask[i] & want) != want) return false;
  }
  return true;
}

uint32_t cc_ask_delay_ms(int tries) {
  static const uint32_t ms[] = { 2000, 60000, 5 * 60000, 15 * 60000 };
  return ms[tries < 3 ? tries : 3];
}
